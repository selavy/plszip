#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <utility>

constexpr size_t MaxCodes = 512;
constexpr size_t MaxBitLength = 16;
constexpr uint16_t EmptySentinel = UINT16_MAX;

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

void gen_dense_tree(Vec& newtree, const Vec& code_lengths, size_t max_bit_length, const uint16_t *codes) {
    size_t tablesz = 1u << max_bit_length;
    newtree.assign(tablesz, EmptySentinel);
    for (size_t i = 0; i < code_lengths.size(); ++i) {
        if (code_lengths[i] == 0) continue;
        uint16_t code = codes[i];
        uint16_t codelen = code_lengths[i];
        uint16_t value = static_cast<uint16_t>(i);
        size_t empty_bits = max_bit_length - codelen;
        code = static_cast<uint16_t>(code << empty_bits);
        uint16_t lowbits = static_cast<uint16_t>((1u << empty_bits) - 1);
        uint16_t maxcode = code | lowbits;
        while (code <= maxcode) {
            xassert(newtree[code] == EmptySentinel, "reused index: %u", code);
            newtree[code++] = value;
        }
    }
}

void init_huffman_tree(Vec& newtree, Vec& tree, const Vec& code_lengths) {
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    static uint16_t codes[MaxCodes];

    xassert(code_lengths.size() < MaxCodes, "code lengths too long: %zu", code_lengths.size());

    // 1) Count the number of codes for each code length. Let bl_count[N] be the
    // number of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < code_lengths.size(); ++i) {
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
    for (size_t i = 0; i < code_lengths.size(); ++i) {
        if (code_lengths[i] != 0) {
            codes[i] = next_code[code_lengths[i]]++;
            xassert((16 - __builtin_clz(codes[i])) <= code_lengths[i], "overflowed code length: %u", codes[i]);
        }
    }

    gen_dense_tree(newtree, code_lengths, max_bit_length, codes);

    std::vector<std::string> allcodes;

    // Table size is 2**(max_bit_length + 1)
    size_t table_size = 1u << (max_bit_length + 1);
    tree.assign(table_size, EmptySentinel);
    assert(tree.size() == table_size);
    for (size_t value = 0; value < code_lengths.size(); ++value) {
        int len = static_cast<int>(code_lengths[value]);
        if (len == 0) {
            allcodes.push_back("");
            continue;
        }
        code = codes[value];
        size_t index = 1;
        std::string scode;
        for (int i = len - 1; i >= 0; --i) {
            size_t isset = ((code & (1u << i)) != 0) ? 1 : 0;
            scode += isset ? '1' : '0';
            index = 2 * index + isset;
        }
        allcodes.push_back(scode);
        assert(static_cast<int>(scode.size()) == len);
        xassert(tree[index] == EmptySentinel, "Assigned multiple values to same index: %zu", index);
        tree[index] = static_cast<uint16_t>(value);
    }

    printf("\n");
    for (size_t vv = 0; vv < allcodes.size(); ++vv) {
        printf("%s ==> %3zu\n", allcodes[vv].c_str(), vv);
    }
    printf("\n");

}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    size_t i;
    Vec codes;

    if (1) {
        Vec lits, newlits;
        i = 0;
        while (i++ <= 143) codes.push_back(8);
        while (i++ <= 255) codes.push_back(9);
        while (i++ <= 279) codes.push_back(7);
        while (i++ <= 287) codes.push_back(8);
        init_huffman_tree(newlits, lits, codes);
        for (i = 0; i < newlits.size(); ++i) {
            printf("dsts[%3zu] = 0x%04x\n", i, newlits[i]);
        }
    }

    if (0) {
        Vec dsts, newdsts;
        codes.assign(32, 5);
        init_huffman_tree(newdsts, dsts, codes);
        for (i = 0; i < newdsts.size(); ++i) {
            printf("dsts[%3zu] = 0x%02x\n", i, newdsts[i]);
        }
    }

    return 0;
}
