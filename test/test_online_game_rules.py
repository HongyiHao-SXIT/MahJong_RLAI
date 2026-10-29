from types import SimpleNamespace

import pytest

from online_game.server import GameEnvironment


def _environment(round_number, dealer=0, min_score=0):
    env = GameEnvironment.__new__(GameEnvironment)
    env.round = round_number
    env.min_score = min_score
    env.game = SimpleNamespace(oya=dealer)
    return env


def test_non_dealer_win_before_south_four_does_not_mark_game_final():
    env = _environment(round_number=6)

    assert not env._game_ends_after_win([310, 260, 240, 190], winner=1)


def test_dealer_win_marks_game_final_only_when_dealer_leads_at_30k():
    env = _environment(round_number=7, dealer=0)

    assert env._game_ends_after_win([310, 300, 240, 190], winner=0)
    assert not env._game_ends_after_win([290, 280, 240, 190], winner=0)
    assert not env._game_ends_after_win([300, 310, 240, 190], winner=0)


def test_non_dealer_win_can_end_after_round_advances():
    env = _environment(round_number=7, dealer=0)

    assert env._game_ends_after_win([310, 320, 240, 190], winner=1)
    assert not env._game_ends_after_win([290, 280, 240, 190], winner=1)


def test_game_end_threshold_and_min_score_are_respected():
    env = _environment(round_number=11, dealer=0)
    assert env._game_ends_after_win([250, 250, 250, 250], winner=1)

    env = _environment(round_number=2, min_score=10000)
    assert env._game_ends_after_win([250, 250, 250, 90], winner=1)


def test_training_requires_reward_checkpoint(tmp_path):
    with pytest.raises(FileNotFoundError, match='reward predictor checkpoint'):
        GameEnvironment(
            AI_count=0,
            train=True,
            reward_model_path=tmp_path / 'missing.pt',
        )


def test_agari_event_includes_ura_dora_indicator():
    env = GameEnvironment.__new__(GameEnvironment)
    env.game = SimpleNamespace(ura_dora_indicator=[12])
    sent = []
    env.send_multiply = sent.append
    actions = [{'who': 0, 'from_who': 1}]

    env.send_agari(actions)

    assert sent == [{
        'event': 'agari',
        'action': actions,
        'ura_dora_indicator': [12],
    }]
