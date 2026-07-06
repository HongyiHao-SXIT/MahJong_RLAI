import argparse
import asyncio
import os
import random
import sys
from typing import Any, Dict, List, Optional, Sequence, Tuple, cast

import gymnasium as gym
import numpy as np
import torch
import torch.nn as nn
from torch.distributions import Categorical
from torch.optim import Adam

from tianshou.algorithm.modelfree.ppo import PPO
from tianshou.algorithm.modelfree.reinforce import ProbabilisticActorPolicy
from tianshou.algorithm.optim import AdamOptimizerFactory
from tianshou.data import Batch, ReplayBuffer
from tianshou.utils.net.common import AbstractDiscreteActor

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
			nn.Linear(256, 1),
		)

	def forward(self, x):
		return self.layers(x)


class DiscardActorAdapter(AbstractDiscreteActor):
	"""Adapter to expose DiscardModel as a Tianshou discrete actor."""

	def __init__(self, model: nn.Module, action_dim: int = 34):
		super().__init__(action_dim)
		self.model = model

	def get_preprocess_net(self):
		return self.model

	def forward(self, obs, state=None, info=None):
		if isinstance(obs, Batch):
			obs = obs.obs
		obs = torch.as_tensor(obs, dtype=torch.float32, device=next(self.model.parameters()).device)
		logits = self.model(obs)
		return logits, state


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
) -> Tuple[List[np.ndarray], List[int], List[float], List[np.ndarray], List[bool], List[bool], List[Optional[np.ndarray]]]:
	valid_items = [item for item in player_records if len(item) >= 3 and np.isscalar(item[-1])]
	if not valid_items:
		return [], [], [], [], [], [], []

	obs_list: List[np.ndarray] = []
	act_list: List[int] = []
	rew_list: List[float] = []
	obs_next_list: List[np.ndarray] = []
	terminated_list: List[bool] = []
	truncated_list: List[bool] = []
	oracle_list: List[Optional[np.ndarray]] = []

	for idx, item in enumerate(valid_items):
		state = np.asarray(item[0], dtype=np.float32)
		action = int(item[1])
		reward = float(item[-1])
		oracle_state = item[2] if len(item) >= 4 else None

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
		oracle_list.append(np.asarray(oracle_state, dtype=np.float32) if oracle_state is not None else None)

	return obs_list, act_list, rew_list, obs_next_list, terminated_list, truncated_list, oracle_list


def build_training_batch(collected_data):
	obs_all: List[np.ndarray] = []
	act_all: List[int] = []
	rew_all: List[float] = []
	obs_next_all: List[np.ndarray] = []
	terminated_all: List[bool] = []
	truncated_all: List[bool] = []
	oracle_states_all: List[Optional[np.ndarray]] = []

	for player_records in collected_data.values():
		obs, act, rew, obs_next, terminated, truncated, oracle_states = _extract_player_rollout(player_records)
		obs_all.extend(obs)
		act_all.extend(act)
		rew_all.extend(rew)
		obs_next_all.extend(obs_next)
		terminated_all.extend(terminated)
		truncated_all.extend(truncated)
		oracle_states_all.extend(oracle_states)

	if not obs_all:
		return None

	has_oracle = len(oracle_states_all) > 0 and all(item is not None for item in oracle_states_all)
	oracle_np = np.asarray(oracle_states_all, dtype=np.float32) if has_oracle else None

	return {
		'obs': np.asarray(obs_all, dtype=np.float32),
		'act': np.asarray(act_all, dtype=np.int64),
		'rew': np.asarray(rew_all, dtype=np.float32),
		'obs_next': np.asarray(obs_next_all, dtype=np.float32),
		'terminated': np.asarray(terminated_all, dtype=np.bool_),
		'truncated': np.asarray(truncated_all, dtype=np.bool_),
		'oracle_obs': oracle_np,
		'mean_reward': float(np.mean(rew_all)) if rew_all else 0.0,
	}


def make_replay_buffer(batch_data: Dict[str, np.ndarray]) -> ReplayBuffer:
	n = int(batch_data['obs'].shape[0])
	buffer = ReplayBuffer(size=max(1, n))
	rollout_batch = Batch(
		obs=batch_data['obs'],
		act=batch_data['act'],
		rew=batch_data['rew'],
		terminated=batch_data['terminated'],
		truncated=batch_data['truncated'],
		obs_next=batch_data['obs_next'],
		info=Batch(),
	)
	buffer.add(cast(Any, rollout_batch))
	return buffer


def build_tianshou_ppo(
	actor_model: nn.Module,
	critic_model: nn.Module,
	actor_lr: float,
	gamma: float,
	clip_eps: float,
	entropy_coef: float,
	max_grad_norm: float,
	gae_lambda: float,
	obs_shape: Tuple[int, ...],
	action_dim: int,
) -> PPO:
	actor_model.train()
	critic_model.train()

	actor = DiscardActorAdapter(actor_model, action_dim=action_dim)
	policy = ProbabilisticActorPolicy(
		actor=actor,
		dist_fn=lambda logits: Categorical(logits=logits),
		deterministic_eval=False,
		action_space=gym.spaces.Discrete(action_dim),
		observation_space=gym.spaces.Box(low=-np.inf, high=np.inf, shape=obs_shape, dtype=np.float32),
		action_scaling=False,
	)

	ppo = PPO(
		policy=policy,
		critic=critic_model,
		optim=AdamOptimizerFactory(lr=actor_lr),
		eps_clip=clip_eps,
		vf_coef=0.5,
		ent_coef=entropy_coef,
		max_grad_norm=max_grad_norm,
		gae_lambda=gae_lambda,
		max_batchsize=2048,
		gamma=gamma,
		advantage_normalization=True,
	)
	return ppo


def run_ppo_update(ppo: PPO, batch_data: Dict[str, np.ndarray], repeat: int, mini_batch_size: int) -> Dict[str, float]:
	buffer = make_replay_buffer(batch_data)
	stats = ppo.update(buffer=buffer, batch_size=mini_batch_size, repeat=repeat)
	loss_stats = stats.get_loss_stats_dict()
	return {
		'actor_loss': float(loss_stats.get('actor_loss', 0.0)),
		'critic_loss': float(loss_stats.get('vf_loss', 0.0)),
		'entropy': float(loss_stats.get('ent_loss', 0.0)),
		'samples': int(batch_data['obs'].shape[0]),
	}


def apply_oracle_guiding(
	actor_model: nn.Module,
	oracle_model: nn.Module,
	actor_optimizer: Adam,
	obs: np.ndarray,
	oracle_obs: np.ndarray,
	coef: float,
	mini_batch_size: int,
) -> float:
	if coef <= 0 or oracle_obs is None:
		return 0.0

	device = next(actor_model.parameters()).device
	obs_tensor = torch.from_numpy(obs).to(device)
	oracle_tensor = torch.from_numpy(oracle_obs).to(device)
	n = obs_tensor.shape[0]
	mini_batch_size = min(max(1, mini_batch_size), n)

	actor_model.train()
	oracle_model.eval()

	losses: List[float] = []
	perm = torch.randperm(n, device=device)
	for start in range(0, n, mini_batch_size):
		idx = perm[start: start + mini_batch_size]
		mb_obs = obs_tensor[idx]
		mb_oracle_obs = oracle_tensor[idx]

		logits = actor_model(mb_obs)
		log_probs = torch.log_softmax(logits, dim=1)
		with torch.no_grad():
			teacher_prob = torch.softmax(oracle_model(mb_oracle_obs), dim=1)

		guiding_loss = torch.nn.functional.kl_div(log_probs, teacher_prob, reduction='batchmean')
		loss = coef * guiding_loss

		actor_optimizer.zero_grad()
		loss.backward()
		torch.nn.utils.clip_grad_norm_(actor_model.parameters(), 1.0)
		actor_optimizer.step()
		losses.append(float(guiding_loss.item()))

	return float(np.mean(losses)) if losses else 0.0


def save_actor_checkpoint(actor, episode: int, num_layers: int, in_channels: int, output_path: str):
	os.makedirs(os.path.dirname(output_path), exist_ok=True)
	torch.save(
		{
			'state_dict': actor.state_dict(),
			'num_layers': num_layers,
			'in_channels': in_channels,
			'episode': episode,
		},
		output_path,
	)


def save_critic_checkpoint(critic, episode: int, output_path: str):
	os.makedirs(os.path.dirname(output_path), exist_ok=True)
	torch.save(
		{
			'state_dict': critic.state_dict(),
			'episode': episode,
		},
		output_path,
	)


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument('--episodes', '-e', default=200, type=int)
	parser.add_argument('--gamma', default=0.99, type=float)
	parser.add_argument('--gae_lambda', default=0.95, type=float)
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

			wandb_run = wandb.init(project='Mahjong', name='train-discard-ppo-tianshou')
		except Exception as exc:
			print(f'wandb init failed: {exc}, continue without wandb')

	if abs(args.actor_lr - args.critic_lr) > 1e-12:
		print(
			f'[warning] tianshou PPO(2.0) uses a unified optimizer for actor/critic; '
			f'critic_lr={args.critic_lr} will be ignored, using actor_lr={args.actor_lr}.'
		)

	env = GameEnvironment(
		has_aka=True,
		AI_count=4,
		min_score=0,
		fast=True,
		allow_observe=False,
		train=True,
		oracle_guiding=args.oracle_guiding,
		oracle_hidden_info_mask=args.oracle_hidden_info_mask,
	)

	if env.ai_agent is None or env.ai_agent.discard_model is None:
		raise RuntimeError('discard model is not initialized in AI agent')

	device = env.ai_agent.device
	actor_model = env.ai_agent.discard_model

	base_params = torch.load(args.base_model_path, map_location=device)
	actor_model.load_state_dict(base_params['state_dict'])
	actor_model.to(device)
	actor_model.eval()

	in_channels = int(base_params.get('in_channels', 291))
	num_layers = int(base_params.get('num_layers', 50))

	critic_model = ValueNet(in_channels=in_channels).to(device)

	ppo = build_tianshou_ppo(
		actor_model=actor_model,
		critic_model=critic_model,
		actor_lr=args.actor_lr,
		gamma=args.gamma,
		clip_eps=args.clip_eps,
		entropy_coef=args.entropy_coef,
		max_grad_norm=args.max_grad_norm,
		gae_lambda=args.gae_lambda,
		obs_shape=(in_channels, 34),
		action_dim=34,
	)

	oracle_model = None
	oracle_ppo = None
	oracle_guiding_optimizer = None
	oracle_in_channels = None
	if args.oracle_guiding:
		oracle_in_channels = int(env.game.get_feature(0, hidden_info_mask=args.oracle_hidden_info_mask).shape[0])
		oracle_model = DiscardModel(in_channels=oracle_in_channels, num_layers=num_layers).to(device)
		oracle_critic = ValueNet(in_channels=oracle_in_channels).to(device)
		oracle_ppo = build_tianshou_ppo(
			actor_model=oracle_model,
			critic_model=oracle_critic,
			actor_lr=args.oracle_lr,
			gamma=args.gamma,
			clip_eps=args.clip_eps,
			entropy_coef=args.entropy_coef,
			max_grad_norm=args.max_grad_norm,
			gae_lambda=args.gae_lambda,
			obs_shape=(oracle_in_channels, 34),
			action_dim=34,
		)
		oracle_guiding_optimizer = Adam(actor_model.parameters(), lr=args.actor_lr)

	best_reward = -float('inf')

	for episode in range(1, args.episodes + 1):
		trajectories = asyncio.run(play_one_episode(env))
		batch_data = build_training_batch(trajectories)

		if batch_data is None or batch_data['obs'].shape[0] == 0:
			print(f'[Episode {episode}] no valid samples, skip update')
			env.reset()
			continue

		metrics = run_ppo_update(
			ppo=ppo,
			batch_data=batch_data,
			repeat=args.ppo_epochs,
			mini_batch_size=args.mini_batch_size,
		)

		oracle_loss = 0.0
		guiding_loss = 0.0
		if (
			oracle_ppo is not None
			and batch_data['oracle_obs'] is not None
			and oracle_model is not None
			and oracle_guiding_optimizer is not None
		):
			oracle_batch = dict(batch_data)
			oracle_batch['obs'] = batch_data['oracle_obs']
			oracle_batch['obs_next'] = np.concatenate(
				[
					batch_data['oracle_obs'][1:],
					batch_data['oracle_obs'][-1:]
				],
				axis=0,
			)
			oracle_metrics = run_ppo_update(
				ppo=oracle_ppo,
				batch_data=oracle_batch,
				repeat=args.ppo_epochs,
				mini_batch_size=args.mini_batch_size,
			)
			oracle_loss = float(oracle_metrics['actor_loss'])

			guiding_loss = apply_oracle_guiding(
				actor_model=actor_model,
				oracle_model=oracle_model,
				actor_optimizer=oracle_guiding_optimizer,
				obs=batch_data['obs'],
				oracle_obs=batch_data['oracle_obs'],
				coef=args.oracle_guiding_coef,
				mini_batch_size=args.mini_batch_size,
			)

		mean_reward = float(batch_data['mean_reward'])
		print(
			f"[Episode {episode}] samples={metrics['samples']} "
			f'mean_reward={mean_reward:.4f} '
			f"actor_loss={metrics['actor_loss']:.4f} "
			f"critic_loss={metrics['critic_loss']:.4f} "
			f"entropy={metrics['entropy']:.4f} "
			f'guiding_loss={guiding_loss:.4f} '
			f'oracle_loss={oracle_loss:.4f}'
		)

		if wandb_run is not None:
			wandb_run.log(
				{
					'episode': episode,
					'samples': metrics['samples'],
					'mean_reward': mean_reward,
					'actor_loss': metrics['actor_loss'],
					'critic_loss': metrics['critic_loss'],
					'entropy': metrics['entropy'],
					'guiding_loss': guiding_loss,
					'oracle_loss': oracle_loss,
					'actor_lr': args.actor_lr,
					'critic_lr': args.critic_lr,
					'oracle_lr': args.oracle_lr if args.oracle_guiding else 0.0,
				}
			)

		if mean_reward > best_reward:
			best_reward = mean_reward
			save_actor_checkpoint(
				actor_model,
				episode,
				num_layers,
				in_channels,
				os.path.join(args.output_dir, 'best.pt'),
			)
			save_critic_checkpoint(
				critic_model,
				episode,
				os.path.join(args.output_dir, 'best_critic.pt'),
			)
			if oracle_model is not None and oracle_in_channels is not None:
				save_actor_checkpoint(
					oracle_model,
					episode,
					num_layers,
					oracle_in_channels,
					os.path.join(args.output_dir, 'best_oracle.pt'),
				)

		if episode % args.save_every == 0:
			save_actor_checkpoint(
				actor_model,
				episode,
				num_layers,
				in_channels,
				os.path.join(args.output_dir, f'episode_{episode}.pt'),
			)
			save_critic_checkpoint(
				critic_model,
				episode,
				os.path.join(args.output_dir, f'episode_{episode}_critic.pt'),
			)
			if oracle_model is not None and oracle_in_channels is not None:
				save_actor_checkpoint(
					oracle_model,
					episode,
					num_layers,
					oracle_in_channels,
					os.path.join(args.output_dir, f'episode_{episode}_oracle.pt'),
				)

		env.reset()

	save_actor_checkpoint(
		actor_model,
		args.episodes,
		num_layers,
		in_channels,
		os.path.join(args.output_dir, 'final.pt'),
	)
	save_critic_checkpoint(
		critic_model,
		args.episodes,
		os.path.join(args.output_dir, 'final_critic.pt'),
	)
	if oracle_model is not None and oracle_in_channels is not None:
		save_actor_checkpoint(
			oracle_model,
			args.episodes,
			num_layers,
			oracle_in_channels,
			os.path.join(args.output_dir, 'final_oracle.pt'),
		)

	if wandb_run is not None:
		wandb_run.finish()


if __name__ == '__main__':
	main()
    