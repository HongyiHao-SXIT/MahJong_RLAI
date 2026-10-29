import torch

from mahjong.agent import AiAgent
from mahjong.game import BASE_FEATURE_CHANNELS, FEATURE_CHANNELS, MahjongGame
from model.models import LegacyBinaryDecisionModel, RiichiModel


def test_legacy_binary_checkpoint_loads_and_predicts(tmp_path):
    legacy = LegacyBinaryDecisionModel(in_channels=BASE_FEATURE_CHANNELS, num_layers=0)
    checkpoint = tmp_path / 'legacy-riichi.pt'
    torch.save({
        'state_dict': legacy.state_dict(),
        'num_layers': 0,
        'in_channels': BASE_FEATURE_CHANNELS,
        'threshold': 0.5,
    }, checkpoint)

    agent = AiAgent.__new__(AiAgent)
    agent.device = torch.device('cpu')
    agent._load_model(checkpoint, RiichiModel, 'riichi_model', has_threshold=True)

    state = torch.randn(1, BASE_FEATURE_CHANNELS, 34)
    assert isinstance(agent.riichi_model, LegacyBinaryDecisionModel)
    assert agent.riichi_model.prob(state).shape == (1,)
    assert 0.0 <= agent.riichi_model.prob(state).item() <= 1.0
    assert agent.riichi_threshold == 0.5


def test_legacy_checkpoint_ignores_only_lookahead_channels():
    legacy = LegacyBinaryDecisionModel(in_channels=BASE_FEATURE_CHANNELS, num_layers=0)
    state = torch.randn(FEATURE_CHANNELS, 34).numpy()

    prepared = AiAgent.prepare_state(state, legacy)

    assert prepared.shape == (BASE_FEATURE_CHANNELS, 34)
    assert (prepared == state[:BASE_FEATURE_CHANNELS]).all()


def test_current_and_legacy_game_feature_shapes():
    game = MahjongGame()

    assert game.get_feature(0).shape == (FEATURE_CHANNELS, 34)
    assert game.get_feature(0, use_lookahead=False).shape == (BASE_FEATURE_CHANNELS, 34)
