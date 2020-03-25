#!/usr/bin/env python

from utils import print_array

from gen_fixed_trees import fixed_codes
from gen_fixed_trees import fixed_codelens
from gen_fixed_trees import num_fixed_tree_lits
from gen_fixed_trees import num_fixed_tree_dists

from gen_length_codes import length_codes
from gen_length_codes import length_bases
from gen_length_codes import length_extra
from gen_length_codes import distance_codes
from gen_length_codes import distance_bases
from gen_length_codes import distance_extra
from gen_length_codes import literal_to_extra_bits
from gen_length_codes import distance_code_to_extra_bits

#
# ----------------- BEGIN GENERATION --------------------------------------- #
#

print("#pragma once")
print("")
print("#include <cstdint>")
print("#include <type_traits>")
print("")
print("")
print("// clang-format off")
print("")
print("// FIXED HUFFMAN TABLES ---------------------------------------------")
print("")
print(f"constexpr int NumFixedTreeLiterals = {num_fixed_tree_lits};")
print(f"constexpr int NumFixedTreeDistances = {num_fixed_tree_dists};")
print("")
print_array(
    dtype='uint16_t',
    name='fixed_codes',
    vals=fixed_codes,
    min_width=3,
    nums_per_row=8,
)
print("")
print_array(
    dtype='uint8_t',
    name='fixed_codelens',
    vals=fixed_codelens,
    min_width=1,
    nums_per_row=16,
)
print("")
print("static_assert(std::extent<decltype(fixed_codes)>::value == std::extent<decltype(fixed_codelens)>::value);")
print("static_assert(std::extent<decltype(fixed_codes)>::value == NumFixedTreeLiterals + NumFixedTreeDistances);")
print("")
print("// END FIXED HUFFMAN TABLES -----------------------------------------")
print("")
print("// LENGTH + DISTANCE TABLES -----------------------------------------")
print("")
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
print("")
print_array(
    dtype='int',
    name='literal_to_extra_bits',
    vals=literal_to_extra_bits,
    min_width=1,
    nums_per_row=16,
)
print("")
print_array(
    dtype='int',
    name='distance_code_to_extra_bits',
    vals=distance_code_to_extra_bits,
    min_width=2,
    nums_per_row=16,
)
print("")
print_array(
    dtype='int',
    name='distance_codes',
    vals=distance_codes,
    min_width=2,
    nums_per_row=16,
)
print("")
print_array(
    dtype='int',
    name='distance_bases',
    vals=distance_bases,
    min_width=5,
    nums_per_row=8,
)
print("")
print_array(
    dtype='int',
    name='distance_extra_bits',
    vals=distance_extra,
    min_width=2,
    nums_per_row=16,
)
print("")
print("""
constexpr int get_distance_index(int dst) noexcept {
    assert(1 <= dst && dst <= 32768);
    return dst <= 256 ? dst - 1 : ((dst - 1) >> 7) + 256;
}

constexpr int get_distance_code(int dst) noexcept {
    return distance_codes[get_distance_index(dst)];
}

constexpr int get_distance_base(int dst) noexcept {
    return distance_bases[get_distance_index(dst)];
}

constexpr int get_distance_extra_bits(int dst) noexcept {
    return distance_extra_bits[get_distance_index(dst)];
}

constexpr int get_length_code(int length) noexcept {
    assert(0 <= length && length <= 258);
    return length_codes[length];
}

constexpr int get_length_base(int length) noexcept {
    assert(0 <= length && length <= 258);
    return length_bases[length];
}

constexpr int get_length_extra_bits(int length) noexcept {
    assert(0 <= length && length <= 258);
    return length_extra_bits[length];
}
""")
print("// END LENGTH + DISTANCE TABLES -------------------------------------")
print("")
print("// clang-format on")


# TEMP TEMP TEMP
print("""//clang-format off
struct DistInfo {
    int code;
    int extra_bits;
    int start; // inclusive
    int stop;  // inclusive
};
constexpr DistInfo distance_info[] = {
    {  0,    0,       1,     1, },
    {  1,    0,       2,     2, },
    {  2,    0,       3,     3, },
    {  3,    0,       4,     4, },
    {  4,    1,       5,     6, },
    {  5,    1,       7,     8, },
    {  6,    2,       9,    12, },
    {  7,    2,      13,    16, },
    {  8,    3,      17,    24, },
    {  9,    3,      25,    32, },
    { 10,    4,      33,    48, },
    { 11,    4,      49,    64, },
    { 12,    5,      65,    96, },
    { 13,    5,      97,   128, },
    { 14,    6,     129,   192, },
    { 15,    6,     193,   256, },
    { 16,    7,     257,   384, },
    { 17,    7,     385,   512, },
    { 18,    8,     513,   768, },
    { 19,    8,     769,  1024, },
    { 20,    9,    1025,  1536, },
    { 21,    9,    1537,  2048, },
    { 22,   10,    2049,  3072, },
    { 23,   10,    3073,  4096, },
    { 24,   11,    4097,  6144, },
    { 25,   11,    6145,  8192, },
    { 26,   12,    8193, 12288, },
    { 27,   12,   12289, 16384, },
    { 28,   13,   16385, 24576, },
    { 29,   13,   24577, 32768, },
};
// clang-format on
""")
