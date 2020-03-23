#!/usr/bin/env python

from utils import print_array


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


print("// clang-format off")
print_array(
    dtype='int',
    name='length_codes',
    vals=length_codes,
    min_width=3,
    nums_per_row=8,
)
print("")
print_array(
    dtype='int',
    name='length_bases',
    vals=length_bases,
    min_width=3,
    nums_per_row=8,
)
print("")
print_array(
    dtype='int',
    name='length_extra_bits',
    vals=length_extra,
    min_width=1,
    nums_per_row=16,
)
print("// clang-format on")
