from collections import Counter

from mahjong.utils import (
    AutoCleanCounter,
    encode_shunzi,
    encode_kezi,
    encode_kanzi,
    parse_meld,
    get_dora,
)


def test_autoclean_counter_removes_zero_key():
    c = AutoCleanCounter({3: 2})
    c[3] = 0
    assert 3 not in c


def test_shunzi_encode_parse_roundtrip():
    tiles = [0, 4, 8]  # 1m,2m,3m
    kui_tile = 4
    code = encode_shunzi(tiles, kui_tile)
    furo_type, parsed_tiles, add_tile, _where = parse_meld(code)

    assert furo_type == 0
    assert set(parsed_tiles) == set(tiles)
    assert add_tile == kui_tile


def test_kezi_encode_parse_roundtrip():
    tiles = [0, 1, 2]  # 1m triplet
    kui_tile = 1
    code = encode_kezi(tiles, kui_tile, where=2)
    furo_type, parsed_tiles, add_tile, where = parse_meld(code)

    assert furo_type == 1
    assert set(parsed_tiles) == set(tiles)
    assert add_tile == kui_tile
    assert where == 2


def test_kan_add_from_pon_code_parseable():
    pon_tiles = [17, 18, 19]  # 5m triplet (non-red copies)
    pon_code = encode_kezi(pon_tiles, kui_tile=17, where=1)
    ka_code = encode_kanzi([16, 17, 18, 19], kui_tile=16, where=1, add=16, pon_code=pon_code)
    furo_type, parsed_tiles, add_tile, _where = parse_meld(ka_code)

    # In current implementation, added-kan code is still parsed through koutsu branch.
    assert furo_type == 1
    assert len(parsed_tiles) == 3
    assert isinstance(add_tile, int)
    assert ka_code == (pon_code | (1 << 4))


def test_get_dora_wrap_and_honor_rules():
    # 9m indicator -> 1m dora
    assert get_dora(8 * 4) == 0
    # North indicator -> East dora
    assert get_dora(30 * 4) == 27
    # Chun indicator -> Haku dora
    assert get_dora(33 * 4) == 31
    # 5p indicator -> 6p dora
    assert get_dora(13 * 4) == 14
