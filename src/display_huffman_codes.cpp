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

#define panic0(msg)                        \
    do {                                   \
        fprintf(stderr, "ERR: " msg "\n"); \
        exit(1);                           \
    } while (0)

#define xassert(c, fmt, ...)                                     \
    do {                                                         \
        if (!(c)) {                                              \
            fprintf(stderr, "ASSERT: " fmt "\n", ##__VA_ARGS__); \
            assert(0);                                           \
        }                                                        \
    } while (0)

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

struct vec {
    size_t len;
    uint16_t *d;
};
typedef struct vec vec;

int init_huffman_tree(vec *tree, const uint16_t *code_lengths, size_t n) {
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    static uint16_t codes[MaxCodes];

    if (!(n < MaxCodes)) {
        xassert(n < MaxCodes, "code lengths too long: %zu", n);
        return 1;  // TODO: improve error codes
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the
    // number of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < n; ++i) {
        xassert(code_lengths[i] <= MaxBitLength, "Unsupported bit length: %u", code_lengths[i]);
        ++bl_count[code_lengths[i]];
        max_bit_length = code_lengths[i] > max_bit_length ? code_lengths[i] : max_bit_length;
    }
    bl_count[0] = 0;

    // 2) Find the numerical value of the smallest code for each code length:
    memset(&next_code[0], 0, sizeof(next_code));
    uint16_t code = 0;
    for (size_t bits = 1; bits <= max_bit_length; ++bits) {
        code = static_cast<uint16_t>((code + bl_count[bits - 1]) << 1);
        next_code[bits] = code;
    }

    // 3) Assign numerical values to all codes, using consecutive values for all
    // codes of the same length with the base values determined at step 2. Codes
    // that are never used (which have a bit length of zero) must not be
    // assigned a value.
    memset(&codes[0], 0, sizeof(codes));
    for (size_t i = 0; i < n; ++i) {
        if (code_lengths[i] != 0) {
            codes[i] = next_code[code_lengths[i]]++;
            xassert((16 - __builtin_clz(codes[i])) <= code_lengths[i], "overflowed code length: %u", codes[i]);
        }
    }

    // Table size is 2**(max_bit_length + 1)
    size_t table_size = 1u << (max_bit_length + 1);
    // tree.assign(table_size, EmptySentinel);
    tree->d = reinterpret_cast<uint16_t*>(calloc(table_size, sizeof(uint16_t)));
    tree->len = table_size;
    for (size_t j = 0; j < table_size; ++j) {
        tree->d[j] = EmptySentinel;
    }
    for (size_t value = 0; value < n; ++value) {
        int len = static_cast<int>(code_lengths[value]);
        if (len == 0) {
            continue;
        }
        code = codes[value];
        size_t index = 1;
        for (int i = len - 1; i >= 0; --i) {
            size_t isset = ((code & (1u << i)) != 0) ? 1 : 0;
            index = 2 * index + isset;
        }
        xassert(tree->d[index] == EmptySentinel, "Assigned multiple values to same index: %zu", index);
        tree->d[index] = static_cast<uint16_t>(value);
    }

    return 0;
}

int init_fixed_huffman(vec *lit_tree, vec *dist_tree) {
    static uint16_t codes[288];
    int rc;
    struct LiteralCodeTableEntry {
        size_t start, stop, bits;
    } xs[] = {
        {
            0,
            143,
            8,
        },
        {
            144,
            255,
            9,
        },
        {
            256,
            279,
            7,
        },
        {
            280,
            287,
            8,
        },
    };
    for (size_t j = 0; j < ARRSIZE(xs); ++j) {
        for (size_t i = xs[j].start; i <= xs[j].stop; ++i) {
            codes[i] = static_cast<uint16_t>(xs[j].bits);
        }
    }
    if ((rc = init_huffman_tree(lit_tree, &codes[0], LIT_CODES)) != 0) {
        panic0("failed to initialize fixed literals huffman tree.");
        return rc;
    }
    for (size_t i = 0; i < 32; ++i) {
        codes[i] = 5;
    }
    if ((rc = init_huffman_tree(dist_tree, &codes[0], DIST_CODES)) != 0) {
        panic0("failed to initialize fixed distance huffman tree.");
        return rc;
    }

    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    vec lit_tree;
    vec dist_tree;
    if (init_fixed_huffman(&lit_tree, &dist_tree) != 0) {
        fprintf(stderr, "ERR: failed to generate fixed huffman codes!\n");
        exit(0);
    }
    return 0;
}
