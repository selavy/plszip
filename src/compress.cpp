#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cxxopts.hpp>

#include "compress_tables.h"
#include "crc32.h"

#define panic(fmt, ...)                                   \
    do {                                                  \
        fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); \
        exit(1);                                          \
    } while (0)

#define xassert(c, fmt, ...)                                              \
    do {                                                                  \
        if (!(c)) {                                                       \
            fprintf(stderr, "ASSERT: " #c " : " fmt "\n", ##__VA_ARGS__); \
            assert(c);                                                    \
        }                                                                 \
    } while (0)

#define DEBUG0(msg) fprintf(stderr, "DEBUG: " msg "\n");
#define DEBUG(fmt, ...) fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__);
#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

#define TRACE(fmt, ...)
// #define TRACE(fmt, ...) fprintf(stdout, "TRACE: " fmt "\n", ##__VA_ARGS__);

constexpr size_t BUFSIZE = 1 << 15;  // 1 << 10;
constexpr size_t BLOCKSIZE = 1 << 15;
constexpr int NumHeaderCodeLengths = 19;
constexpr int LiteralCodes = 256;  // [0, 255] doesn't include END_BLOCK code
constexpr int LengthCodes = 29;    // [257, 285]
constexpr int LitCodes = LiteralCodes + LengthCodes + 1;
constexpr int DistCodes = 30;  // [0, 29]
constexpr int MaxNumCodes = LitCodes + DistCodes;
constexpr int HeaderLengthBits = 3;
constexpr int MaxHeaderCodeLength = (1u << HeaderLengthBits) - 1;
constexpr int MinMatchLength = 3;
constexpr int MaxMatchLength = 258;
constexpr int MinMatchDistance = 1;
constexpr int MaxMatchDistance = 32768;
constexpr int MaxBits = 15;
constexpr uint8_t ID1_GZIP = 31;
constexpr uint8_t ID2_GZIP = 139;
constexpr uint8_t CM_DEFLATE = 8;

struct HuffTrees {
    const uint16_t* codes;
    const uint8_t* codelens;
    size_t n_lits;
    size_t n_dists;
};

constexpr HuffTrees fixed_tree = {fixed_codes, fixed_codelens, NumFixedTreeLiterals, NumFixedTreeDistances};

struct FileHandle {
    FileHandle(FILE* f = nullptr) noexcept : fp(f) {}
    FileHandle(FileHandle&) noexcept = delete;
    FileHandle& operator=(FileHandle&) noexcept = delete;
    FileHandle(FileHandle&& rhs) noexcept : fp(rhs.fp) { rhs.fp = nullptr; }
    FileHandle& operator=(FileHandle&& rhs) noexcept {
        fp = rhs.fp;
        rhs.fp = nullptr;
        return *this;
    }
    ~FileHandle() noexcept {
        if (fp) {
            fclose(fp);
        }
    }
    operator FILE*() noexcept { return fp; }
    explicit operator bool() const noexcept { return fp != nullptr; }
    FILE* fp;
};

enum class Flags : uint8_t {
    FTEXT = 1u << 0,
    FHCRC = 1u << 1,
    FEXTRA = 1u << 2,
    FNAME = 1u << 3,
    FCOMMENT = 1u << 4,
    RESERV1 = 1u << 5,
    RESERV2 = 1u << 6,
    RESERV3 = 1u << 7,
};

// BTYPE specifies how the data are compressed, as follows:
// 00 - no compression
// 01 - compressed with fixed Huffman codes
// 10 - compressed with dynamic Huffman codes
// 11 - reserved (error)
enum class BType : uint8_t {
    NO_COMPRESSION = 0x0u,
    FIXED_HUFFMAN = 0x1u,
    DYNAMIC_HUFFMAN = 0x2u,
    RESERVED = 0x3u,
};

void xwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (fwrite(ptr, size, nmemb, stream) != nmemb) {
        panic("short write");
    }
}

struct Code {
    constexpr Code(uint16_t code_, uint16_t codelen_) noexcept : code{code_}, codelen{codelen_} {}
    uint16_t code;
    uint16_t codelen;
};

using CodeLengths = std::vector<uint8_t>;

struct Tree {
    std::vector<uint16_t> codes;  // [MaxNumCodes+1];
    CodeLengths codelens;         // [MaxNumCodes+1];
    int n_lits;
    int n_dists;
};

// TODO: use Code
struct TreeNode {
    TreeNode(int v, int b) noexcept : value{v}, codelen{b} {}
    int value;
    int codelen;
};

struct Node {
    Node(int v, int w) : value{v}, weight{w} {}
    int value;
    int weight;
    Node* left = nullptr;
    Node* right = nullptr;
    int depth = -1;
};

struct NodeCmp {
    bool operator()(const Node* a, const Node* b) {
        // STL heap api is for a max heap and expects less than comparison
        return a->weight > b->weight;
    }
};

void assign_depth(Node* n, int depth) {
    if (!n) return;
    assign_depth(n->left, depth + 1);
    n->depth = depth;
    // printf("n: value=%d weight=%d depth=%d\n", n->value, n->weight, depth);
    assign_depth(n->right, depth + 1);
}

std::map<int, int> count_values(const int* values, size_t n_values) {
    std::map<int, int> counts;
    for (size_t i = 0; i < n_values; ++i) {
        counts[values[i]]++;
    }
    return counts;
}

std::vector<TreeNode> construct_huffman_tree(const std::map<int, int>& counts) {
    // TODO: max number of nodes is 2*N + 1?
    std::list<Node> pool;
    std::vector<Node*> nodes;
    for (auto&& [value, count] : counts) {
        assert(0 <= value);
        assert(count > 0);
        auto& n = pool.emplace_back(value, count);
        n.left = n.right = nullptr;
        nodes.push_back(&n);
    }

    auto&& pop_heap = [](std::vector<Node*>& nodes) {
        std::pop_heap(nodes.begin(), nodes.end(), NodeCmp{});
        nodes.pop_back();
    };

    std::make_heap(nodes.begin(), nodes.end(), NodeCmp{});
    while (nodes.size() >= 2) {
        Node* a = nodes[0];
        pop_heap(nodes);
        Node* b = nodes[0];
        pop_heap(nodes);
        auto& n = pool.emplace_back(-1, a->weight + b->weight);
        n.left = a;
        n.right = b;
        nodes.push_back(&n);
        std::push_heap(nodes.begin(), nodes.end(), NodeCmp{});
    }

    assert(nodes.size() == 1);
    assign_depth(nodes[0], 0);

    std::vector<TreeNode> result;
    for (auto&& node : pool) {
        if (node.value != -1) {
            result.emplace_back(node.value, node.depth);
        }
    }

    if (result.empty()) {
        result.push_back({0, 1});
    } else if (result[0].codelen == 0) {
        assert(result.size() == 1);
        result[0].codelen = 1;
    }

    return result;
}

struct DynamicCodeLengths {
    CodeLengths codelens;
    size_t hlit;
    size_t hdist;
};

static const unsigned char BitReverseTable256[256] = {
// clang-format off
#   define R2(n)     n,     n + 2*64,     n + 1*64,     n + 3*64
#   define R4(n) R2(n), R2(n + 2*16), R2(n + 1*16), R2(n + 3*16)
#   define R6(n) R4(n), R4(n + 2*4 ), R4(n + 1*4 ), R4(n + 3*4 )
    R6(0), R6(2), R6(1), R6(3)
#undef R2
#undef R4
#undef R6
    // clang-format on
};

uint16_t flip_u16(uint16_t v) noexcept {
    // clang-format off
    return static_cast<uint16_t>(
        (BitReverseTable256[(v >> 0) & 0xff] << 8) |
        (BitReverseTable256[(v >> 8) & 0xff] << 0)
    );
    // clang-format on
}

uint16_t flip_code(uint16_t code, size_t codelen) {
    assert(0 < codelen && codelen <= 16);
    return static_cast<uint16_t>(flip_u16(code) >> (16 - codelen));
}

void init_huffman_tree(const uint8_t* codelens, int n_values, uint16_t* out_codes) {
    size_t bl_count[MaxBits];
    uint16_t next_code[MaxBits];

    // 1) Count the number of codes for each code length. Let bl_count[N] be the
    // number of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    int max_bit_length = 0;
    for (int i = 0; i < n_values; ++i) {
        xassert(codelens[i] <= MaxBits, "Unsupported bit length");
        ++bl_count[codelens[i]];
        max_bit_length = std::max<int>(codelens[i], max_bit_length);
    }
    bl_count[0] = 0;

    // 2) Find the numerical value of the smallest code for each code length:
    memset(&next_code[0], 0, sizeof(next_code));
    uint32_t code = 0;
    for (int bits = 1; bits <= max_bit_length; ++bits) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    // 3) Assign numerical values to all codes, using consecutive values for all
    // codes of the same length with the base values determined at step 2. Codes
    // that are never used (which have a bit length of zero) must not be
    // assigned a value.
    for (int i = 0; i < n_values; ++i) {
        if (codelens[i] != 0) {
            out_codes[i] = flip_code(next_code[codelens[i]]++, codelens[i]);
        }
    }
}

// All multi-byte numbers in the format described here are stored with
// the least-significant byte first (at the lower memory address).
//
//  * Data elements are packed into bytes in order of
//    increasing bit number within the byte, i.e., starting
//    with the least-significant bit of the byte.
//  * Data elements other than Huffman codes are packed
//    starting with the least-significant bit of the data
//    element.
//  * Huffman codes are packed starting with the most-
//    significant bit of the code.
//
// THEREFORE:
//  1. Multi-byte numbers are little-endian
//  2. Huffman codes are are packed most significant -> least significant
//  3. Everything else is least significant -> most significant

struct BitWriter {
    using Buffer = uint32_t;
    constexpr static size_t BufferSizeInBits = 32;
    static_assert((sizeof(Buffer) * CHAR_BIT) >= BufferSizeInBits);

    BitWriter(FILE* fp) noexcept : out_{fp} {}

    void write_bits(uint16_t val, size_t n_bits) noexcept {
        total_written += n_bits;
        assert(n_bits <= MaxBits);
        if (bits_ == BufferSizeInBits) {
            _write_full_buffer();
        }
        auto room = BufferSizeInBits - bits_;
        if (room >= n_bits) {
            buff_ |= val << bits_;
            bits_ += n_bits;
        } else {
            auto n1 = std::min(room, n_bits);
            auto n2 = n_bits - n1;
            buff_ |= (val & _ones_mask(n1)) << bits_;
            bits_ += n1;
            _write_full_buffer();
            assert(bits_ == 0);
            buff_ |= val >> n1;
            bits_ += n2;
        }
        assert(bits_ <= BufferSizeInBits);
    }

    void write(const void* p, size_t size) noexcept {
        total_written += 8 * size;
        flush();
        xwrite(p, 1, size, out_);
    }

    void flush() noexcept {
        auto n_bytes = (bits_ + 7) / 8;
        xwrite(&buff_, 1, n_bytes, out_);
        buff_ = 0;
        bits_ = 0;
    }

    void _write_full_buffer() noexcept {
        assert(bits_ == BufferSizeInBits);
        xwrite(&buff_, sizeof(buff_), 1, out_);
        buff_ = 0;
        bits_ = 0;
    }

    static constexpr Buffer _ones_mask(size_t n_bits) noexcept {
        assert(n_bits <= BufferSizeInBits);
        if (n_bits == BufferSizeInBits) {
            return static_cast<Buffer>(-1);
        } else {
            return static_cast<Buffer>((1u << n_bits) - 1);
        }
    }

    Buffer buff_ = 0;
    size_t bits_ = 0;
    FILE* out_ = nullptr;
    uint64_t total_written = 0;
};

void blkwrite_no_compression(const uint8_t* const buffer, size_t size, uint8_t bfinal, BitWriter& out) {
    uint8_t block_type = static_cast<uint8_t>(BType::NO_COMPRESSION);
    uint8_t btype = static_cast<uint8_t>(BType::NO_COMPRESSION);
    xassert(size < UINT16_MAX, "invalid size: %zu", size);
    uint16_t len = size;  //  & 0xffffu;
    uint16_t nlen = len ^ 0xffffu;
    out.write_bits(bfinal, 1);
    out.write_bits(block_type, 2);
    out.flush();
    // TODO: technically need to force little endian
    out.write(&len, sizeof(len));
    out.write(&nlen, sizeof(nlen));
    out.write(&buffer[0], size);
}

void write_block(const std::vector<int>& lits, const std::vector<int>& dsts, const HuffTrees& tree, BitWriter& out) {
    assert(lits.size() == dsts.size());
    for (size_t i = 0; i < lits.size(); ++i) {
        auto len = lits[i] - LiteralCodes;
        auto lit = lits[i] <= LiteralCodes ? lits[i] : get_length_code(len);
        xassert(0 <= lit && lit <= 285, "invalid literal: %d", lit);
        uint16_t lit_huff_code = tree.codes[lit];
        int lit_n_bits = tree.codelens[lit];
        xassert(lit_n_bits > 0, "invalid code length: %u", lit_n_bits);
        assert(1 <= lit_n_bits && lit_n_bits <= MaxBits);
        out.write_bits(lit_huff_code, lit_n_bits);
        if (lit >= 257) {
            auto len_base = get_length_base(len);
            auto len_extra = len - len_base;
            xassert(len_extra >= 0, "len < len_base: %d %d", len, len_base);
            auto len_extra_bits = get_length_extra_bits(len);
            if (len_extra_bits > 0) {
                out.write_bits(static_cast<uint16_t>(len_extra), len_extra_bits);
            }

            auto dst = dsts[i];
            xassert(1 <= dst && dst <= 32768, "invalid distance: %d", dst);
            auto dst_code = get_distance_code(dst);
            xassert(0 <= dst_code && dst_code <= 29, "invalid distance code: %d", dst_code);
            uint16_t dst_huff_code = tree.codes[tree.n_lits + dst_code];
            int dst_n_bits = tree.codelens[tree.n_lits + dst_code];
            xassert(dst_n_bits > 0, "invalid code length: %u", lit_n_bits);
            out.write_bits(static_cast<uint16_t>(dst_huff_code), dst_n_bits);

            auto dst_base = get_distance_base(dst);
            auto dst_extra = dst - dst_base;
            assert(dst_extra >= 0);
            auto dst_extra_bits = get_distance_extra_bits(dst);
            if (dst_extra_bits > 0) {
                out.write_bits(static_cast<uint16_t>(dst_extra), dst_extra_bits);
            }
        }
    }
}

struct DynamicHeader {
    std::vector<int> codes;
    std::vector<int> extra;
    Tree tree;
};

DynamicHeader make_header_tree(const CodeLengths& codelens) {
    std::vector<int> codes;
    std::vector<int> extra;
    assert(!codelens.empty());
    int buf = codelens[0];
    int cnt = 0;
    for (auto codelen : codelens) {
        if (buf == codelen) {
            cnt++;
        } else if (cnt < 3) {
            codes.insert(codes.end(), cnt, buf);
            extra.insert(extra.end(), cnt, 0);
            buf = codelen;
            cnt = 1;
        } else if (buf == 0) {
            assert(cnt >= 3);
            assert(codelen != buf);
            while (cnt >= 11) {
                int amt = std::min(cnt, 138);
                assert(11 <= amt && amt <= 138);
                codes.push_back(18);
                extra.push_back(amt);
                cnt -= amt;
            }
            while (cnt >= 3) {
                int amt = std::min(cnt, 10);
                assert(3 <= amt && amt <= 10);
                codes.push_back(17);
                extra.push_back(amt);
                cnt -= amt;
            }
            if (cnt > 0) {
                codes.insert(codes.end(), cnt, 0);
                extra.insert(extra.end(), cnt, 0);
            }
            buf = codelen;
            cnt = 1;
        } else {
            assert(cnt >= 3);
            assert(buf != 0);
            if (codes.empty() || codes.back() != buf) {
                codes.push_back(buf);
                extra.push_back(0);
                cnt--;
            }
            while (cnt >= 3) {
                int amt = std::min(cnt, 6);
                codes.push_back(16);
                extra.push_back(amt);
                cnt -= amt;
            }
            codes.insert(codes.end(), cnt, buf);
            extra.insert(extra.end(), cnt, 0);
            buf = codelen;
            cnt = 1;
        }
        assert(codes.size() == extra.size());
        assert(buf == codelen);
        assert(cnt > 0);
    }

    // flush
    if (cnt < 3) {
        codes.insert(codes.end(), cnt, buf);
        extra.insert(extra.end(), cnt, 0);
    } else if (buf == 0) {
        assert(cnt >= 3);
        while (cnt >= 11) {
            int amt = std::min(cnt, 138);
            assert(11 <= amt && amt <= 138);
            codes.push_back(18);
            extra.push_back(amt);
            cnt -= amt;
        }
        while (cnt >= 3) {
            int amt = std::min(cnt, 10);
            assert(3 <= amt && amt <= 10);
            codes.push_back(17);
            extra.push_back(amt);
            cnt -= amt;
        }
        if (cnt > 0) {
            codes.insert(codes.end(), cnt, 0);
            extra.insert(extra.end(), cnt, 0);
        }
    } else {
        assert(cnt >= 3);
        assert(buf != 0);
        if (codes.empty() || codes.back() != buf) {
            codes.push_back(buf);
            extra.push_back(0);
            cnt--;
        }
        while (cnt >= 3) {
            int repeat_amount = std::min(6, cnt);
            codes.push_back(16);
            extra.push_back(repeat_amount);
            cnt -= repeat_amount;
        }
        codes.insert(codes.end(), cnt, buf);
        extra.insert(extra.end(), cnt, 0);
    }

    auto header_tree = construct_huffman_tree(count_values(codes.data(), codes.size()));

    Tree tree;
    tree.codes.assign(NumHeaderCodeLengths, 0xffffu);
    tree.codelens.assign(NumHeaderCodeLengths, 0);
    tree.n_lits = NumHeaderCodeLengths;
    tree.n_dists = 0;  // TEMP TEMP
    for (auto&& [value, bits] : header_tree) {
        assert(0 <= value && value < NumHeaderCodeLengths);
        assert(0 <= bits && bits < MaxBits);
        tree.codelens[value] = static_cast<uint8_t>(bits);
    }
    init_huffman_tree(&tree.codelens[0], tree.n_lits, &tree.codes[0]);
    return {codes, extra, tree};
}

struct HeaderTreeData {
    std::array<int, NumHeaderCodeLengths> codelens;
    int hclen = 0;
};
HeaderTreeData make_header_tree_data(const Tree& tree) {
    constexpr std::array<int, NumHeaderCodeLengths> order = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                             11, 4,  12, 3, 13, 2, 14, 1, 15};
    // TDOO: detect codelen > MaxHeaderCodeLength and bail if so by setting hdr_cost very high
    HeaderTreeData results = {};
    for (size_t i = 0; i < order.size(); ++i) {
        results.codelens[i] = tree.codelens[order[i]];
    }
    results.hclen = static_cast<int>(order.size());
    while (results.hclen > 4 && results.codelens[results.hclen - 1] == 0) {
        --results.hclen;
    }
    assert(results.hclen >= 4 && (results.codelens[results.hclen - 1] != 0 || results.hclen == 4));
    return results;
}

constexpr uint32_t update_hash(uint32_t current, uint8_t c) noexcept {
    constexpr uint32_t mask = (1u << 24) - 1;
    return ((current << 8) | c) & mask;
}

int longest_match(const uint8_t* const wnd, const uint8_t* const str, int max_length) {
    int i = 0;
    for (; i < max_length; ++i) {
        if (wnd[i] != str[i]) break;
    }
    return i;
}

struct BlockResults {
    CodeLengths codelens;
    size_t hlit;
    size_t hdist;
    std::vector<int> lits;  // lit <= LiteralCodes --> literal code
                            // lit  > LiteralCodes --> length value
    std::vector<int> dsts;
    int64_t fix_cost;
    int64_t dyn_cost;
};

#if 0
template <class T>
void analyze_hash_table(const T& ht, const char* buf) {
    size_t longest_chain = 0;
    uint64_t total_chain = 0;
    uint64_t num_chains = 0;
    for (auto&& [h, locs] : ht) {
        ++num_chains;
        total_chain += locs.size();
        longest_chain = std::max(longest_chain, locs.size());

        auto* bb = reinterpret_cast<const uint8_t*>(buf);
        auto p1 = locs[0];
        for (size_t i = 1; i < locs.size(); ++i) {
            auto p2 = locs[i];
            xassert(bb[p1+0] == bb[p2+0], "%u %u", bb[p1+0], bb[p2+0]);
            xassert(bb[p1+1] == bb[p2+1], "%u %u", bb[p1+1], bb[p2+1]);
            xassert(bb[p1+2] == bb[p2+2], "%u %u", bb[p1+2], bb[p2+2]);
        }
    }
    DEBUG("HASH TABLE STATS");
    DEBUG("num_chains : %lu", num_chains);
    DEBUG("total chain: %lu", total_chain);
    DEBUG("mean chain : %0.3f", total_chain / static_cast<double>(num_chains));
    DEBUG("longest    : %zu", longest_chain);
    DEBUG("END HASH TABLE STATS");
}
#endif

#ifndef NDEBUG
#define CHECK_HASH(i)                                                                                     \
    {                                                                                                     \
        uint32_t h2 = update_hash(update_hash(update_hash(0, buf[(i) + 0]), buf[(i) + 1]), buf[(i) + 2]); \
        uint32_t h3 = (buf[(i) + 0] << 16) | (buf[(i) + 1] << 8) | (buf[(i) + 2]);                        \
        assert(h2 == h3);                                                                                 \
        xassert(h == h2, "%u != %u", h, h2);                                                              \
    }
#else
#define CHECK_HASH(i)
#endif

struct Config {
    int good_length; /* reduce lazy search above this match length */
    int max_lazy;    /* do not perform lazy search above this match length */
    int nice_length; /* quit search above this match length */
    int max_chain;
    // compress_func func;
    // using compress_func = BlockResult (*)(const uint8_t* const buf, size_t size, Config config);
};

// clang-format off
// configs taken from zlib in deflate.c
constexpr Config configs[/*10*/] = {
    /*      good lazy nice chain */
    /* 0 */ {  0,   0,   0,    0 },  // deflate_stored},  /* store only */
    /* 1 */ {  4,   4,   8,    4 },  // deflate_fast}, /* max speed, no lazy matches */
    /* 2 */ {  4,   5,  16,    8 },  // deflate_fast},
    /* 3 */ {  4,   6,  32,   32 },  // deflate_fast},

    /* 4 */ {  4,   4,  16,   16 },  // deflate_slow},  /* lazy matches */
    /* 5 */ {  8,  16,  32,   32 },  // deflate_slow},
    /* 6 */ {  8,  16, 128,  128 },  // deflate_slow},
    /* 7 */ {  8,  32, 128,  256 },  // deflate_slow},
    /* 8 */ { 32, 128, 258, 1024 },  // deflate_slow},
    /* 9 */ { 32, 258, 258, 4096 },  // deflate_slow}}; /* max compression */
    /* 10 */ { INT_MAX, INT_MAX, INT_MAX, INT_MAX },
};
// clang-format on

BlockResults finish_up(std::vector<int>& lits, std::vector<int>& dsts, std::map<int, int>& lit_counts,
                       std::map<int, int>& dst_counts) {
    // TODO: remove this, shouldn't do dynamic encoding if the input is empty
    // edge case for when input is empty
    if (lit_counts.empty()) {
        lit_counts[0] = 1;
    }

    // must have code for END_BLOCK
    lits.push_back(256);
    dsts.push_back(0);
    lit_counts[lits.back()] = 1;

    assert(lits.size() == dsts.size());
    // assert(lits.size() == lens.size());

    auto lit_tree = construct_huffman_tree(lit_counts);
#ifndef NDEBUG
    for (auto&& [value, codelen] : lit_tree) {
        xassert(0 <= value && value < 286, "invalid lit value: %d", value);
        xassert(1 <= codelen && codelen <= MaxBits, "invalid codelen: %d", codelen);
    }
#endif

    // TODO: try out dst_counts.empty() case so I can test my inflate implementation
    //
    // NOTE: rather than handling case of no length+distance codes, just add 2 codes
    //       because that is what gzip appears to do
    if (dst_counts.empty()) {
        dst_counts[0] = 1;
        dst_counts[1] = 1;
    }
    auto dst_tree = construct_huffman_tree(dst_counts);
#ifndef NDEBUG
    for (auto&& [value, codelen] : dst_tree) {
        xassert(0 <= value && value < 32, "invalid dst value: %d", value);
        xassert(1 <= codelen && codelen <= MaxBits, "invalid codelen: %d", codelen);
    }
#endif

    assert(!lit_tree.empty());
    int max_lit_value = std::max_element(lit_tree.begin(), lit_tree.end(), [](TreeNode a, TreeNode b) {
                            return a.value < b.value;
                        })->value;
    int max_dst_value = std::max_element(dst_tree.begin(), dst_tree.end(), [](TreeNode a, TreeNode b) {
                            return a.value < b.value;
                        })->value;

    // Ranges:
    // HLIT:  257 - 286
    // HDIST: 1 - 32
    size_t hlit = std::max(max_lit_value + 1, 257);
    size_t hdist = std::max(max_dst_value + 1, 1);
    assert(257 <= hlit && hlit <= 286);
    assert(1 <= hdist && hdist <= 32);

    CodeLengths codelens(hlit + hdist, 0);
    assert(codelens.size() == hlit + hdist);
    for (auto&& [value, codelen] : lit_tree) {
        xassert(0 <= value && value < codelens.size(), "invalid value: %d", value);
        xassert(1 <= codelen && codelen <= MaxBits, "invalid codelen: %d", codelen);
        codelens[value] = codelen;
    }
    for (auto&& [value, codelen] : dst_tree) {
        xassert(hlit <= (value + hlit) && (value + hlit) < codelens.size(), "invalid value: %d", value);
        xassert(1 <= codelen && codelen <= MaxBits, "invalid codelen: %d", codelen);
        codelens[value + hlit] = codelen;
    }
    assert(codelens[256] != 0);

    int64_t fix_cost = 0;
    int64_t dyn_cost = 0;
    for (auto&& [lit, count] : lit_counts) {
        dyn_cost += count * codelens[lit];
        fix_cost += count * fixed_codelens[lit];
        assert(0 <= lit && lit < ARRSIZE(literal_to_extra_bits));
        dyn_cost += count * literal_to_extra_bits[lit];
        fix_cost += count * literal_to_extra_bits[lit];
    }
    for (auto&& [dst_code, count] : dst_counts) {
        dyn_cost += count * codelens[hlit + dst_code];
        fix_cost += count * fixed_codelens[NumFixedTreeLiterals + dst_code];
        assert(0 <= dst_code && dst_code <= ARRSIZE(distance_code_to_extra_bits));
        dyn_cost += count * distance_code_to_extra_bits[dst_code];
        fix_cost += count * distance_code_to_extra_bits[dst_code];
    }

    return {codelens, hlit, hdist, lits, dsts, fix_cost, dyn_cost};
}

BlockResults analyze_block_lazy(const uint8_t* const buf, size_t size, Config config) {
    TRACE("analyze_block_lazy: good_length=%d max_lazy=%d nice_length=%d max_chain=%d", config.good_length,
          config.max_lazy, config.nice_length, config.max_chain);

    const int good_length = config.good_length;
    const int max_lazy = config.max_lazy;
    const int nice_length = config.nice_length;
    const int max_chain = config.max_chain;
    std::vector<int> lits;
    std::vector<int> dsts;
    std::map<uint32_t, std::vector<int>> ht;
    uint32_t h = size >= MinMatchLength ? (buf[0] << 8) | buf[1] : 0;

    auto tally_lit = [&](int lit) {
        lits.push_back(lit);
        dsts.push_back(0);
    };

    auto tally_dst_len = [&](int dst, int len) {
        lits.push_back(LiteralCodes + len);
        dsts.push_back(dst);
    };

    auto update_hash_for_length = [&](int pos, int match_length) {
        for (int i = 1; i < match_length; ++i) {
            if (pos + i + 2 >= size) {
                return;
            }
            h = update_hash(h, buf[pos + i + 2]);
            CHECK_HASH(pos + i);
            ht[h].push_back(pos + i);
        }
    };

    const int max_pos = static_cast<int>(size) - MinMatchLength;
    int pos = 0;
    int prev_length = MinMatchLength - 1;
    int prev_distance = -1;  // TEMP TEMP
    bool need_flush = false;
    while (pos < max_pos) {
        int length = MinMatchLength - 1;
        int distance = -1;  // TEMP TEMP
        h = update_hash(h, buf[pos + 2]);
        auto& locs = ht[h];

        if (prev_length < max_lazy) {
            // find longest match (within constraints of max_chain and nice_length)
            const int max_iters = prev_length >= good_length ? max_chain >> 2 : max_chain;
            int iter = 0;
            for (auto rit = locs.rbegin(); rit != locs.rend(); ++rit) {
                int loc = *rit;
                const int match_length = longest_match(buf + loc, buf + pos, std::min(static_cast<size_t>(MaxMatchLength), size - pos));
                if (match_length > length) {
                    length = match_length;
                    distance = pos - loc;
                    xassert(3 <= length && length <= MaxMatchLength, "invalid match length (too long): %d", length);
                    xassert(0 <= distance && distance <= MaxMatchDistance, "invalid distance (too far): %d", distance);
                }
                if (length >= nice_length || ++iter >= max_iters /*!(max_iters-- > 0)*/) {
                    TRACE("exceeded match or chain length: match_length=%d chain_length=%d", length, iter);
                    break;
                }
            }
        }

        // add position
        locs.push_back(pos);

        const int prev_pos = pos - 1;
        if (prev_length >= MinMatchLength && prev_length >= length) {
            TRACE("using match: len=%d dist=%d str=\"%.*s\" (new_len=%d, new_dst=%d)", prev_length, prev_distance,
                  prev_length, &buf[prev_pos - prev_distance], length, distance);

            xassert(pos != 0, "had previous match at pos=0?");
            tally_dst_len(prev_distance, prev_length);

            // prev_length = 3 ; prev_distance = 3                   curr position     new position
            //                                                             |               |
            //                                                             v               v
            //         | pos-5 | pos-4 | pos-3 | pos-2 | pos-1 | pos   | pos+1 | pos+2 | pos+3 | pos+4 |
            //         ---------------------------------------------------------------------------------
            //         | 'h'   | 'i'   | 's'   | ' '   | 'i'   | 's'   | ' '   | 'a'   | 't'   | 'e'   |
            //         ---------------------------------------------------------------------------------
            // hashed: |  x    |  x    |  x    |  x    |  x    |  x    |  x    |  x    |       |       |
            //         ---------------------------------------------------------------------------------
            //                                                                                    ^
            //                                                                                    |
            //                                                                           need to hash to here

            for (int i = 2; i < prev_length && (prev_pos + 2 + i) < size; ++i) {
                h = update_hash(h, buf[prev_pos + 2 + i]);
                CHECK_HASH(prev_pos + i);
                ht[h].push_back(prev_pos + i);
            }
            need_flush = false;
            pos = prev_pos + prev_length;
            prev_length = MinMatchLength - 1;
            prev_distance = -1;  // TEMP TEMP
        } else if (need_flush) {
            assert(prev_pos >= 0);
            tally_lit(buf[prev_pos]);
            pos++;
            prev_length = length;
            prev_distance = distance;
        } else {
            need_flush = true;
            pos++;
            prev_length = length;
            prev_distance = distance;
        }
    }

    {  // flush final match or literal
        const int prev_pos = pos - 1;
        if (prev_length >= MinMatchLength) {
            tally_dst_len(prev_distance, prev_length);
            pos = prev_pos + prev_length;
        } else if (need_flush) {
            tally_lit(buf[prev_pos]);
            pos = prev_pos + 1;  // TEMP TEMP -- no-op remove
        }
    }

    for (; pos < size; ++pos) {
        tally_lit(buf[pos]);
    }

    // TEMP TEMP -- for simplicity count everything at the end
    std::map<int, int> lit_counts;
    std::map<int, int> dst_counts;
    for (int lit : lits) {
        if (lit <= LiteralCodes) {
            lit_counts[lit]++;
        } else {
            lit_counts[get_length_code(lit - LiteralCodes)]++;
        }
    }
    for (int dst : dsts) {
        if (dst != 0) {
            dst_counts[get_distance_code(dst)]++;
        }
    }

    TRACE("--- DST CODE COUNTS")
    for (auto&& [dst, cnt] : dst_counts) {
        TRACE("%d: %d", dst, cnt);
    }
    TRACE("--- END DST CODE COUNTS")

    return finish_up(lits, dsts, lit_counts, dst_counts);
}

BlockResults analyze_block(const uint8_t* const buf, size_t size, Config config) {
    // TODO: add fast path for analyzing very small blocks. no point in even trying
    //       dynamic encoding in that case, and potentially gives optimization ability
    //       to know that there are at least N bytes of input

    TRACE("analyze_block: good_length=%d max_lazy=%d nice_length=%d max_chain=%d", config.good_length, config.max_lazy,
          config.nice_length, config.max_chain);

    std::vector<int> lits;
    std::vector<int> dsts;
    std::map<int, int> lit_counts;
    std::map<int, int> dst_counts;
    std::map<uint32_t, std::vector<int>> ht;
    const int nice_length = config.nice_length;
    const int max_chain = config.max_chain;
    uint32_t h = size >= 2 ? ((buf[0] << 8) | (buf[1] << 0)) : 0;

    auto tally_lit = [&](int lit) {
        assert(0 <= lit && lit <= LiteralCodes);
        lits.push_back(lit);
        dsts.push_back(0);
        lit_counts[lit]++;
    };
    auto tally_dst_len = [&](int dst, int len) {
        assert(MinMatchDistance <= dst && dst <= MaxMatchDistance);
        assert(MinMatchLength <= len && len <= MaxMatchLength);
        lits.push_back(LiteralCodes + len);
        dsts.push_back(dst);
        lit_counts[get_length_code(len)]++;
        dst_counts[get_distance_code(dst)]++;
    };

    size_t i = 0;
    while (i + 3 < size) {
        xassert(i + 2 < size, "i=%zu size=%zu", i, size);
        h = update_hash(h, buf[i + 2]);
        CHECK_HASH(i);
        auto& locs = ht[h];
        int length = 2;
        int distance = 0;
        int iter = 0;
        for (auto rit = locs.rbegin(); rit != locs.rend(); ++rit) {
            int pos = *rit;
            int match_length = longest_match(buf + pos, buf + i, std::min(static_cast<size_t>(MaxMatchLength), size - i));
            if (match_length > length) {
                length = match_length;
                distance = i - pos;
                xassert(3 <= length && length <= MaxMatchLength, "invalid match length (too long): %d", length);
                xassert(0 <= distance && distance <= MaxMatchDistance, "invalid distance (too far): %d", distance);
            }
            if (length >= nice_length || iter++ >= max_chain) {
                TRACE("exceeded match or chain length: match_length=%d chain_length=%d", length, iter);
                break;
            }
        }
        locs.push_back(i);
        if (length >= 3) {
            TRACE("using match: len=%d dist=%d str=\"%.*s\"", length, distance, length, &buf[i - distance]);
            for (int j = 1; j < length; ++j) {
                if (i + j + 2 >= size) {
                    break;
                }
                h = update_hash(h, buf[i + j + 2]);
                CHECK_HASH(i + j);
                ht[h].push_back(i + j);
            }
            i += length;
            tally_dst_len(distance, length);
        } else {
            tally_lit(buf[i]);
            i += 1;
        }
    }
    for (; i < size; ++i) {
        tally_lit(buf[i]);
    }

    TRACE("--- DST CODE COUNTS")
    for (auto&& [dst, cnt] : dst_counts) {
        TRACE("%d: %d", dst, cnt);
    }
    TRACE("--- END DST CODE COUNTS")

    return finish_up(lits, dsts, lit_counts, dst_counts);
}

int64_t calculate_header_cost(const Tree& htree, const std::vector<int>& hcodes, int n_hcodelens) {
    int64_t cost = 5 + 5 + 4;
    cost += 3 * n_hcodelens;
    for (auto hcode : hcodes) {
        cost += htree.codelens[hcode];
        cost += header_extra_bits[hcode];
    }
    return cost;
}

void compress_block(const uint8_t* const buf, size_t size, uint8_t bfinal, bool use_fast, int compression_level, BitWriter& out, int block_number) {
    auto* analyzer = use_fast ? analyze_block : analyze_block_lazy;
    auto& config = configs[compression_level];
    // analyze_block(buf, size, config);
    auto&& [codelens, hlit, hdist, lits, dsts, fix_cost, dyn_cost] = analyzer(buf, size, config);
    auto&& [hcodes, hextra, htree] = make_header_tree(codelens);
    auto&& [header_data, hclen] = make_header_tree_data(htree);
    auto hdr_cost = calculate_header_cost(htree, hcodes, hclen);
    // TODO(peter): better way to detect this?
    bool is_possible = std::all_of(htree.codelens.begin(), htree.codelens.end(),
                                   [](uint8_t codelen) { return codelen <= MaxHeaderCodeLength; });
    int64_t nc_cost = 5 + 16 + 16 + 8 * size;       // "Header Block flush" + LEN + NLEN + `LEN` bytes
    const char* compress_type = nullptr;            // TEMP TEMP
    uint64_t before = 0, after = 0, hdr_after = 0;  // TEMP TEMP

    // TEMP TEMP -- 3 bits for the header, can delete this later because same for all
    dyn_cost += 3;
    fix_cost += 3;
    nc_cost += 3;

    auto tot_dyn_cost = is_possible ? hdr_cost + dyn_cost : INT64_MAX;

    if (nc_cost < fix_cost && nc_cost < tot_dyn_cost) {
        before = hdr_after = out.total_written;
        blkwrite_no_compression(buf, size, bfinal, out);
        after = out.total_written;
        compress_type = "No Compression";
    } else if (tot_dyn_cost < fix_cost) {
        static uint16_t codes[MaxNumCodes + 1];  // TODO: figure out where to put this data
        init_huffman_tree(&codelens[0], hlit, &codes[0]);
        init_huffman_tree(&codelens[hlit], hdist, &codes[hlit]);
        xassert(257 <= hlit && hlit <= 286, "hlit = %zu", hlit);
        xassert(1 <= hdist && hdist <= 32, "hdist = %zu", hdist);
        xassert(4 <= hclen && hclen <= 19, "hclen = %d", hclen);

        before = out.total_written;
        uint8_t block_type = static_cast<uint8_t>(BType::DYNAMIC_HUFFMAN);
        out.write_bits(bfinal, 1);
        out.write_bits(block_type, 2);
        out.write_bits(hlit - 257, 5);
        out.write_bits(hdist - 1, 5);
        out.write_bits(hclen - 4, 4);

        // header tree code lengths
        for (int i = 0; i < hclen; ++i) {
            out.write_bits(header_data[i], 3);
        }

        // literal and distance code lengths
        for (size_t i = 0; i < hcodes.size(); ++i) {
            auto hcode = hcodes[i];
            uint16_t huff_code = htree.codes[hcode];
            int n_bits = htree.codelens[hcode];
            assert(n_bits > 0);
            out.write_bits(huff_code, n_bits);
            switch (hcode) {
            case 16:
                xassert(3 <= hextra[i] && hextra[i] <= 6, "invalid hextra: %d", hextra[i]);
                out.write_bits(hextra[i] - 3, 2);
                break;
            case 17:
                xassert(3 <= hextra[i] && hextra[i] <= 10, "invalid hextra: %d", hextra[i]);
                out.write_bits(hextra[i] - 3, 3);
                break;
            case 18:
                xassert(11 <= hextra[i] && hextra[i] <= 138, "invalid hextra: %d", hextra[i]);
                out.write_bits(hextra[i] - 11, 7);
                break;
            default:
                break;
            }
        }

        HuffTrees trees;
        trees.codes = &codes[0];
        trees.codelens = &codelens[0];
        trees.n_lits = hlit;
        trees.n_dists = hdist;
        hdr_after = out.total_written;
        write_block(lits, dsts, trees, out);
        after = out.total_written;
        compress_type = "Dynamic Huffman";
    } else {
        before = hdr_after = out.total_written;
        uint8_t block_type = static_cast<uint8_t>(BType::FIXED_HUFFMAN);
        out.write_bits(bfinal, 1);
        out.write_bits(block_type, 2);
        write_block(lits, dsts, fixed_tree, out);
        after = out.total_written;
        compress_type = "Fixed Huffman";
    }

    const char* bfinal_desc = bfinal ? " -- Final Block" : "";
    DEBUG("Block #%d Encoding: %s -- nc=%ld fix=%ld totdyn=%ld dyn=%ld hdr=%ld hdr_actual=%lu actual=%lu%s",
          block_number, compress_type, nc_cost, fix_cost, tot_dyn_cost, dyn_cost, hdr_cost, hdr_after - before,
          after - before, bfinal_desc);
}

int main(int argc, char** argv) {
    cxxopts::Options options("compress", "compress files using the LZ77 compression algorithm into the gzip format");
    options.add_options()
        ("f,fast", "use the non-lazy implementation")
        ("s,slow", "use the lazy implementation")
        ("l,level", "the level of compression to use", cxxopts::value<int>()->default_value("6"))
        ("input", "input filename", cxxopts::value<std::string>(), "FILE")
        ("output", "output filename", cxxopts::value<std::string>(), "OUTPUT")
        ("h,help", "Print usage")
        ;
    options.parse_positional({ "input", "output", "too many positional arguments" });
    auto args = options.parse(argc, argv);

    if (args.count("help")) {
        std::cerr << options.help() << std::endl;
        return 0;
    }

    if (!args.count("input")) {
        std::cerr << "Must specify input filename\n\n"
            << options.help()
            << std::endl;
        return 1;
    }
    if (args.count("fast") && args.count("slow")) {
        std::cerr << "Can't specify both fast and slow implementations\n\n"
            << options.help()
            << std::endl;
        return 1;
    }

    auto input_filename = args["input"].as<std::string>();
    auto output_filename = args.count("output") ? args["output"].as<std::string>() : input_filename + ".gz";
    bool use_fast = args.count("fast") || !args.count("slow");
    int compression_level = args["level"].as<int>();
    int max_compression = ARRSIZE(configs) - 1;
    compression_level = std::clamp(compression_level, 0, max_compression);

    printf("Input Filename : %s\n", input_filename.c_str());
    printf("Output Filename: %s\n", output_filename.c_str());
    printf("UseFast        : %s\n", use_fast ? "TRUE": "FALSE");
    printf("Level          : %d\n", compression_level);

    FileHandle fp = fopen(input_filename.c_str(), "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    FileHandle out = fopen(output_filename.c_str(), "wb");
    if (!out) {
        perror("fopen");
        exit(1);
    }
    BitWriter writer{out};
    int block_number = 0;

    // +---+---+---+---+---+---+---+---+---+---+
    // |ID1|ID2|CM |FLG|     MTIME     |XFL|OS | (more-->)
    // +---+---+---+---+---+---+---+---+---+---+

    uint8_t flags = static_cast<uint8_t>(Flags::FNAME);
    uint32_t mtime = 0;  // TODO: set mtime to seconds since epoch
    uint8_t xfl = 0;
    uint8_t os = 3;                                   // UNIX
    xwrite(&ID1_GZIP, sizeof(ID1_GZIP), 1, out);      // ID1
    xwrite(&ID2_GZIP, sizeof(ID2_GZIP), 1, out);      // ID2
    xwrite(&CM_DEFLATE, sizeof(CM_DEFLATE), 1, out);  // CM
    xwrite(&flags, sizeof(flags), 1, out);            // FLG
    xwrite(&mtime, sizeof(mtime), 1, out);            // MTIME
    xwrite(&xfl, sizeof(xfl), 1, out);                // XFL
    xwrite(&os, sizeof(os), 1, out);                  // OS

    //   +=========================================+
    //   |...original file name, zero-terminated...| (more-->)
    //   +=========================================+
    xwrite(input_filename.c_str(), input_filename.size() + 1, 1, out);  // FNAME

    std::fseek(fp, 0, SEEK_END);
    std::size_t filesize = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);

    uint32_t crc = calc_crc32(0, NULL, 0);
    // This contains the size of the original (uncompressed) input
    // data modulo 2^32.
    uint32_t isize = 0;

    auto* compress_fn = &compress_block;
    static char buf[BUFSIZE];
    size_t size = 0;
    size_t read;
    const uint8_t* pbuf = reinterpret_cast<const uint8_t*>(&buf[0]);  // TEMP: for convenience
    while ((read = fread(&buf[size], 1, BUFSIZE - size, fp)) > 0) {
        crc = calc_crc32(crc, reinterpret_cast<const uint8_t*>(&buf[size]), read);
        isize += read;
        size += read;
        assert(isize <= filesize);
        while (size >= BLOCKSIZE) {
            uint8_t bfinal = size <= BLOCKSIZE && isize == filesize;
            compress_fn(pbuf, BLOCKSIZE, bfinal, use_fast, compression_level, writer, block_number++);
            size -= BLOCKSIZE;
            memmove(&buf[0], &buf[BLOCKSIZE], size);
        }
    }
    if (ferror(fp)) {
        panic("error reading from file");
    }
    assert(feof(fp));

    // If the input file is empty, do need to write at least 1 block, which can
    // contain no data.
    assert(size < BLOCKSIZE);
    if (size > 0 || block_number == 0) {
        compress_fn(pbuf, size, 1, use_fast, compression_level, writer, block_number++);
    }
    writer.flush();

    DEBUG("CRC32 = 0x%08x", crc);
    DEBUG("ISIZE = 0x%08x", isize);

    //   0   1   2   3   4   5   6   7
    // +---+---+---+---+---+---+---+---+
    // |     CRC32     |     ISIZE     |
    // +---+---+---+---+---+---+---+---+
    xwrite(&crc, sizeof(crc), 1, out);      // CRC32
    xwrite(&isize, sizeof(isize), 1, out);  // ISIZE

    return 0;
}
