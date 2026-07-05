import argparse
import asyncio
import os
import random
import sys
from typing import List, Optional, Tuple

import numpy as np
import torch
import torch.nn as nn
from torch.optim import Adam

sys.path.append(os.path.dirname(os.path.abspath(os.path.dirname(__file__))))
from model.models import DiscardModel
from online_game.server import GameEnvironment


class ValueNet(nn.Module):
    def __init__(self, in_channels: int):
        super().__init__()
        self.layers = nn.Sequential(
            nn.Conv1d(in_channels, 128, kernel_size=3, padding='same'),
            nn.ReLU(inplace=True),
            nn.Conv1d(128, 64, kernel_size=3, padding='same'),
            nn.ReLU(inplace=True),
            nn.Flatten(),
            nn.Linear(64 * 34, 256),
            nn.ReLU(inplace=True),
            nn.Linear(256, 1)
        )

    def forward(self, x):
        return self.layers(x)


def set_seed(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def discounted_returns(rewards: List[float], gamma: float) -> List[float]:
    ret = 0.0
    returns = []
    for reward in reversed(rewards):
        ret = reward + gamma * ret
        returns.append(ret)
    returns.reverse()
    return returns


async def play_one_episode(env: GameEnvironment):
    env.collected_data.clear()
    env.reward_features.clear()
    env.game_start = True
    random.shuffle(env.clients)

    while env.game_start:
        env.start()
        result = await env.game_loop()
        if result is None:
            break

        scores = [p.score for p in env.agents]
        game_over, score_delta = env.game_update(result)

        for i in range(4):
            env.reward_features[i].append(torch.from_numpy(env.game.get_game_feature(score_delta[i], scores[i])))
            for item in env.collected_data[i]:
                if len(item) >= 3 and np.isscalar(item[-1]):
                    continue
                features = torch.stack(env.reward_features[i])[None].float()
                reward = env.reward(features, len(env.reward_features[i]) - 1)
                item.append(float(reward))

        if game_over:
            env.game_start = False

    return env.collected_data


def build_training_batch(collected_data, gamma: float):
    states: List[np.ndarray] = []
    actions: List[int] = []
    returns_all: List[float] = []
    episode_rewards: List[float] = []
    oracle_states: List[Optional[np.ndarray]] = []

    for player_records in collected_data.values():
        rewards = [float(item[-1]) for item in player_records if len(item) >= 3 and np.isscalar(item[-1])]
        if not rewards:
            continue
        player_returns = discounted_returns(rewards, gamma)

        idx = 0
        for item in player_records:
            if len(item) < 3 or not np.isscalar(item[-1]):
                continue
            state = item[0]
            action = item[1]
            reward = item[-1]
            oracle_state = item[2] if len(item) >= 4 else None

            states.append(state)
            actions.append(int(action))
            returns_all.append(float(player_returns[idx]))
            episode_rewards.append(float(reward))
            oracle_states.append(oracle_state)
            idx += 1

    if not states:
        return None, None, None, None, 0.0

    states_np = np.asarray(states, dtype=np.float32)
    actions_np = np.asarray(actions, dtype=np.int64)
    returns_np = np.asarray(returns_all, dtype=np.float32)

    has_oracle = len(oracle_states) > 0 and all(item is not None for item in oracle_states)
    oracle_np = np.asarray(oracle_states, dtype=np.float32) if has_oracle else None

    mean_reward = float(np.mean(episode_rewards)) if episode_rewards else 0.0
    return states_np, actions_np, returns_np, oracle_np, mean_reward


def ppo_update(
    actor,
    critic,
    actor_optimizer,
    critic_optimizer,
    states,
    actions,
    returns,
    device,
    clip_eps: float,
    entropy_coef: float,
    max_grad_norm: float,
    ppo_epochs: int,
    mini_batch_size: int,
    oracle_actor=None,
    oracle_optimizer=None,
    oracle_states=None,
    oracle_guiding_coef: float = 0.0
):
    features = torch.from_numpy(states).to(device)
    labels = torch.from_numpy(actions).to(device)
    rets = torch.from_numpy(returns).to(device)

    oracle_features = None
    if oracle_actor is not None and oracle_states is not None:
        oracle_features = torch.from_numpy(oracle_states).to(device)

    with torch.no_grad():
        old_logits = actor(features)
        old_log_probs = torch.log_softmax(old_logits, dim=1).gather(1, labels.unsqueeze(1)).squeeze(1)
        values = critic(features).squeeze(1)

        old_oracle_log_probs = None
        if oracle_actor is not None and oracle_features is not None:
            old_oracle_logits = oracle_actor(oracle_features)
            old_oracle_log_probs = torch.log_softmax(old_oracle_logits, dim=1).gather(1, labels.unsqueeze(1)).squeeze(1)

    advantages = rets - values
    if advantages.numel() > 1:
        advantages = (advantages - advantages.mean()) / (advantages.std(unbiased=False) + 1e-6)

    n = len(features)
    mini_batch_size = min(max(1, mini_batch_size), n)

    actor_losses = []
    critic_losses = []
    entropies = []
    guiding_losses = []
    oracle_losses = []

    actor.train()
    critic.train()
    if oracle_actor is not None:
        oracle_actor.train()

    for _ in range(ppo_epochs):
        perm = torch.randperm(n, device=device)
        for start in range(0, n, mini_batch_size):
            idx = perm[start: start + mini_batch_size]

            mb_features = features[idx]
            mb_labels = labels[idx]
            mb_old_log_probs = old_log_probs[idx]
            mb_advantages = advantages[idx]
            mb_returns = rets[idx]

            logits = actor(mb_features)
            log_probs = torch.log_softmax(logits, dim=1)
            selected_log_probs = log_probs.gather(1, mb_labels.unsqueeze(1)).squeeze(1)
            ratio = torch.exp(selected_log_probs - mb_old_log_probs)

            surr1 = ratio * mb_advantages
            surr2 = torch.clamp(ratio, 1.0 - clip_eps, 1.0 + clip_eps) * mb_advantages
            entropy = -(torch.softmax(logits, dim=1) * log_probs).sum(dim=1).mean()
            actor_loss = -torch.min(surr1, surr2).mean() - entropy_coef * entropy

            guiding_loss = torch.tensor(0.0, device=device)
            if oracle_actor is not None and oracle_features is not None and oracle_guiding_coef > 0:
                mb_oracle_features = oracle_features[idx]
                with torch.no_grad():
                    teacher_prob = torch.softmax(oracle_actor(mb_oracle_features), dim=1)
                guiding_loss = torch.nn.functional.kl_div(
                    log_probs,
                    teacher_prob,
                    reduction='batchmean'
                )
                actor_loss = actor_loss + oracle_guiding_coef * guiding_loss

            actor_optimizer.zero_grad()
            actor_loss.backward()
            torch.nn.utils.clip_grad_norm_(actor.parameters(), max_grad_norm)
            actor_optimizer.step()

            value = critic(mb_features).squeeze(1)
            critic_loss = torch.nn.functional.mse_loss(value, mb_returns)

            critic_optimizer.zero_grad()
            critic_loss.backward()
            torch.nn.utils.clip_grad_norm_(critic.parameters(), max_grad_norm)
            critic_optimizer.step()

            if oracle_actor is not None and oracle_features is not None and oracle_optimizer is not None:
                mb_oracle_features = oracle_features[idx]
                mb_old_oracle_log_probs = old_oracle_log_probs[idx]
                oracle_logits = oracle_actor(mb_oracle_features)
                oracle_log_probs = torch.log_softmax(oracle_logits, dim=1)
                oracle_selected_log_probs = oracle_log_probs.gather(1, mb_labels.unsqueeze(1)).squeeze(1)
                oracle_ratio = torch.exp(oracle_selected_log_probs - mb_old_oracle_log_probs)
                oracle_surr1 = oracle_ratio * mb_advantages
                oracle_surr2 = torch.clamp(oracle_ratio, 1.0 - clip_eps, 1.0 + clip_eps) * mb_advantages
                oracle_entropy = -(torch.softmax(oracle_logits, dim=1) * oracle_log_probs).sum(dim=1).mean()
                oracle_loss = -torch.min(oracle_surr1, oracle_surr2).mean() - entropy_coef * oracle_entropy

                oracle_optimizer.zero_grad()
                oracle_loss.backward()
                torch.nn.utils.clip_grad_norm_(oracle_actor.parameters(), max_grad_norm)
                oracle_optimizer.step()

                oracle_losses.append(float(oracle_loss.item()))

            actor_losses.append(float(actor_loss.item()))
            critic_losses.append(float(critic_loss.item()))
            entropies.append(float(entropy.item()))
            guiding_losses.append(float(guiding_loss.item()))

    actor.eval()
    critic.eval()
    if oracle_actor is not None:
        oracle_actor.eval()

    return {
        'actor_loss': float(np.mean(actor_losses)) if actor_losses else 0.0,
        'critic_loss': float(np.mean(critic_losses)) if critic_losses else 0.0,
        'entropy': float(np.mean(entropies)) if entropies else 0.0,
        'guiding_loss': float(np.mean(guiding_losses)) if guiding_losses else 0.0,
        'oracle_loss': float(np.mean(oracle_losses)) if oracle_losses else 0.0,
        'samples': int(n)
    }


def save_actor_checkpoint(actor, episode: int, num_layers: int, in_channels: int, output_path: str):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    torch.save(
        {
            'state_dict': actor.state_dict(),
            'num_layers': num_layers,
            'in_channels': in_channels,
            'episode': episode
        },
        output_path
    )


def save_critic_checkpoint(critic, critic_optimizer, episode: int, output_path: str):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    torch.save(
        {
            'state_dict': critic.state_dict(),
            'episode': episode,
            'optimizer_state': critic_optimizer.state_dict()
        },
        output_path
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--episodes', '-e', default=200, type=int)
    parser.add_argument('--gamma', default=0.99, type=float)
    parser.add_argument('--actor_lr', default=1e-5, type=float)
    parser.add_argument('--critic_lr', default=2e-4, type=float)
    parser.add_argument('--oracle_lr', default=2e-4, type=float)
    parser.add_argument('--clip_eps', default=0.2, type=float)
    parser.add_argument('--entropy_coef', default=1e-3, type=float)
    parser.add_argument('--oracle_guiding_coef', default=0.05, type=float)
    parser.add_argument('--oracle_hidden_info_mask', default=1.0, type=float)
    parser.add_argument('--max_grad_norm', default=1.0, type=float)
    parser.add_argument('--ppo_epochs', default=4, type=int)
    parser.add_argument('--mini_batch_size', default=1024, type=int)
    parser.add_argument('--save_every', default=10, type=int)
    parser.add_argument('--seed', default=3407, type=int)
    parser.add_argument('--base_model_path', default='model/saved/discard-model/best.pt', type=str)
    parser.add_argument('--reward_model_path', default='model/saved/reward-model/best.pt', type=str)
    parser.add_argument('--output_dir', default='output/discard-ppo-model/checkpoints', type=str)
    parser.add_argument('--oracle_guiding', action='store_true')
    parser.add_argument('--use_wandb', action='store_true')
    args = parser.parse_args()

    set_seed(args.seed)

    if not os.path.isfile(args.base_model_path):
        raise FileNotFoundError(f'discard model not found: {args.base_model_path}')
    if not os.path.isfile(args.reward_model_path):
        raise FileNotFoundError(f'reward model not found: {args.reward_model_path}')

    wandb_run = None
    if args.use_wandb:
        try:
            import wandb
            wandb_run = wandb.init(project='Mahjong', name='train-discard-ppo')
        except Exception as exc:
            print(f'wandb init failed: {exc}, continue without wandb')

    env = GameEnvironment(
        has_aka=True,
        AI_count=4,
        min_score=0,
        fast=True,
        allow_observe=False,
        train=True,
        oracle_guiding=args.oracle_guiding,
        oracle_hidden_info_mask=args.oracle_hidden_info_mask
    )

    if env.ai_agent is None or env.ai_agent.discard_model is None:
        raise RuntimeError('discard model is not initialized in AI agent')

    device = env.ai_agent.device
    actor = env.ai_agent.discard_model

    base_params = torch.load(args.base_model_path, map_location=device)
    actor.load_state_dict(base_params['state_dict'])
    actor.to(device)
    actor.eval()

    in_channels = int(base_params.get('in_channels', 291))
    num_layers = int(base_params.get('num_layers', 50))

    critic = ValueNet(in_channels=in_channels).to(device)
    critic.eval()

    actor_optimizer = Adam(actor.parameters(), lr=args.actor_lr)
    critic_optimizer = Adam(critic.parameters(), lr=args.critic_lr)

    oracle_actor = None
    oracle_optimizer = None
    oracle_in_channels = None
    if args.oracle_guiding:
        oracle_in_channels = int(env.game.get_feature(0, hidden_info_mask=args.oracle_hidden_info_mask).shape[0])
        oracle_actor = DiscardModel(in_channels=oracle_in_channels, num_layers=num_layers).to(device)
        oracle_actor.eval()
        oracle_optimizer = Adam(oracle_actor.parameters(), lr=args.oracle_lr)

    best_reward = -float('inf')

    for episode in range(1, args.episodes + 1):
        trajectories = asyncio.run(play_one_episode(env))
        states, actions, returns, oracle_states, mean_reward = build_training_batch(trajectories, args.gamma)

        if states is None or len(states) == 0:
            print(f'[Episode {episode}] no valid samples, skip update')
            env.reset()
            continue

        metrics = ppo_update(
            actor,
            critic,
            actor_optimizer,
            critic_optimizer,
            states,
            actions,
            returns,
            device,
            clip_eps=args.clip_eps,
            entropy_coef=args.entropy_coef,
            max_grad_norm=args.max_grad_norm,
            ppo_epochs=args.ppo_epochs,
            mini_batch_size=args.mini_batch_size,
            oracle_actor=oracle_actor,
            oracle_optimizer=oracle_optimizer,
            oracle_states=oracle_states,
            oracle_guiding_coef=args.oracle_guiding_coef if args.oracle_guiding else 0.0
        )

        print(
            f"[Episode {episode}] samples={metrics['samples']} "
            f"mean_reward={mean_reward:.4f} "
            f"actor_loss={metrics['actor_loss']:.4f} "
            f"critic_loss={metrics['critic_loss']:.4f} "
            f"entropy={metrics['entropy']:.4f} "
            f"guiding_loss={metrics['guiding_loss']:.4f} "
            f"oracle_loss={metrics['oracle_loss']:.4f}"
        )

        if wandb_run is not None:
            wandb_run.log({
                'episode': episode,
                'samples': metrics['samples'],
                'mean_reward': mean_reward,
                'actor_loss': metrics['actor_loss'],
                'critic_loss': metrics['critic_loss'],
                'entropy': metrics['entropy'],
                'guiding_loss': metrics['guiding_loss'],
                'oracle_loss': metrics['oracle_loss'],
                'actor_lr': actor_optimizer.param_groups[0]['lr'],
                'critic_lr': critic_optimizer.param_groups[0]['lr'],
                'oracle_lr': oracle_optimizer.param_groups[0]['lr'] if oracle_optimizer is not None else 0.0
            })

        if mean_reward > best_reward:
            best_reward = mean_reward
            save_actor_checkpoint(
                actor,
                episode,
                num_layers,
                in_channels,
                os.path.join(args.output_dir, 'best.pt')
            )
            save_critic_checkpoint(
                critic,
                critic_optimizer,
                episode,
                os.path.join(args.output_dir, 'best_critic.pt')
            )
            if oracle_actor is not None and oracle_in_channels is not None:
                save_actor_checkpoint(
                    oracle_actor,
                    episode,
                    num_layers,
                    oracle_in_channels,
                    os.path.join(args.output_dir, 'best_oracle.pt')
                )

        if episode % args.save_every == 0:
            save_actor_checkpoint(
                actor,
                episode,
                num_layers,
                in_channels,
                os.path.join(args.output_dir, f'episode_{episode}.pt')
            )
            save_critic_checkpoint(
                critic,
                critic_optimizer,
                episode,
                os.path.join(args.output_dir, f'episode_{episode}_critic.pt')
            )
            if oracle_actor is not None and oracle_in_channels is not None:
                save_actor_checkpoint(
                    oracle_actor,
                    episode,
                    num_layers,
                    oracle_in_channels,
                    os.path.join(args.output_dir, f'episode_{episode}_oracle.pt')
                )

        env.reset()

    save_actor_checkpoint(
        actor,
        args.episodes,
        num_layers,
        in_channels,
        os.path.join(args.output_dir, 'final.pt')
    )
    save_critic_checkpoint(
        critic,
        critic_optimizer,
        args.episodes,
        os.path.join(args.output_dir, 'final_critic.pt')
    )
    if oracle_actor is not None and oracle_in_channels is not None:
        save_actor_checkpoint(
            oracle_actor,
            args.episodes,
            num_layers,
            oracle_in_channels,
            os.path.join(args.output_dir, 'final_oracle.pt')
        )

    if wandb_run is not None:
        wandb_run.finish()


if __name__ == '__main__':
    main()
