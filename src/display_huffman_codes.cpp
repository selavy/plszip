#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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

using Vec = std::vector<uint16_t>;

void init_huffman_tree(Vec& tree, const uint16_t *code_lengths, size_t n) {
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    static uint16_t codes[MaxCodes];

    xassert(n < MaxCodes, "code lengths too long: %zu", n);

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
    tree.assign(table_size, EmptySentinel);
    assert(tree.size() == table_size);
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
        xassert(tree[index] == EmptySentinel, "Assigned multiple values to same index: %zu", index);
        tree[index] = static_cast<uint16_t>(value);
    }
}

void init_fixed_huffman(Vec& lit_tree, Vec& dist_tree) {
    static uint16_t codes[288];
    {
        size_t i = 0;
        while (i <= 143)
            codes[i++] = 8;
        while (i <= 255)
            codes[i++] = 9;
        while (i <= 279)
            codes[i++] = 7;
        while (i <= 287)
            codes[i++] = 8;
    }
    init_huffman_tree(lit_tree, &codes[0], LIT_CODES);

    for (size_t i = 0; i < 32; ++i) {
        codes[i] = 5;
    }
    init_huffman_tree(dist_tree, &codes[0], DIST_CODES);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    Vec lits, dsts;
    init_fixed_huffman(lits, dsts);
    for (size_t i = 0; i < lits.size(); ++i) {
        printf("lits[%zu] = %u\n", i, lits[i]);
    }
    for (size_t i = 0; i < dsts.size(); ++i) {
        printf("dsts[%zu] = %u\n", i, dsts[i]);
    }
    return 0;
}
