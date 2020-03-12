#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MaxCodes 512
#define MaxBitLength 16
#define EmptySentinel UINT16_MAX
#define LIT_CODES 288
#define DIST_CODES 32

#define panic(fmt, ...)                                   \
    do {                                                  \
        fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); \
        exit(1);                                          \
    } while (0)

#define xassert(c, fmt, ...)                                     \
    do {                                                         \
        if (!(c)) {                                              \
            fprintf(stderr, "ASSERT: " fmt "\n", ##__VA_ARGS__); \
            assert(0);                                           \
        }                                                        \
    } while (0)

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))
#define MAX(a, b) (a) > (b) ? (a) : (b)

struct vec {
    size_t len;
    size_t maxlen;
    uint16_t *d;
};
typedef struct vec vec;
FILE *out;

#define WRITE(fmt, ...) fprintf(out, fmt "\n", ##__VA_ARGS__)

// clang-format off
static const unsigned char BitReverseTable256[256] =
{
#   define R2(n)     n,     n + 2*64,     n + 1*64,     n + 3*64
#   define R4(n) R2(n), R2(n + 2*16), R2(n + 1*16), R2(n + 3*16)
#   define R6(n) R4(n), R4(n + 2*4 ), R4(n + 1*4 ), R4(n + 3*4 )
    R6(0), R6(2), R6(1), R6(3)
};
// clang-format off

uint16_t flip_u16(uint16_t v)
{
    // clang-format off
    return
        (BitReverseTable256[(v >> 0) & 0xff] << 8) |
        (BitReverseTable256[(v >> 8) & 0xff] << 0) ;
    // clang-format on
}

uint16_t flip_code(uint16_t code, size_t codelen) {
    assert(0 < codelen && codelen <= 16);
    code = flip_u16(code);
    code >>= (16 - codelen);
    return code;
}

void init_huffman_tree(vec *tree, const uint16_t *code_lengths, size_t n) {
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    static uint16_t codes[MaxCodes];
    size_t max_bit_length = 0;

    xassert(n < MaxCodes, "code lengths too long");

    {
        // 1) Count the number of codes for each code length. Let bl_count[N] be the
        // number of codes of length N, N >= 1.
        memset(&bl_count[0], 0, sizeof(bl_count));
        for (size_t i = 0; i < n; ++i) {
            xassert(code_lengths[i] <= MaxBitLength, "invalid bit length: %u for value %zu", code_lengths[i], i);
            ++bl_count[code_lengths[i]];
            max_bit_length = MAX(code_lengths[i], max_bit_length);
        }
        bl_count[0] = 0;
    }

    {
        // 2) Find the numerical value of the smallest code for each code length:
        memset(&next_code[0], 0, sizeof(next_code));
        uint32_t code = 0;
        for (size_t bits = 1; bits <= max_bit_length; ++bits) {
            code = (code + bl_count[bits - 1]) << 1;
            next_code[bits] = code;
        }
    }

    {
        // 3) Assign numerical values to all codes, using consecutive values for all
        // codes of the same length with the base values determined at step 2. Codes
        // that are never used (which have a bit length of zero) must not be
        // assigned a value.
        memset(&codes[0], 0, sizeof(codes));
        for (size_t i = 0; i < n; ++i) {
            if (code_lengths[i] != 0) {
                codes[i] = next_code[code_lengths[i]]++;
                xassert((16 - __builtin_clz(codes[i])) <= code_lengths[i], "overflowed code length");
            }
        }
    }

    {
        // 4) Generate dense table. This means that can read `max_bit_length` bits at a
        // time, and do a lookup immediately; should then use `code_lengths` to
        // determine how many of the peek'd bits should be removed.
        size_t tablesz = 1u << max_bit_length;
        tree->d = calloc(tablesz, sizeof(uint16_t));
        tree->len = tablesz;
        for (int j = 0; j < tablesz; ++j) {
            tree->d[j] = EmptySentinel;
        }
        for (size_t i = 0; i < n; ++i) {
            if (code_lengths[i] == 0) continue;
            uint16_t code = codes[i];
            uint16_t codelen = code_lengths[i];
            uint16_t value = (uint16_t)i;
            size_t empty_bits = max_bit_length - codelen;
            code = (uint16_t)(code << empty_bits);
            uint16_t lowbits = (uint16_t)((1u << empty_bits) - 1);
            uint16_t maxcode = code | lowbits;
            while (code <= maxcode) {
                uint16_t flipped = flip_code(code, max_bit_length);
                xassert(tree->d[flipped] == EmptySentinel, "reused index: %u", flipped);
                tree->d[flipped] = value;
                ++code;
            }
        }
        tree->maxlen = max_bit_length;
    }
}

void print_tree(const uint16_t *A, size_t n) {
#define FMT "%5u"
    size_t i;
    for (i = 0; i < n; i += 8) {
        // clang-format off
        WRITE("    " FMT ", " FMT ", " FMT ", " FMT ", " FMT ", " FMT ", " FMT ", " FMT ",    // (%4zu)",
                A[i + 0], A[i + 1], A[i + 2], A[i + 3],
                A[i + 4], A[i + 5], A[i + 6], A[i + 7],
                i);
        // clang-format on
    }
    fprintf(out, "    ");
    for (; i < n; ++i) {
        fprintf(out, FMT ", ", A[i]);
    }
}

int main(int argc, char **argv) {
    static uint16_t litlens[288];
    static uint16_t dstlens[32];
    vec lits;
    vec dsts;
    size_t i;

    if (argc > 1) {
        if (!(out = fopen(argv[1], "w"))) {
            fprintf(stderr, "ERR: failed to open output file: %s", argv[1]);
            exit(0);
        }
    } else {
        out = stdout;
    }

    {
        i = 0;
        while (i < 144) litlens[i++] = 8;
        while (i < 256) litlens[i++] = 9;
        while (i < 280) litlens[i++] = 7;
        while (i < 288) litlens[i++] = 8;
        init_huffman_tree(&lits, litlens, ARRSIZE(litlens));
    }

    {
        for (i = 0; i < 32; ++i)
            dstlens[i] = 5;
        init_huffman_tree(&dsts, dstlens, ARRSIZE(dstlens));
    }

    WRITE("#ifndef FIXED_HUFFMAN_TREES__H_");
    WRITE("#define FIXED_HUFFMAN_TREES__H_");
    WRITE("");
    WRITE("#include <cstdint>");
    WRITE("");
    WRITE("static constexpr uint8_t fixed_huffman_literals_maxbits = %zu;", lits.maxlen);
    WRITE("");
    WRITE("static constexpr uint8_t fixed_huffman_literals_lens[%zu] = {", ARRSIZE(litlens));
    print_tree(litlens, ARRSIZE(litlens));
    WRITE("};");
    WRITE("");
    WRITE("static constexpr uint16_t fixed_huffman_literals_codes[%zu] = {", lits.len);
    print_tree(lits.d, lits.len);
    WRITE("};");
    WRITE("");
    WRITE("static constexpr uint8_t fixed_huffman_distance_maxbits = %zu;", dsts.maxlen);
    WRITE("");
    WRITE("static constexpr uint8_t fixed_huffman_distance_lens[%zu] = {", ARRSIZE(dstlens));
    print_tree(dstlens, ARRSIZE(dstlens));
    WRITE("};");
    WRITE("");
    WRITE("static constexpr uint16_t fixed_huffman_distance_codes[%zu] = {", dsts.len);
    print_tree(dsts.d, dsts.len);
    WRITE("};");
    WRITE("");
    WRITE("#endif // FIXED_HUFFMAN_TREES__H_");

    free(lits.d);
    free(dsts.d);

    return 0;
}
