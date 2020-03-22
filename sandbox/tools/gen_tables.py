#!/usr/bin/env python

#                 Extra               Extra               Extra
#            Code Bits Length(s) Code Bits Lengths   Code Bits Length(s)
#            ---- ---- ------     ---- ---- -------   ---- ---- -------
#             257   0     3       267   1   15,16     277   4   67-82
#             258   0     4       268   1   17,18     278   4   83-98
#             259   0     5       269   2   19-22     279   4   99-114
#             260   0     6       270   2   23-26     280   4  115-130
#             261   0     7       271   2   27-30     281   5  131-162
#             262   0     8       272   2   31-34     282   5  163-194
#             263   0     9       273   3   35-42     283   5  195-226
#             264   0    10       274   3   43-50     284   5  227-257
#             265   1  11,12      275   3   51-58     285   0    258
#             266   1  13,14      276   3   59-66

#                  Extra           Extra               Extra
#             Code Bits Dist  Code Bits   Dist     Code Bits Distance
#             ---- ---- ----  ---- ----  ------    ---- ---- --------
#               0   0    1     10   4     33-48    20    9   1025-1536
#               1   0    2     11   4     49-64    21    9   1537-2048
#               2   0    3     12   5     65-96    22   10   2049-3072
#               3   0    4     13   5     97-128   23   10   3073-4096
#               4   1   5,6    14   6    129-192   24   11   4097-6144
#               5   1   7,8    15   6    193-256   25   11   6145-8192
#               6   2   9-12   16   7    257-384   26   12  8193-12288
#               7   2  13-16   17   7    385-512   27   12 12289-16384
#               8   3  17-24   18   8    513-768   28   13 16385-24576
#               9   3  25-32   19   8   769-1024   29   13 24577-32768

def get_length_code(length):
    table = [
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        12,
        14,
        16,
        18,
        22,
        26,
        30,
        34,
        42,
        50,
        58,
        66,
        82,
        98,
        114,
        130,
        162,
        194,
        226,
        257,
        258,
    ]
    for i, limit in enumerate(table):
        if length <= limit:
            return i + 257
    else:
        raise ValueError(f"Invalid length: {length}!")


def get_length_extra_bits(length):
    length_cutoffs = [
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        12,
        14,
        16,
        18,
        22,
        26,
        30,
        34,
        42,
        50,
        58,
        66,
        82,
        98,
        114,
        130,
        162,
        194,
        226,
        257,
        258,
    ]
    extra_bits = [
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        1,
        1,
        1,
        1,
        2,
        2,
        2,
        2,
        3,
        3,
        3,
        3,
        4,
        4,
        4,
        4,
        5,
        5,
        5,
        5,
        0,
    ]
    for cutoff, extra in zip(length_cutoffs, extra_bits):
        if length <= cutoff:
            return extra
    else:
        raise ValueError


def get_distance_code(dist):
    distance_cutoffs = [
        1,
        2,
        3,
        4,
        6,
        8,
        12,
        16,
        24,
        32,
        48,
        64,
        96,
        128,
        192,
        256,
        384,
        512,
        768,
        1024,
        1536,
        2048,
        3072,
        4096,
        6144,
        8192,
        12288,
        16384,
        24576,
        32768
    ]
    for i, cutoff in enumerate(distance_cutoffs):
        if dist <= cutoff:
            return i
    else:
        raise ValueError(f"Invalid distance: {dist}")


def get_distance_extra_bits(dist):
    distance_cutoffs = [
        1,
        2,
        3,
        4,
        6,
        8,
        12,
        16,
        24,
        32,
        48,
        64,
        96,
        128,
        192,
        256,
        384,
        512,
        768,
        1024,
        1536,
        2048,
        3072,
        4096,
        6144,
        8192,
        12288,
        16384,
        24576,
        32768
    ]
    extra_bits = [
        0,
        0,
        0,
        0,
        1,
        1,
        2,
        2,
        3,
        3,
        4,
        4,
        5,
        5,
        6,
        6,
        7,
        7,
        8,
        8,
        9,
        9,
        10,
        10,
        11,
        11,
        12,
        12,
        13,
        13,
    ]
    for cutoff, extra_bits in zip(distance_cutoffs, extra_bits):
        if dist <= cutoff:
            return extra_bits
    else:
        raise ValueError(f"Invalid distance: {dist}")


length_codes = [get_length_code(x) for x in range(3, 259)]
length_extra_bits_codes = [get_length_extra_bits(x) for x in range(3, 259)]
distance_codes = [get_distance_code(x) for x in range(3, 259)]
distance_extra_bits_codes = [get_distance_extra_bits(x) for x in range(3, 259)]

def print_array(dtype, name, vals, width=3):
    print(f"constexpr {dtype} {name}[{len(vals)}] = {{")
    for i in range(len(vals) // 8):
        vs = vals[8*i:8*i+8]
        vs = [f"{x:{width}}" for x in vs]
        vs = ', '.join(vs)
        print(f"    {vs}")
    n = len(vals) % 8
    if n != 0:
        vs = vals[-n:]
        vs = [f"{x:{width}}" for x in vs]
        vs = ', '.join(vs)
        print(f"    {vs}")
    print("};")


print_array(
    dtype="uint16_t",
    name="length_codes",
    vals=length_codes,
    width=3,
)
print("")
print_array(
    dtype="uint16_t",
    name="length_extra_bits",
    vals=length_extra_bits_codes,
    width=1,
)
