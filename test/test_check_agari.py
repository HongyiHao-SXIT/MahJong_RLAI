from collections import Counter

from mahjong.check_agari import is_agari, check_machi, check_riichi, machi
from mahjong.utils import AutoCleanCounter


def _base_winning_counter():
    # Seven pairs: 11m22m33m11p22p33p東東
    return AutoCleanCounter({0: 2, 1: 2, 2: 2, 9: 2, 10: 2, 11: 2, 27: 2})


def test_is_agari_for_basic_hand():
    counter = _base_winning_counter()
    result = is_agari(counter)
    assert result is not None


def test_check_machi_and_machi_result_for_tenpai():
    counter = _base_winning_counter().copy()
    counter[0] -= 1  # 13 tiles, validated tenpai shape for this table

    assert check_machi(counter) is True
    assert machi(counter) == {0, 3}


def test_check_riichi_reports_discard_candidates():
    # Build a 14-tile hand where discarding 5m or 1m reaches tenpai.
    counter = AutoCleanCounter({0: 1, 1: 2, 2: 2, 9: 2, 10: 2, 11: 2, 27: 2})
    counter[5] += 1

    candidates = check_riichi(counter, return_riichi_hai=True)
    assert isinstance(candidates, list)
    assert set(candidates) == {0, 5}
