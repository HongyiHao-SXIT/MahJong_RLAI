"""Look-ahead features (Suphx, Sec. 3.1): each channel is a 34-dim vector describing
what would happen if a given tile were discarded / drawn, computed by searching
over the player's own hand while ignoring the opponents."""
from functools import lru_cache
from typing import Dict, Sequence, Tuple

import numpy as np

LOOKAHEAD_CHANNELS = 19
_YAOCHU = (0, 8, 9, 17, 18, 26, 27, 28, 29, 30, 31, 32, 33)


def _neighbors(counts: Sequence[int]) -> Tuple[int, ...]:
    """Tiles that can possibly improve a regular hand: held tiles and their +-2 suit neighbors."""
    cand = set()
    for t, c in enumerate(counts):
        if c == 0:
            continue
        cand.add(t)
        if t < 27:
            base = t // 9 * 9
            for d in (-2, -1, 1, 2):
                if base <= t + d < base + 9:
                    cand.add(t + d)
    return tuple(sorted(cand))


@lru_cache(maxsize=1 << 16)
def _suit_options(suit: Tuple[int, ...], sequences: bool) -> frozenset:
    """All Pareto-optimal (mentsu, taatsu, pair) decompositions of one suit (or the honors)."""
    c = list(suit)
    n = len(c)
    out = set()
    seen = set()

    def dfs(i, m, t, p):
        while i < n and c[i] == 0:
            i += 1
        if i >= n:
            out.add((m, t, p))
            return
        state = (i, tuple(c[i:]), m, t, p)
        if state in seen:
            return
        seen.add(state)
        if c[i] >= 3:
            c[i] -= 3
            dfs(i, m + 1, t, p)
            c[i] += 3
        if sequences and i + 2 < n and c[i + 1] and c[i + 2]:
            c[i] -= 1; c[i + 1] -= 1; c[i + 2] -= 1
            dfs(i, m + 1, t, p)
            c[i] += 1; c[i + 1] += 1; c[i + 2] += 1
        if c[i] >= 2:
            if not p:
                c[i] -= 2
                dfs(i, m, t, 1)
                c[i] += 2
            c[i] -= 2
            dfs(i, m, t + 1, p)
            c[i] += 2
        if sequences:
            if i + 1 < n and c[i + 1]:
                c[i] -= 1; c[i + 1] -= 1
                dfs(i, m, t + 1, p)
                c[i] += 1; c[i + 1] += 1
            if i + 2 < n and c[i + 2]:
                c[i] -= 1; c[i + 2] -= 1
                dfs(i, m, t + 1, p)
                c[i] += 1; c[i + 2] += 1
        c[i] -= 1
        dfs(i, m, t, p)
        c[i] += 1

    dfs(0, 0, 0, 0)
    return frozenset(o for o in out if not any(q != o and q[0] >= o[0] and q[1] >= o[1] and q[2] >= o[2] for q in out))


@lru_cache(maxsize=1 << 18)
def _regular_shanten(counts: Tuple[int, ...], melds: int) -> int:
    states = {(melds, 0, 0)}
    for start, end, seq in ((0, 9, True), (9, 18, True), (18, 27, True), (27, 34, False)):
        options = _suit_options(counts[start:end], seq)
        states = {(m + dm, t + dt, min(1, p + dp)) for m, t, p in states for dm, dt, dp in options
                  if not (p and dp)}
    best = 8
    for m, t, p in states:
        best = min(best, 8 - 2 * m - min(t, 4 - m) - p)
    return best


def _chiitoi_shanten(counts: Tuple[int, ...]) -> int:
    pairs = sum(1 for c in counts if c >= 2)
    kinds = sum(1 for c in counts if c >= 1)
    return 6 - pairs + max(0, 7 - kinds)


def _kokushi_shanten(counts: Tuple[int, ...]) -> int:
    kinds = sum(1 for t in _YAOCHU if counts[t] >= 1)
    pair = 1 if any(counts[t] >= 2 for t in _YAOCHU) else 0
    return 13 - kinds - pair


@lru_cache(maxsize=1 << 18)
def shanten(counts: Tuple[int, ...], melds: int = 0) -> int:
    """Minimum shanten over regular / seven pairs / thirteen orphans (-1 means complete)."""
    s = _regular_shanten(counts, melds)
    if melds == 0:
        s = min(s, _chiitoi_shanten(counts), _kokushi_shanten(counts))
    return s


def _candidates(counts: Tuple[int, ...], melds: int) -> Tuple[int, ...]:
    cand = set(_neighbors(counts))
    if melds == 0:
        if _kokushi_shanten(counts) <= 4:
            cand.update(_YAOCHU)
        cand.update(t for t, c in enumerate(counts) if c)
    return tuple(sorted(cand))


@lru_cache(maxsize=1 << 16)
def _ukeire(counts: Tuple[int, ...], melds: int, live: Tuple[int, ...]) -> Tuple[int, int, Tuple[int, ...]]:
    """For a 13-tile (3n+1) hand: (live tiles that lower shanten, distinct such types, per-type live count)."""
    base = shanten(counts, melds)
    arr = list(counts)
    total, kinds = 0, 0
    mask = [0] * 34
    for t in _candidates(counts, melds):
        if live[t] <= 0 or arr[t] >= 4:
            continue
        arr[t] += 1
        if shanten(tuple(arr), melds) < base:
            total += live[t]
            kinds += 1
            mask[t] = live[t]
        arr[t] -= 1
    return total, kinds, tuple(mask)


def _one_hot_shanten(value: int) -> np.ndarray:
    out = np.zeros(5, dtype=np.float32)
    out[min(max(value + 1, 0), 4)] = 1.0  # -1,0,1,2,>=3
    return out


def lookahead_feature(counter, melds: int, visible) -> np.ndarray:
    """
    :param counter: concealed hand tile counter (34 types)
    :param melds: number of open/closed melds already declared
    :param visible: tile type -> number of copies already visible to this player (including own hand)
    :return: (LOOKAHEAD_CHANNELS, 34)
    """
    counts = tuple(int(counter.get(t, 0)) if hasattr(counter, 'get') else int(counter[t]) for t in range(34))
    live = tuple(max(0, 4 - int(visible.get(t, 0))) for t in range(34))
    total = sum(counts)
    feat = np.zeros((LOOKAHEAD_CHANNELS, 34), dtype=np.float32)

    cur = shanten(counts, melds)
    feat[0:5] = _one_hot_shanten(cur)[:, None]
    feat[16] = melds / 4.0
    feat[17] = _chiitoi_shanten(counts) / 6.0 if melds == 0 else 1.0
    feat[18] = _kokushi_shanten(counts) / 13.0 if melds == 0 else 1.0

    if total % 3 == 2:  # discard decision: evaluate every possible discard
        if cur == -1:
            feat[15] = 1.0
        arr = list(counts)
        results: Dict[int, Tuple[int, int, int, Tuple[int, ...]]] = {}
        for t in range(34):
            if arr[t] == 0:
                continue
            arr[t] -= 1
            sub = tuple(arr)
            sh = shanten(sub, melds)
            ulive, kinds, mask = _ukeire(sub, melds, live)
            results[t] = (sh, ulive, kinds, mask)
            arr[t] += 1
        best = min(results, key=lambda t: (results[t][0], -results[t][1]))
        for t, (sh, ulive, kinds, _) in results.items():
            feat[0:5, t] = _one_hot_shanten(sh)
            feat[5, t] = ulive / 40.0
            for k, thr in enumerate((1, 8, 16, 24)):
                feat[6 + k, t] = float(ulive >= thr)
            feat[10, t] = kinds / 34.0
        feat[11, best] = 1.0
        mask = results[best][3]
        ref_shanten = results[best][0]
    else:  # waiting for a draw / meld: describe the current hand
        ulive, kinds, mask = _ukeire(counts, melds, live)
        feat[5] = ulive / 40.0
        feat[10] = kinds / 34.0
        ref_shanten = cur

    mask_arr = np.array(mask, dtype=np.float32)
    feat[12] = mask_arr > 0
    feat[13] = mask_arr / 4.0
    if ref_shanten == 0:
        feat[14] = mask_arr > 0  # tenpai: improving tiles are exactly the winning tiles
    return feat
