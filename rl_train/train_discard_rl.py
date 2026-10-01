import argparse
import asyncio
import os
import random
import sys
from typing import Any, Dict, List, Sequence, Tuple, cast

import gymnasium as gym
import numpy as np
import torch
from torch.distributions import Categorical

from tianshou.algorithm.modelfree.reinforce import (
    DiscountedReturnComputation,
    LossSequenceTrainingStats,
    ProbabilisticActorPolicy,
    Reinforce,
)
from tianshou.algorithm.algorithm_base import OnPolicyAlgorithm
from tianshou.algorithm.optim import AdamOptimizerFactory, OptimizerFactory
from tianshou.data import Batch, ReplayBuffer, to_torch, to_torch_as
from tianshou.data.stats import SequenceSummaryStats
from tianshou.data.types import BatchWithReturnsProtocol, RolloutBatchProtocol
from tianshou.utils.net.common import AbstractDiscreteActor

sys.path.append(os.path.dirname(os.path.abspath(os.path.dirname(__file__))))
from online_game.server import GameEnvironment


class DiscardActorAdapter(AbstractDiscreteActor):
    """Expose the existing discard model as a Tianshou discrete actor."""

    def __init__(self, model: torch.nn.Module, action_dim: int = 34):
        super().__init__(action_dim)
        self.model = model

    def get_preprocess_net(self):
        return self.model

    def forward(self, obs, state=None, info=None):
        if isinstance(obs, Batch):
            obs = obs.obs
        device = next(self.model.parameters()).device
        obs = torch.as_tensor(obs, dtype=torch.float32, device=device)
        logits = self.model(obs)
        return logits, state


class EntropyReinforce(Reinforce):
    """REINFORCE with entropy bonus and optional grad clipping to match previous script behavior."""

    def __init__(
        self,
        *,
        policy: ProbabilisticActorPolicy,
        gamma: float,
        return_standardization: bool,
        optim: OptimizerFactory,
        entropy_coef: float,
        max_grad_norm: float,
    ) -> None:
        OnPolicyAlgorithm.__init__(self, policy=policy)
        self.discounted_return_computation = DiscountedReturnComputation(
            gamma=gamma,
            return_standardization=return_standardization,
        )
        self.optim = self._create_optimizer(self.policy, optim, max_grad_norm=max_grad_norm)
        self.entropy_coef = float(entropy_coef)
        self.last_policy_loss = 0.0
        self.last_entropy = 0.0

    def _preprocess_batch(
        self,
        batch: RolloutBatchProtocol,
        buffer: ReplayBuffer,
        indices: np.ndarray,
    ) -> BatchWithReturnsProtocol:
        return self.discounted_return_computation.add_discounted_returns(batch, buffer, indices)

    def _update_with_batch(
        self,
        batch: BatchWithReturnsProtocol,
        batch_size: int | None,
        repeat: int,
    ) -> LossSequenceTrainingStats:
        losses: List[float] = []
        policy_losses: List[float] = []
        entropies: List[float] = []

        split_batch_size = batch_size or -1
        for _ in range(repeat):
            for minibatch in batch.split(split_batch_size, merge_last=True):
                result = self.policy(minibatch)
                dist = result.dist
                act = to_torch_as(minibatch.act, result.act)
                ret = to_torch(minibatch.returns, torch.float, result.act.device)

                if ret.numel() > 1:
                    ret = (ret - ret.mean()) / (ret.std(unbiased=False) + 1e-6)

                log_prob = dist.log_prob(act).reshape(len(ret), -1).transpose(0, 1)
                policy_loss = -(log_prob * ret).mean()
                entropy = dist.entropy().mean()
                loss = policy_loss - self.entropy_coef * entropy

                self.optim.step(loss)
                losses.append(float(loss.item()))
                policy_losses.append(float(policy_loss.item()))
                entropies.append(float(entropy.item()))

        self.last_policy_loss = float(np.mean(policy_losses)) if policy_losses else 0.0
        self.last_entropy = float(np.mean(entropies)) if entropies else 0.0
        return LossSequenceTrainingStats(loss=SequenceSummaryStats.from_sequence(losses))


def set_seed(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


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


def _extract_player_rollout(
    player_records: Sequence[List],
) -> Tuple[List[np.ndarray], List[int], List[float], List[np.ndarray], List[bool], List[bool]]:
    valid_items = [item for item in player_records if len(item) >= 3 and np.isscalar(item[-1])]
    if not valid_items:
        return [], [], [], [], [], []

    obs_list: List[np.ndarray] = []
    act_list: List[int] = []
    rew_list: List[float] = []
    obs_next_list: List[np.ndarray] = []
    terminated_list: List[bool] = []
    truncated_list: List[bool] = []

    for idx, item in enumerate(valid_items):
        state = np.asarray(item[0], dtype=np.float32)
        action = int(item[1])
        reward = float(item[-1])

        if idx + 1 < len(valid_items):
            next_state = np.asarray(valid_items[idx + 1][0], dtype=np.float32)
            terminated = False
        else:
            next_state = state
            terminated = True

        obs_list.append(state)
        act_list.append(action)
        rew_list.append(reward)
        obs_next_list.append(next_state)
        terminated_list.append(terminated)
        truncated_list.append(False)

    return obs_list, act_list, rew_list, obs_next_list, terminated_list, truncated_list


def build_training_batch(collected_data) -> Dict[str, np.ndarray | float] | None:
    obs_all: List[np.ndarray] = []
    act_all: List[int] = []
    rew_all: List[float] = []
    obs_next_all: List[np.ndarray] = []
    terminated_all: List[bool] = []
    truncated_all: List[bool] = []

    for player_records in collected_data.values():
        obs, act, rew, obs_next, terminated, truncated = _extract_player_rollout(player_records)
        obs_all.extend(obs)
        act_all.extend(act)
        rew_all.extend(rew)
        obs_next_all.extend(obs_next)
        terminated_all.extend(terminated)
        truncated_all.extend(truncated)

    if not obs_all:
        return None

    return {
        'obs': np.asarray(obs_all, dtype=np.float32),
        'act': np.asarray(act_all, dtype=np.int64),
        'rew': np.asarray(rew_all, dtype=np.float32),
        'obs_next': np.asarray(obs_next_all, dtype=np.float32),
        'terminated': np.asarray(terminated_all, dtype=np.bool_),
        'truncated': np.asarray(truncated_all, dtype=np.bool_),
        'mean_reward': float(np.mean(rew_all)) if rew_all else 0.0,
    }


def make_replay_buffer(batch_data: Dict[str, np.ndarray | float]) -> ReplayBuffer:
    obs = cast(np.ndarray, batch_data['obs'])
    n = int(obs.shape[0])
    buffer = ReplayBuffer(size=max(1, n))
    rollout_batch = Batch(
        obs=obs,
        act=cast(np.ndarray, batch_data['act']),
        rew=cast(np.ndarray, batch_data['rew']),
        obs_next=cast(np.ndarray, batch_data['obs_next']),
        terminated=cast(np.ndarray, batch_data['terminated']),
        truncated=cast(np.ndarray, batch_data['truncated']),
        info=Batch(),
    )
    buffer.add(cast(Any, rollout_batch))
    return buffer


def build_reinforce(
    model: torch.nn.Module,
    lr: float,
    gamma: float,
    entropy_coef: float,
    max_grad_norm: float,
    in_channels: int,
) -> EntropyReinforce:
    actor = DiscardActorAdapter(model=model, action_dim=34)
    policy = ProbabilisticActorPolicy(
        actor=actor,
        dist_fn=lambda logits: Categorical(logits=logits),
        deterministic_eval=False,
        action_space=gym.spaces.Discrete(34),
        observation_space=gym.spaces.Box(low=-np.inf, high=np.inf, shape=(in_channels, 34), dtype=np.float32),
        action_scaling=False,
    )

    return EntropyReinforce(
        policy=policy,
        gamma=gamma,
        return_standardization=False,
        optim=AdamOptimizerFactory(lr=lr),
        entropy_coef=entropy_coef,
        max_grad_norm=max_grad_norm,
    )


def optimize_policy(
    algo: EntropyReinforce,
    batch_data: Dict[str, np.ndarray | float],
    mini_batch_size: int | None,
    repeat: int,
) -> Tuple[float, float, float]:
    buffer = make_replay_buffer(batch_data)
    stats = algo.update(buffer=buffer, batch_size=mini_batch_size, repeat=repeat)
    loss = float(stats.get_loss_stats_dict().get('loss', 0.0))
    return loss, float(algo.last_policy_loss), float(algo.last_entropy)


def save_checkpoint(model, optimizer, episode: int, num_layers: int, in_channels: int, output_path: str):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    torch.save(
        {
            'state_dict': model.state_dict(),
            'num_layers': num_layers,
            'in_channels': in_channels,
            'episode': episode,
            'optimizer_state': optimizer.state_dict(),
        },
        output_path,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--episodes', '-e', default=200, type=int)
    parser.add_argument('--gamma', default=0.99, type=float)
    parser.add_argument('--lr', default=1e-5, type=float)
    parser.add_argument('--entropy_coef', default=1e-3, type=float)
    parser.add_argument('--max_grad_norm', default=1.0, type=float)
    parser.add_argument('--repeat', default=1, type=int)
    parser.add_argument('--mini_batch_size', default=0, type=int)
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
        train=True,
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

    algo = build_reinforce(
        model=model,
        lr=args.lr,
        gamma=args.gamma,
        entropy_coef=args.entropy_coef,
        max_grad_norm=args.max_grad_norm,
        in_channels=in_channels,
    )

    best_reward = -float('inf')
    for episode in range(1, args.episodes + 1):
        trajectories = asyncio.run(play_one_episode(env))
        batch_data = build_training_batch(trajectories)

        if batch_data is None:
            print(f'[Episode {episode}] no valid samples, skip update')
            env.reset()
            continue

        mini_batch_size = args.mini_batch_size if args.mini_batch_size > 0 else None
        loss, policy_loss, entropy = optimize_policy(
            algo=algo,
            batch_data=batch_data,
            mini_batch_size=mini_batch_size,
            repeat=args.repeat,
        )

        samples = int(cast(np.ndarray, batch_data['obs']).shape[0])
        mean_reward = float(cast(float, batch_data['mean_reward']))
        print(
            f'[Episode {episode}] samples={samples} '
            f'mean_reward={mean_reward:.4f} '
            f'loss={loss:.4f} policy_loss={policy_loss:.4f} entropy={entropy:.4f}'
        )

        if wandb_run is not None:
            wandb_run.log(
                {
                    'episode': episode,
                    'samples': samples,
                    'mean_reward': mean_reward,
                    'loss': loss,
                    'policy_loss': policy_loss,
                    'entropy': entropy,
                    'lr': args.lr,
                }
            )

        if mean_reward > best_reward:
            best_reward = mean_reward
            save_checkpoint(
                model,
                algo.optim,
                episode,
                num_layers,
                in_channels,
                os.path.join(args.output_dir, 'best.pt'),
            )

        if episode % args.save_every == 0:
            save_checkpoint(
                model,
                algo.optim,
                episode,
                num_layers,
                in_channels,
                os.path.join(args.output_dir, f'episode_{episode}.pt'),
            )

        env.reset()

    save_checkpoint(
        model,
        algo.optim,
        args.episodes,
        num_layers,
        in_channels,
        os.path.join(args.output_dir, 'final.pt'),
    )

    if wandb_run is not None:
        wandb_run.finish()


if __name__ == '__main__':
    main()