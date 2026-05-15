from termcolor import colored
import unicodedata
from typing import Optional

ASCII_TILES = {
    0: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　一　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/""",
    1: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　二　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/""",
    2: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　三　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/""",
    3: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　四　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/""",
    4: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　伍　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/""",
    5: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　六　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/""",
    6: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　七　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/""",
    7: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　八　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/""",
    8: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　九　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/""",
    9: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　　　┃┃
┃　⬤　 ┃┃
┃　　　┃┃
┃＿＿＿┃/""",
    10: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　⬤　 ┃┃
┃　　　┃┃
┃　⬤　 ┃┃
┃＿＿＿┃/""",
    11: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃⬤　　 ┃┃
┃　⬤　 ┃┃
┃　　⬤ ┃┃
┃＿＿＿┃/""",
    12: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃⬤　 ⬤ ┃┃
┃　　　┃┃
┃⬤　 ⬤ ┃┃
┃＿＿＿┃/""",
    13: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃⬤　 ⬤ ┃┃
┃　⬤　 ┃┃
┃⬤　 ⬤ ┃┃
┃＿＿＿┃/""",
    14: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃⬤　 ⬤ ┃┃
┃⬤　 ⬤ ┃┃
┃⬤　 ⬤ ┃┃
┃＿＿＿┃/""",
    15: """　＿＿＿ 
/＿＿＿/┃
┃⬤ ⬤ 　┃┃
┃　　⬤ ┃┃
┃⬤　 ⬤ ┃┃
┃⬤　 ⬤ ┃┃
┃＿＿＿┃/""",
    16: """　＿＿＿ 
/＿＿＿/┃
┃⬤　 ⬤ ┃┃
┃⬤　 ⬤ ┃┃
┃⬤　 ⬤ ┃┃
┃⬤　 ⬤ ┃┃
┃＿＿＿┃/""",
    17: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃⬤ ⬤ ⬤ ┃┃
┃⬤ ⬤ ⬤ ┃┃
┃⬤ ⬤ ⬤ ┃┃
┃＿＿＿┃/""",
    18: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　一　┃┃
┃　🐦　┃┃
┃　索　┃┃
┃＿＿＿┃/""",
    19: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　 |  ┃┃
┃　    ┃┃
┃　 |  ┃┃
┃＿＿＿┃/""",
    20: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　 |  ┃┃
┃　    ┃┃
┃　| | ┃┃
┃＿＿＿┃/""",
    21: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　| | ┃┃
┃　    ┃┃
┃　| | ┃┃
┃＿＿＿┃/""",
    22: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　| | ┃┃
┃　 |  ┃┃
┃　| | ┃┃
┃＿＿＿┃/""",
    23: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　| | ┃┃
┃　| | ┃┃
┃　| | ┃┃
┃＿＿＿┃/""",
    24: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　 |  ┃┃
┃ | | |┃┃
┃ | | |┃┃
┃＿＿＿┃/""",
    25: r"""　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃ |/\| ┃┃
┃　    ┃┃
┃ |\/| ┃┃
┃＿＿＿┃/""",
    26: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃ | | |┃┃
┃ | | |┃┃
┃ | | |┃┃
┃＿＿＿┃/""",
    27: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　　　┃┃
┃　東　┃┃
┃　　　┃┃
┃＿＿＿┃/""",
    28: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　　　┃┃
┃　南　┃┃
┃　　　┃┃
┃＿＿＿┃/""",
    29: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　　　┃┃
┃　西　┃┃
┃　　　┃┃
┃＿＿＿┃/""",
    30: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　　　┃┃
┃　北　┃┃
┃　　　┃┃
┃＿＿＿┃/""",
    31: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　　　┃┃
┃　　　┃┃
┃　　　┃┃
┃＿＿＿┃/""",
    32: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　　　┃┃
┃　發　┃┃
┃　　　┃┃
┃＿＿＿┃/""",
    33: """　＿＿＿ 
/＿＿＿/┃
┃　　　┃┃
┃　　　┃┃
┃　中　┃┃
┃　　　┃┃
┃＿＿＿┃/""",
}

AKA_MAN = """　＿＿＿ 
/＿＿＿/┃
┃赤　　┃┃
┃　伍　┃┃
┃　　　┃┃
┃　萬　┃┃
┃＿＿＿┃/"""
AKA_PIN = """　＿＿＿ 
/＿＿＿/┃
┃赤　　┃┃
┃⬤　 ⬤ ┃┃
┃　⬤　 ┃┃
┃⬤　 ⬤ ┃┃
┃＿＿＿┃/"""
AKA_SOU = """　＿＿＿ 
/＿＿＿/┃
┃赤　　┃┃
┃　| | ┃┃
┃　 |  ┃┃
┃　| | ┃┃
┃＿＿＿┃/"""

TENHOU_TILE_ASCII = {i: ASCII_TILES[i // 4] for i in range(136)}
TENHOU_TILE_ASCII[16] = AKA_MAN
TENHOU_TILE_ASCII[52] = AKA_PIN
TENHOU_TILE_ASCII[88] = AKA_SOU


def get_visual_length(s):
    length = 0
    for char in s:
        if unicodedata.east_asian_width(char) in ('F', 'W'):
            length += 2
        else:
            length += 1
    return length


def pad_string(s, target_length):
    visual_length = get_visual_length(s)
    padding_length = target_length - visual_length
    if padding_length > 0:
        left_length = padding_length // 2
        right_length = padding_length - left_length
        return ' ' * left_length + s + ' ' * right_length
    else:
        return s[:target_length + (visual_length - target_length) // 2]


def yellow(s):
    return colored(s, color='yellow', attrs=['bold', 'blink'])


def magenta(s):
    return colored(s, color='magenta', attrs=['bold', 'blink'])


def green(s):
    return colored(s, color='green', attrs=['bold', 'blink'])


def red(s):
    return colored(s, color='red', attrs=['bold', 'blink'])


def blue(s):
    return colored(s, color='blue', attrs=['bold', 'blink'])


def cyan(s):
    return colored(s, color='cyan', attrs=['bold', 'blink'])


def light_grey(s):
    return colored(s, color='light_grey', attrs=['bold', 'blink'])


def ascii_style_print(tile_groups, with_color: Optional[str] = 'green'):
    ascii_group = []
    for tiles in tile_groups:
        ascii_str = list(map(lambda x: x.split('\n'), [TENHOU_TILE_ASCII[_] for _ in tiles]))
        ascii_str = list(map(lambda x: '　'.join(x), zip(*ascii_str)))
        ascii_group.append(ascii_str)
    ascii_group_str = '\n'.join(map(lambda x: '　　'.join(x), zip(*ascii_group)))
    if with_color:
        return globals()[with_color](ascii_group_str)
    return ascii_group_str


chinese_numerals = {1: '一', 2: '二', 3: '三', 4: '四', 5: '五', 6: '六', 7: '七', 8: '八', 9: '九'}
TILE_STRING_DICT = {
    **{i: f"{chinese_numerals[i + 1]}萬" for i in range(9)},
    **{i: f"{chinese_numerals[i - 8]}饼" for i in range(9, 18)},
    **{i: f"{chinese_numerals[i - 17]}索" for i in range(18, 27)},
    27: "東", 28: "南", 29: "西", 30: "北", 31: "白", 32: "發", 33: "中"
}
TENHOU_TILE_STRING_DICT = {i: TILE_STRING_DICT[i // 4] for i in range(136)}
TENHOU_TILE_STRING_DICT[16] = '赤五萬'
TENHOU_TILE_STRING_DICT[52] = '赤五饼'
TENHOU_TILE_STRING_DICT[88] = '赤五索'

TILE_UNICODE = "🀇🀈🀉🀊🀋🀌🀍🀎🀏🀙🀚🀛🀜🀝🀞🀟🀠🀡🀐🀑🀒🀓🀔🀕🀖🀗🀘🀀🀁🀂🀃🀆🀅🀄︎"
TENHOU_TILE_UNICODE_DICT = {i: TILE_UNICODE[i // 4] for i in range(136)}
TENHOU_TILE_UNICODE_DICT[16] = '赤🀋'
TENHOU_TILE_UNICODE_DICT[52] = '赤🀝'
TENHOU_TILE_UNICODE_DICT[88] = '赤🀔'
