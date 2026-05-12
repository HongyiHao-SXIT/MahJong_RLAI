"""
该文件用于打和牌表
"""

import copy
from itertools import permutations
import pickle

import tqdm


def generate_pattern_permutations(a):
    if len(a) == 1:
        return [a]
    ret = list(permutations(a))
    seen_merge_keys = set()
    for i in range(len(a)):
        for j in range(i + 1, len(a)):
            key = str(a[i] + [0] + a[j])
            if key not in seen_merge_keys:
                seen_merge_keys.add(key)
                seen_patterns = set()
                t1 = copy.deepcopy(a)
                t1.pop(i)
                t1.pop(j - 1)
                if len(a[i]) + len(a[j]) < 9:
                    ret += generate_pattern_permutations([a[i] + [0] + a[j]] + t1)
                    ret += generate_pattern_permutations([a[j] + [0] + a[i]] + t1)
                for k in range(len(a[i] + a[j]) + 1):
                    t = [0] * len(a[j]) + a[i] + [0] * len(a[j])
                    for m in range(len(a[j])):
                        t[k + m] += a[j][m]
                    t = list(filter(bool, t))
                    if any(_ > 4 for _ in t):
                        continue
                    if len(t) > 9:
                        continue
                    if str(t) not in seen_patterns:
                        seen_patterns.add(str(t))
                        t2 = copy.deepcopy(a)
                        t2.pop(i)
                        t2.pop(j - 1)
                        ret += generate_pattern_permutations([t] + t2)
    return ret


def deduplicate_patterns(ret):
    for i in range(len(ret)):
        ret[i] = tuple(map(tuple, ret[i]))
    ret = list(set(ret))
    for i in range(len(ret)):
        ret[i] = list(map(list, ret[i]))
    return ret


def calculate_pattern_key(a):
    ret = 0
    length = -1
    for b in a:
        for i in b:
            length += 1
            if i == 1:
                ret |= 0b11 << length
                length += 2
            elif i == 2:
                ret |= 0b1111 << length
                length += 4
            elif i == 3:
                ret |= 0b111111 << length
                length += 6
            elif i == 4:
                ret |= 0b11111111 << length
                length += 8
        ret |= 0b1 << length
        length += 1
    return ret


def find_tile_positions(a):
    ret_array = []
    pair_position = 0
    for i in range(len(a)):
        for j in range(len(a[i])):
            if a[i][j] == 0:
                continue
            if a[i][j] >= 2:
                for ks in range(2):
                    t = copy.deepcopy(a)
                    t[i][j] -= 2
                    p = 0
                    triplet_positions = []
                    sequence_positions = []
                    for k in range(len(t)):
                        for m in range(len(t[k])):
                            if a[k][m] == 0:
                                continue
                            if ks == 0:
                                if t[k][m] >= 3:
                                    t[k][m] -= 3
                                    triplet_positions.append(p)
                                while len(t[k]) - m >= 3 and t[k][m] >= 1 and t[k][m + 1] >= 1 and t[k][m + 2] >= 1:
                                    t[k][m] -= 1
                                    t[k][m + 1] -= 1
                                    t[k][m + 2] -= 1
                                    sequence_positions.append(p)
                            else:
                                while len(t[k]) - m >= 3 and t[k][m] >= 1 and t[k][m + 1] >= 1 and t[k][m + 2] >= 1:
                                    t[k][m] -= 1
                                    t[k][m + 1] -= 1
                                    t[k][m + 2] -= 1
                                    sequence_positions.append(p)
                                if t[k][m] >= 3:
                                    t[k][m] -= 3
                                    triplet_positions.append(p)
                            p += 1

                    if all(_ == 0 for _ in sum(t, [])):
                        ret = len(triplet_positions) + (len(sequence_positions) << 3) + (pair_position << 6)
                        length = 10
                        for x in triplet_positions:
                            ret |= x << length
                            length += 4
                        for x in sequence_positions:
                            ret |= x << length
                            length += 4
                        if len(a) == 1:
                            if a == [[4, 1, 1, 1, 1, 1, 1, 1, 3]] or \
                                    a == [[3, 2, 1, 1, 1, 1, 1, 1, 3]] or \
                                    a == [[3, 1, 2, 1, 1, 1, 1, 1, 3]] or \
                                    a == [[3, 1, 1, 2, 1, 1, 1, 1, 3]] or \
                                    a == [[3, 1, 1, 1, 2, 1, 1, 1, 3]] or \
                                    a == [[3, 1, 1, 1, 1, 2, 1, 1, 3]] or \
                                    a == [[3, 1, 1, 1, 1, 1, 2, 1, 3]] or \
                                    a == [[3, 1, 1, 1, 1, 1, 1, 2, 3]] or \
                                    a == [[3, 1, 1, 1, 1, 1, 1, 1, 4]]:
                                ret |= 1 << 27
                        if len(a) <= 3 and len(sequence_positions) >= 3:
                            ikki_base_position = 0
                            for b in a:
                                if len(b) == 9:
                                    has_ikki_first_sequence = has_ikki_second_sequence = has_ikki_third_sequence = False
                                    for sequence_position in sequence_positions:
                                        has_ikki_first_sequence |= (sequence_position == ikki_base_position)
                                        has_ikki_second_sequence |= (sequence_position == ikki_base_position + 3)
                                        has_ikki_third_sequence |= (sequence_position == ikki_base_position + 6)
                                    if has_ikki_first_sequence and has_ikki_second_sequence and has_ikki_third_sequence:
                                        ret |= 1 << 28
                                ikki_base_position += len(b)
                        if len(sequence_positions) == 4 and \
                                sequence_positions[0] == sequence_positions[1] and \
                                sequence_positions[2] == sequence_positions[3]:
                            ret |= 1 << 29
                        elif len(sequence_positions) >= 2 and len(triplet_positions) + len(sequence_positions) == 4:
                            if len(sequence_positions) - len(set(sequence_positions)) >= 1:
                                ret |= 1 << 30
                        ret_array.append(ret)
            pair_position += 1
    if len(ret_array) > 0:
        ret_array = list(set(ret_array))
        return ','.join(map(hex, ret_array))
    t = sum(a, [])
    if sum(t) == 14 and all(_ in [0, 2] for _ in t):
        return hex(1 << 26)


def counter_to_pattern(counter):
    counter = list(sorted(counter.items(), key=lambda x: x[0]))
    pattern = []
    current_digit, current_type = None, None
    new = []
    for tile, c in counter:
        digit, t = tile % 9, tile // 9
        if t != current_type or digit - 2 > current_digit or t == 3:
            if new:
                pattern.append(new)
                new = []
        if t == current_type and t != 3 and digit - 2 == current_digit:
            new.extend([0, c])
        else:
            new.append(c)
        current_digit, current_type = digit, t
    if new:
        pattern.append(new)
    return pattern


AGARI_TABLE = 'AGARI_TABLE_2.pkl'
if __name__ == '__main__':
    agari_table = {0: {}, 1: {}}

    chitoi = generate_pattern_permutations([[2], [2], [2], [2], [2], [2], [2]])
    chitoi = list(filter(lambda x: all(_ in [0, 2] for _ in sum(x, [])), chitoi))
    chitoi = deduplicate_patterns(chitoi)
    for p in tqdm.tqdm(chitoi):
        key = calculate_pattern_key(p)
        value = find_tile_positions(p)
        agari_table[0][key] = value
    for a in [[[1, 1, 1], [1, 1, 1], [1, 1, 1], [1, 1, 1], [2]],
              [[1, 1, 1], [1, 1, 1], [1, 1, 1], [3], [2]],
              [[1, 1, 1], [1, 1, 1], [3], [3], [2]],
              [[1, 1, 1], [3], [3], [3], [2]],
              [[3], [3], [3], [3], [2]],
              [[1, 1, 1], [1, 1, 1], [1, 1, 1], [2]],
              [[1, 1, 1], [1, 1, 1], [3], [2]],
              [[1, 1, 1], [3], [3], [2]],
              [[3], [3], [3], [2]],
              [[1, 1, 1], [1, 1, 1], [2]],
              [[1, 1, 1], [3], [2]],
              [[3], [3], [2]],
              [[1, 1, 1], [2]],
              [[3], [2]],
              [[2]]]:
        patterns = deduplicate_patterns(generate_pattern_permutations(a))
        for p in tqdm.tqdm(patterns):
            key = calculate_pattern_key(p)
            value = find_tile_positions(p)
            agari_table[0][key] = value

    p = [[1] for _ in range(12)]
    for i in range(13):
        p.insert(i, [2])
        key = calculate_pattern_key(p)
        agari_table[1][key] = None
        p.pop(i)

    print('和牌pattern数:', len(agari_table[0]))
    with open(AGARI_TABLE, 'wb') as f:
        f.write(pickle.dumps(agari_table))