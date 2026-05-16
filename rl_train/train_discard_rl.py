import argparse
import asyncio
import os
import random
import sys
from typing import Dict, List, Tuple

import numpy as np
import torch
from torch.optim import Adam

sys.path.append(os.path.dirname(os.path.abspath(os.path.dirname(__file__))))
from online_game.server import GameEnvironment


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
                if len(item) == 3:
                    continue
                features = torch.stack(env.reward_features[i])[None].float()
                reward = env.reward(features, len(env.reward_features[i]) - 1)
                item.append(float(reward))

        if game_over:
            env.game_start = False

    return env.collected_data


def build_training_batch(collected_data, gamma: float) -> Tuple[np.ndarray, np.ndarray, np.ndarray, float]:
    states: List[np.ndarray] = []
    actions: List[int] = []
    returns_all: List[float] = []
    episode_rewards: List[float] = []

    for player_records in collected_data.values():
        rewards = [float(item[2]) for item in player_records if len(item) >= 3]
        if not rewards:
            continue
        player_returns = discounted_returns(rewards, gamma)

        idx = 0
        for item in player_records:
            if len(item) < 3:
                continue
            state, action, reward = item
            states.append(state)
            actions.append(int(action))
            returns_all.append(float(player_returns[idx]))
            episode_rewards.append(float(reward))
            idx += 1

    if not states:
        return np.empty((0, 291, 34), dtype=np.float32), np.empty((0,), dtype=np.int64), np.empty((0,), dtype=np.float32), 0.0

    mean_reward = float(np.mean(episode_rewards)) if episode_rewards else 0.0
    return (
        np.asarray(states, dtype=np.float32),
        np.asarray(actions, dtype=np.int64),
        np.asarray(returns_all, dtype=np.float32),
        mean_reward
    )


def optimize_policy(model, optimizer, states, actions, returns, device, entropy_coef: float, max_grad_norm: float):
    features = torch.from_numpy(states).to(device)
    labels = torch.from_numpy(actions).to(device)
    rets = torch.from_numpy(returns).to(device)

    if rets.numel() > 1:
        rets = (rets - rets.mean()) / (rets.std(unbiased=False) + 1e-6)

    model.train()
    logits = model(features)
    log_probs = torch.log_softmax(logits, dim=1)
    probs = torch.softmax(logits, dim=1)
    selected_log_probs = log_probs.gather(1, labels.unsqueeze(1)).squeeze(1)

    entropy = -(probs * log_probs).sum(dim=1).mean()
    policy_loss = -(selected_log_probs * rets).mean()
    loss = policy_loss - entropy_coef * entropy

    optimizer.zero_grad()
    loss.backward()
    torch.nn.utils.clip_grad_norm_(model.parameters(), max_grad_norm)
    optimizer.step()
    model.eval()

    return float(loss.item()), float(policy_loss.item()), float(entropy.item())


def save_checkpoint(model, optimizer, episode: int, num_layers: int, in_channels: int, output_path: str):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    torch.save(
        {
            "state_dict": model.state_dict(),
            "num_layers": num_layers,
            "in_channels": in_channels,
            "episode": episode,
            "optimizer_state": optimizer.state_dict()
        },
        output_path
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--episodes', '-e', default=200, type=int)
    parser.add_argument('--gamma', default=0.99, type=float)
    parser.add_argument('--lr', default=1e-5, type=float)
    parser.add_argument('--entropy_coef', default=1e-3, type=float)
    parser.add_argument('--max_grad_norm', default=1.0, type=float)
    parser.add_argument('--save_every', default=10, type=int)
    parser.add_argument('--seed', default=3407, type=int)
    parser.add_argument('--base_model_path', default='model/saved/discard-model/best.pt', type=str)
    parser.add_argument('--reward_model_path', default='model/saved/reward-model/best.pt', type=str)
    parser.add_argument('--output_dir', default='output/discard-rl-model/checkpoints', type=str)
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
            wandb_run = wandb.init(project='Mahjong', name='train-discard-rl')
        except Exception as exc:
            print(f'wandb init failed: {exc}, continue without wandb')

    env = GameEnvironment(
        has_aka=True,
        AI_count=4,
        min_score=0,
        fast=True,
        allow_observe=False,
        train=True
    )

    if env.ai_agent is None or env.ai_agent.discard_model is None:
        raise RuntimeError('discard model is not initialized in AI agent')

    device = env.ai_agent.device
    model = env.ai_agent.discard_model

    base_params = torch.load(args.base_model_path, map_location=device)
    model.load_state_dict(base_params['state_dict'])
    model.to(device)
    model.eval()

    num_layers = int(base_params.get('num_layers', 50))
    in_channels = int(base_params.get('in_channels', 291))
    optimizer = Adam(model.parameters(), lr=args.lr)

    best_reward = -float('inf')
    for episode in range(1, args.episodes + 1):
        trajectories = asyncio.run(play_one_episode(env))
        states, actions, returns, mean_reward = build_training_batch(trajectories, args.gamma)

        if len(states) == 0:
            print(f'[Episode {episode}] no valid samples, skip update')
            env.reset()
            continue

        loss, policy_loss, entropy = optimize_policy(
            model,
            optimizer,
            states,
            actions,
            returns,
            device,
            args.entropy_coef,
            args.max_grad_norm
        )

        print(
            f'[Episode {episode}] samples={len(states)} '
            f'mean_reward={mean_reward:.4f} '
            f'loss={loss:.4f} policy_loss={policy_loss:.4f} entropy={entropy:.4f}'
        )

        if wandb_run is not None:
            wandb_run.log({
                'episode': episode,
                'samples': len(states),
                'mean_reward': mean_reward,
                'loss': loss,
                'policy_loss': policy_loss,
                'entropy': entropy,
                'lr': optimizer.param_groups[0]['lr']
            })

        if mean_reward > best_reward:
            best_reward = mean_reward
            save_checkpoint(
                model,
                optimizer,
                episode,
                num_layers,
                in_channels,
                os.path.join(args.output_dir, 'best.pt')
            )

        if episode % args.save_every == 0:
            save_checkpoint(
                model,
                optimizer,
                episode,
                num_layers,
                in_channels,
                os.path.join(args.output_dir, f'episode_{episode}.pt')
            )

        env.reset()

    save_checkpoint(
        model,
        optimizer,
        args.episodes,
        num_layers,
        in_channels,
        os.path.join(args.output_dir, 'final.pt')
    )

    if wandb_run is not None:
        wandb_run.finish()


if __name__ == '__main__':
    main()
