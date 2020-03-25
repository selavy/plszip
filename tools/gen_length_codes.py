#!/usr/bin/env python

length_info = (
    # code, extra_bits, start, stop
    ( 257,   0,     3,   3, ),
    ( 258,   0,     4,   4, ),
    ( 259,   0,     5,   5, ),
    ( 260,   0,     6,   6, ),
    ( 261,   0,     7,   7, ),
    ( 262,   0,     8,   8, ),
    ( 263,   0,     9,   9, ),
    ( 264,   0,    10,  10, ),
    ( 265,   1,    11,  12, ),
    ( 266,   1,    13,  14, ),
    ( 267,   1,    15,  16, ),
    ( 268,   1,    17,  18, ),
    ( 269,   2,    19,  22, ),
    ( 270,   2,    23,  26, ),
    ( 271,   2,    27,  30, ),
    ( 272,   2,    31,  34, ),
    ( 273,   3,    35,  42, ),
    ( 274,   3,    43,  50, ),
    ( 275,   3,    51,  58, ),
    ( 276,   3,    59,  66, ),
    ( 277,   4,    67,  82, ),
    ( 278,   4,    83,  98, ),
    ( 279,   4,    99, 114, ),
    ( 280,   4,   115, 130, ),
    ( 281,   5,   131, 162, ),
    ( 282,   5,   163, 194, ),
    ( 283,   5,   195, 226, ),
    ( 284,   5,   227, 257, ),
    ( 285,   0,   258, 258, ),
)


distance_info = (
    # code, extra_bits, start, stop
    (  0,    0,       1,     1, ),
    (  1,    0,       2,     2, ),
    (  2,    0,       3,     3, ),
    (  3,    0,       4,     4, ),
    (  4,    1,       5,     6, ),
    (  5,    1,       7,     8, ),
    (  6,    2,       9,    12, ),
    (  7,    2,      13,    16, ),
    (  8,    3,      17,    24, ),
    (  9,    3,      25,    32, ),
    ( 10,    4,      33,    48, ),
    ( 11,    4,      49,    64, ),
    ( 12,    5,      65,    96, ),
    ( 13,    5,      97,   128, ),
    ( 14,    6,     129,   192, ),
    ( 15,    6,     193,   256, ),
    ( 16,    7,     257,   384, ),
    ( 17,    7,     385,   512, ),
    ( 18,    8,     513,   768, ),
    ( 19,    8,     769,  1024, ),
    ( 20,    9,    1025,  1536, ),
    ( 21,    9,    1537,  2048, ),
    ( 22,   10,    2049,  3072, ),
    ( 23,   10,    3073,  4096, ),
    ( 24,   11,    4097,  6144, ),
    ( 25,   11,    6145,  8192, ),
    ( 26,   12,    8193, 12288, ),
    ( 27,   12,   12289, 16384, ),
    ( 28,   13,   16385, 24576, ),
    ( 29,   13,   24577, 32768, ),
)

def get_distance_extra_bits(dst):
    for code, extra_bits, start, stop in distance_info:
        if start <= dst <= stop:
            return extra_bits
    raise ValueError("invalid distance: {dst}")


def get_distance_base(x):
    for code, extra_bits, start, stop in distance_info:
        if start <= x <= stop:
            return start
    else:
        raise ValueError(f"invalid distance: {x}")


def get_distance_code(x):
    for code, extra_bits, start, stop in distance_info:
        if start <= x <= stop:
            return code
    else:
        raise ValueError(f"invalid distance: {x}")


def get_extra_bits_from_distance_code(dst_code):
    for code, extra_bits, start, stop in distance_info:
        if dst_code == code:
            return extra_bits
    raise ValueError(f"invalid distance code: {dst_code}")


def get_extra_bits_from_literal(lit):
    if lit < 257:
        return 0
    for code, extra_bits, start, stop in length_info:
        if code == lit:
            return extra_bits
    raise ValueError(f"invalid literal: {lit}")


def get_length_code(x):
    for code, extra_bits, start, stop in length_info:
        if start <= x <= stop:
            return code
    else:
        raise ValueError(f"invalid length: {x}")


def get_length_base(x):
    for code, extra_bits, start, stop in length_info:
        if start <= x <= stop:
            return start
    else:
        raise ValueError(f"invalid length: {x}")


def get_length_extra(x):
    for code, extra_bits, start, stop in length_info:
        if start <= x <= stop:
            return extra_bits
    else:
        raise ValueError(f"invalid length: {x}")


# length_codes = [get_length_code(x) for x in range(3, 258+1)]
# length_bases = [get_length_base(x) for x in range(3, 258+1)]
# length_extra = [get_length_extra(x) for x in range(3, 258+1)]

N = 258+1
length_codes = [-1]*N
length_bases = [-1]*N
length_extra = [-1]*N
for index in range(3, N):
    length_codes[index] = get_length_code(index)
    length_bases[index] = get_length_base(index)
    length_extra[index] = get_length_extra(index)

literal_to_extra_bits = [
    get_extra_bits_from_literal(lit) for lit in range(0, 285+1)
]
distance_code_to_extra_bits = [
    get_extra_bits_from_distance_code(dst_code) for dst_code in range(0, 29+1)
]


def dist_index(dst):
    if dst <= 256:
        return dst-1
    else:
        return 256 + ((dst-1) >> 7)


max_dist = 32768-1
max_dist_index = dist_index(max_dist)
distance_codes = [-1]*(max_dist_index+1)
distance_bases = [-1]*(max_dist_index+1)
distance_extra = [-1]*(max_dist_index+1)
for dst in range(1, max_dist+1):
    index = dist_index(dst)
    code = get_distance_code(dst)
    base = get_distance_base(dst)
    extra = get_distance_extra_bits(dst)
    assert distance_codes[index] == -1 or distance_codes[index] == code
    assert distance_bases[index] == -1 or distance_bases[index] == base
    assert distance_extra[index] == -1 or distance_extra[index] == extra
    distance_codes[index] = code
    distance_bases[index] = base
    distance_extra[index] = extra

