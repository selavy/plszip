#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>

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

struct vec {
    size_t len;
    uint16_t *d;
};
typedef struct vec vec;
FILE *out;

#define WRITE(fmt, ...) fprintf(out, fmt "\n", ##__VA_ARGS__)

int init_huffman_tree(vec *tree, const uint16_t *code_lengths,
                      size_t n) {
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    static uint16_t codes[MaxCodes];

    if (!(n < MaxCodes)) {
        xassert(n < MaxCodes, "code lengths too long");
        return 1;  // TODO: improve error codes
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the
    // number of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < n; ++i) {
        xassert(code_lengths[i] <= MaxBitLength, "Unsupported bit length");
        ++bl_count[code_lengths[i]];
        max_bit_length =
            code_lengths[i] > max_bit_length ? code_lengths[i] : max_bit_length;
    }
    bl_count[0] = 0;

    // 2) Find the numerical value of the smallest code for each code length:
    memset(&next_code[0], 0, sizeof(next_code));
    uint32_t code = 0;
    for (size_t bits = 1; bits <= max_bit_length; ++bits) {
        code = (code + bl_count[bits - 1]) << 1;
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
            xassert((16 - __builtin_clz(codes[i])) <= code_lengths[i],
                    "overflowed code length");
        }
    }

    // Table size is 2**(max_bit_length + 1)
    size_t table_size = 1u << (max_bit_length + 1);
    // tree.assign(table_size, EmptySentinel);
    tree->d = calloc(table_size, sizeof(uint16_t));
    tree->len = table_size;
    for (int j = 0; j < table_size; ++j) {
        tree->d[j] = EmptySentinel;
    }
    for (size_t value = 0; value < n; ++value) {
        size_t len = code_lengths[value];
        if (len == 0) {
            continue;
        }
        uint16_t code = codes[value];
        size_t index = 1;
        for (int i = len - 1; i >= 0; --i) {
            size_t isset = ((code & (1u << i)) != 0) ? 1 : 0;
            index = 2 * index + isset;
        }
        xassert(tree->d[index] == EmptySentinel,
                "Assigned multiple values to same index");
        tree->d[index] = value;
    }

    return 0;
}

int init_fixed_huffman(vec *lit_tree, vec *dist_tree) {
    static uint16_t codes[288];
    vec cvec;
    int rc;

    /* Literal Tree */
    struct LiteralCodeTableEntry {
        size_t start, stop, bits;
    } xs[] = {
        // start, stop, bits
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
            codes[i] = xs[j].bits;
        }
    }
    // {
    //     size_t i = 0;
    //     while (i < 144) codes[i++] = 8;
    //     while (i < 256) codes[i++] = 9;
    //     while (i < 280) codes[i++] = 7;
    //     while (i < 288) codes[i++] = 8;
    // }
    // cvec.d = &codes[0];
    // cvec.len = 288;
    if ((rc = init_huffman_tree(lit_tree, &codes[0], LIT_CODES)) != 0) {
        panic("failed to initialize fixed literals huffman tree.");
        return rc;
    }

    /* Distance Tree */
    for (size_t i = 0; i < 32; ++i) {
        codes[i] = 5;
    }
    // cvec.d = &codes[0];
    // cvec.len = 32;
    if ((rc = init_huffman_tree(dist_tree, &codes[0], DIST_CODES)) != 0) {
        panic("failed to initialize fixed distance huffman tree.");
        return rc;
    }

    return 0;
}

void print_tree(vec v) {
#define FMT "%5u"
    size_t n = v.len;
    uint16_t *A = v.d;
    size_t i = 0;
    for (; i < n; i += 8) {
        WRITE("    " FMT " " FMT " " FMT " " FMT " " FMT " " FMT " " FMT " " FMT "",
                A[i+0],
                A[i+1],
                A[i+2],
                A[i+3],
                A[i+4],
                A[i+5],
                A[i+6],
                A[i+7]
                );
    }
    fprintf(out, "   ");
    for (; i < n; ++i) {
        fprintf(out, " " FMT, A[i]);
    }
    fprintf(out, "\n");
}

int main(int argc, char **argv) {
    vec lit_tree;
    vec dist_tree;

    if (argc > 1) {
        if (!(out = fopen(argv[1], "w"))) {
            fprintf(stderr, "ERR: failed to open output file: %s", argv[1]);
            exit(0);
        }
    } else {
        out = stdout;
    }

    if (init_fixed_huffman(&lit_tree, &dist_tree) != 0) {
        fprintf(stderr, "ERR: failed to generate fixed huffman codes!\n");
        exit(0);
    }

    WRITE("#ifndef FIXED_HUFFMAN_TREES__H_");
    WRITE("#define FIXED_HUFFMAN_TREES__H_");
    WRITE("");
    WRITE("#include \"<stdint.h>\"");
    WRITE("");
    WRITE("static const uint16_t fixed_huffman_literals_tree[%zu] = {", lit_tree.len);
    print_tree(lit_tree);
    WRITE("};");
    WRITE("");
    WRITE("static const uint16_t fixed_huffman_distance_tree[%zu] = {", dist_tree.len);
    print_tree(dist_tree);
    WRITE("};");
    WRITE("");
    WRITE("// FIXED_HUFFMAN_TREES__H_");

    return 0;
}
