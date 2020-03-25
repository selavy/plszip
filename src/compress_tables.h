#pragma once

#include <cstdint>
#include <type_traits>


// clang-format off

// FIXED HUFFMAN TABLES ---------------------------------------------

constexpr int NumFixedTreeLiterals = 288;
constexpr int NumFixedTreeDistances = 32;

constexpr uint16_t fixed_codes[320] = {
     12, 140,  76, 204,  44, 172, 108, 236,
     28, 156,  92, 220,  60, 188, 124, 252,
      2, 130,  66, 194,  34, 162,  98, 226,
     18, 146,  82, 210,  50, 178, 114, 242,
     10, 138,  74, 202,  42, 170, 106, 234,
     26, 154,  90, 218,  58, 186, 122, 250,
      6, 134,  70, 198,  38, 166, 102, 230,
     22, 150,  86, 214,  54, 182, 118, 246,
     14, 142,  78, 206,  46, 174, 110, 238,
     30, 158,  94, 222,  62, 190, 126, 254,
      1, 129,  65, 193,  33, 161,  97, 225,
     17, 145,  81, 209,  49, 177, 113, 241,
      9, 137,  73, 201,  41, 169, 105, 233,
     25, 153,  89, 217,  57, 185, 121, 249,
      5, 133,  69, 197,  37, 165, 101, 229,
     21, 149,  85, 213,  53, 181, 117, 245,
     13, 141,  77, 205,  45, 173, 109, 237,
     29, 157,  93, 221,  61, 189, 125, 253,
     19, 275, 147, 403,  83, 339, 211, 467,
     51, 307, 179, 435, 115, 371, 243, 499,
     11, 267, 139, 395,  75, 331, 203, 459,
     43, 299, 171, 427, 107, 363, 235, 491,
     27, 283, 155, 411,  91, 347, 219, 475,
     59, 315, 187, 443, 123, 379, 251, 507,
      7, 263, 135, 391,  71, 327, 199, 455,
     39, 295, 167, 423, 103, 359, 231, 487,
     23, 279, 151, 407,  87, 343, 215, 471,
     55, 311, 183, 439, 119, 375, 247, 503,
     15, 271, 143, 399,  79, 335, 207, 463,
     47, 303, 175, 431, 111, 367, 239, 495,
     31, 287, 159, 415,  95, 351, 223, 479,
     63, 319, 191, 447, 127, 383, 255, 511,
      0,  64,  32,  96,  16,  80,  48, 112,
      8,  72,  40, 104,  24,  88,  56, 120,
      4,  68,  36, 100,  20,  84,  52, 116,
      3, 131,  67, 195,  35, 163,  99, 227,
      0,  16,   8,  24,   4,  20,  12,  28,
      2,  18,  10,  26,   6,  22,  14,  30,
      1,  17,   9,  25,   5,  21,  13,  29,
      3,  19,  11,  27,   7,  23,  15,  31,
};

constexpr uint8_t fixed_codelens[320] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};

static_assert(std::extent<decltype(fixed_codes)>::value == std::extent<decltype(fixed_codelens)>::value);
static_assert(std::extent<decltype(fixed_codes)>::value == NumFixedTreeLiterals + NumFixedTreeDistances);

// END FIXED HUFFMAN TABLES -----------------------------------------

// LENGTH + DISTANCE TABLES -----------------------------------------

constexpr int length_codes[259] = {
     -1,  -1,  -1, 257, 258, 259, 260, 261,
    262, 263, 264, 265, 265, 266, 266, 267,
    267, 268, 268, 269, 269, 269, 269, 270,
    270, 270, 270, 271, 271, 271, 271, 272,
    272, 272, 272, 273, 273, 273, 273, 273,
    273, 273, 273, 274, 274, 274, 274, 274,
    274, 274, 274, 275, 275, 275, 275, 275,
    275, 275, 275, 276, 276, 276, 276, 276,
    276, 276, 276, 277, 277, 277, 277, 277,
    277, 277, 277, 277, 277, 277, 277, 277,
    277, 277, 277, 278, 278, 278, 278, 278,
    278, 278, 278, 278, 278, 278, 278, 278,
    278, 278, 278, 279, 279, 279, 279, 279,
    279, 279, 279, 279, 279, 279, 279, 279,
    279, 279, 279, 280, 280, 280, 280, 280,
    280, 280, 280, 280, 280, 280, 280, 280,
    280, 280, 280, 281, 281, 281, 281, 281,
    281, 281, 281, 281, 281, 281, 281, 281,
    281, 281, 281, 281, 281, 281, 281, 281,
    281, 281, 281, 281, 281, 281, 281, 281,
    281, 281, 281, 282, 282, 282, 282, 282,
    282, 282, 282, 282, 282, 282, 282, 282,
    282, 282, 282, 282, 282, 282, 282, 282,
    282, 282, 282, 282, 282, 282, 282, 282,
    282, 282, 282, 283, 283, 283, 283, 283,
    283, 283, 283, 283, 283, 283, 283, 283,
    283, 283, 283, 283, 283, 283, 283, 283,
    283, 283, 283, 283, 283, 283, 283, 283,
    283, 283, 283, 284, 284, 284, 284, 284,
    284, 284, 284, 284, 284, 284, 284, 284,
    284, 284, 284, 284, 284, 284, 284, 284,
    284, 284, 284, 284, 284, 284, 284, 284,
    284, 284, 285,
};

constexpr int length_bases[259] = {
     -1,  -1,  -1,   3,   4,   5,   6,   7,
      8,   9,  10,  11,  11,  13,  13,  15,
     15,  17,  17,  19,  19,  19,  19,  23,
     23,  23,  23,  27,  27,  27,  27,  31,
     31,  31,  31,  35,  35,  35,  35,  35,
     35,  35,  35,  43,  43,  43,  43,  43,
     43,  43,  43,  51,  51,  51,  51,  51,
     51,  51,  51,  59,  59,  59,  59,  59,
     59,  59,  59,  67,  67,  67,  67,  67,
     67,  67,  67,  67,  67,  67,  67,  67,
     67,  67,  67,  83,  83,  83,  83,  83,
     83,  83,  83,  83,  83,  83,  83,  83,
     83,  83,  83,  99,  99,  99,  99,  99,
     99,  99,  99,  99,  99,  99,  99,  99,
     99,  99,  99, 115, 115, 115, 115, 115,
    115, 115, 115, 115, 115, 115, 115, 115,
    115, 115, 115, 131, 131, 131, 131, 131,
    131, 131, 131, 131, 131, 131, 131, 131,
    131, 131, 131, 131, 131, 131, 131, 131,
    131, 131, 131, 131, 131, 131, 131, 131,
    131, 131, 131, 163, 163, 163, 163, 163,
    163, 163, 163, 163, 163, 163, 163, 163,
    163, 163, 163, 163, 163, 163, 163, 163,
    163, 163, 163, 163, 163, 163, 163, 163,
    163, 163, 163, 195, 195, 195, 195, 195,
    195, 195, 195, 195, 195, 195, 195, 195,
    195, 195, 195, 195, 195, 195, 195, 195,
    195, 195, 195, 195, 195, 195, 195, 195,
    195, 195, 195, 227, 227, 227, 227, 227,
    227, 227, 227, 227, 227, 227, 227, 227,
    227, 227, 227, 227, 227, 227, 227, 227,
    227, 227, 227, 227, 227, 227, 227, 227,
    227, 227, 258,
};

constexpr int length_extra_bits[259] = {
    -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 0,
};

constexpr int literal_to_extra_bits[286] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};

constexpr int distance_code_to_extra_bits[30] = {
     0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,
     7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13,
};

constexpr int distance_codes[512] = {
     0,  1,  2,  3,  4,  4,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,
     8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    -1, -1, 16, 17, 18, 18, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21,
    22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25,
    26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
    26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
    27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
    27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
    28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
    28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
    28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
    28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
    29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
    29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
    29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
    29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
};

constexpr int distance_bases[512] = {
        1,     2,     3,     4,     5,     5,     7,     7,
        9,     9,     9,     9,    13,    13,    13,    13,
       17,    17,    17,    17,    17,    17,    17,    17,
       25,    25,    25,    25,    25,    25,    25,    25,
       33,    33,    33,    33,    33,    33,    33,    33,
       33,    33,    33,    33,    33,    33,    33,    33,
       49,    49,    49,    49,    49,    49,    49,    49,
       49,    49,    49,    49,    49,    49,    49,    49,
       65,    65,    65,    65,    65,    65,    65,    65,
       65,    65,    65,    65,    65,    65,    65,    65,
       65,    65,    65,    65,    65,    65,    65,    65,
       65,    65,    65,    65,    65,    65,    65,    65,
       97,    97,    97,    97,    97,    97,    97,    97,
       97,    97,    97,    97,    97,    97,    97,    97,
       97,    97,    97,    97,    97,    97,    97,    97,
       97,    97,    97,    97,    97,    97,    97,    97,
      129,   129,   129,   129,   129,   129,   129,   129,
      129,   129,   129,   129,   129,   129,   129,   129,
      129,   129,   129,   129,   129,   129,   129,   129,
      129,   129,   129,   129,   129,   129,   129,   129,
      129,   129,   129,   129,   129,   129,   129,   129,
      129,   129,   129,   129,   129,   129,   129,   129,
      129,   129,   129,   129,   129,   129,   129,   129,
      129,   129,   129,   129,   129,   129,   129,   129,
      193,   193,   193,   193,   193,   193,   193,   193,
      193,   193,   193,   193,   193,   193,   193,   193,
      193,   193,   193,   193,   193,   193,   193,   193,
      193,   193,   193,   193,   193,   193,   193,   193,
      193,   193,   193,   193,   193,   193,   193,   193,
      193,   193,   193,   193,   193,   193,   193,   193,
      193,   193,   193,   193,   193,   193,   193,   193,
      193,   193,   193,   193,   193,   193,   193,   193,
       -1,    -1,   257,   385,   513,   513,   769,   769,
     1025,  1025,  1025,  1025,  1537,  1537,  1537,  1537,
     2049,  2049,  2049,  2049,  2049,  2049,  2049,  2049,
     3073,  3073,  3073,  3073,  3073,  3073,  3073,  3073,
     4097,  4097,  4097,  4097,  4097,  4097,  4097,  4097,
     4097,  4097,  4097,  4097,  4097,  4097,  4097,  4097,
     6145,  6145,  6145,  6145,  6145,  6145,  6145,  6145,
     6145,  6145,  6145,  6145,  6145,  6145,  6145,  6145,
     8193,  8193,  8193,  8193,  8193,  8193,  8193,  8193,
     8193,  8193,  8193,  8193,  8193,  8193,  8193,  8193,
     8193,  8193,  8193,  8193,  8193,  8193,  8193,  8193,
     8193,  8193,  8193,  8193,  8193,  8193,  8193,  8193,
    12289, 12289, 12289, 12289, 12289, 12289, 12289, 12289,
    12289, 12289, 12289, 12289, 12289, 12289, 12289, 12289,
    12289, 12289, 12289, 12289, 12289, 12289, 12289, 12289,
    12289, 12289, 12289, 12289, 12289, 12289, 12289, 12289,
    16385, 16385, 16385, 16385, 16385, 16385, 16385, 16385,
    16385, 16385, 16385, 16385, 16385, 16385, 16385, 16385,
    16385, 16385, 16385, 16385, 16385, 16385, 16385, 16385,
    16385, 16385, 16385, 16385, 16385, 16385, 16385, 16385,
    16385, 16385, 16385, 16385, 16385, 16385, 16385, 16385,
    16385, 16385, 16385, 16385, 16385, 16385, 16385, 16385,
    16385, 16385, 16385, 16385, 16385, 16385, 16385, 16385,
    16385, 16385, 16385, 16385, 16385, 16385, 16385, 16385,
    24577, 24577, 24577, 24577, 24577, 24577, 24577, 24577,
    24577, 24577, 24577, 24577, 24577, 24577, 24577, 24577,
    24577, 24577, 24577, 24577, 24577, 24577, 24577, 24577,
    24577, 24577, 24577, 24577, 24577, 24577, 24577, 24577,
    24577, 24577, 24577, 24577, 24577, 24577, 24577, 24577,
    24577, 24577, 24577, 24577, 24577, 24577, 24577, 24577,
    24577, 24577, 24577, 24577, 24577, 24577, 24577, 24577,
    24577, 24577, 24577, 24577, 24577, 24577, 24577, 24577,
};

constexpr int distance_extra_bits[512] = {
     0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    -1, -1,  7,  7,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
};


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

// END LENGTH + DISTANCE TABLES -------------------------------------

// clang-format on
//clang-format off
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

