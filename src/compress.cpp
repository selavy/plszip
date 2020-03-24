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

#include "crc32.h"
#include "compress_tables.h"

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

constexpr size_t BUFSIZE = 2056;
constexpr size_t READSIZE = 2056;
constexpr size_t BLOCKSIZE = 2056;
constexpr size_t NumHeaderCodeLengths = 19;
constexpr size_t LiteralCodes = 256;  // [0, 255] doesn't include END_BLOCK code
constexpr size_t LengthCodes = 29;    // [257, 285]
constexpr size_t LitCodes = LiteralCodes + LengthCodes + 1;
constexpr size_t DistCodes = 30;  // [0, 29]
constexpr size_t MaxNumCodes = LitCodes + DistCodes;
constexpr size_t HeaderLengthBits = 3;
constexpr size_t MaxHeaderCodeLength = (1u << HeaderLengthBits) - 1;
constexpr size_t MaxMatchLength = 258;
constexpr size_t MaxMatchDistance = 32768;
constexpr size_t MaxBits = 15;
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

int get_distance_code(int value) {
    for (size_t i = 0; i < ARRSIZE(distance_info); ++i) {
        if (distance_info[i].start <= value && value <= distance_info[i].stop) {
            return distance_info[i].code;
        }
    }
    xassert(0, "invalid value: %d", value);
    return -1;
}

int get_distance_base(int value) {
    for (size_t i = 0; i < ARRSIZE(distance_info); ++i) {
        if (distance_info[i].start <= value && value <= distance_info[i].stop) {
            return distance_info[i].start;
        }
    }
    xassert(0, "invalid value: %d", value);
    return -1;
}

int get_distance_extra_bits(int value) {
    for (size_t i = 0; i < ARRSIZE(distance_info); ++i) {
        if (distance_info[i].start <= value && value <= distance_info[i].stop) {
            return distance_info[i].extra_bits;
        }
    }
    xassert(0, "invalid value: %d", value);
    return -1;
}

int get_length_code(int length) {
    assert(0 <= length && length < ARRSIZE(length_codes));
    return length_codes[length];
}

int get_length_base(int length) {
    assert(0 <= length && length < ARRSIZE(length_bases));
    return length_bases[length];
}

int get_length_extra_bits(int length) {
    assert(0 <= length && length < ARRSIZE(length_extra_bits));
    return length_extra_bits[length];
}

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
    std::vector<uint16_t> codes;    // [MaxNumCodes+1];
    std::vector<uint8_t>  codelens; // [MaxNumCodes+1];
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
        n.left = a->weight < b->weight ? a : b;
        n.right = a->weight < b->weight ? b : a;
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

    BitWriter(FILE* fp) noexcept : out_{fp}, buff_{0}, bits_{0} {}

    void write_bits(uint16_t val, size_t n_bits) noexcept {
        assert(n_bits <= MaxBits);
        if (bits_ == BufferSizeInBits) {
            write_full_buffer();
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
            write_full_buffer();
            assert(bits_ == 0);
            buff_ |= val >> n1;
            bits_ += n2;
        }
        assert(bits_ <= BufferSizeInBits);
    }

    void write(const void* p, size_t size) noexcept {
        flush();
        xwrite(p, 1, size, out_);
    }

    void flush() noexcept {
        auto n_bytes = (bits_ + 7) / 8;
        xwrite(&buff_, 1, n_bytes, out_);
        buff_ = 0;
        bits_ = 0;
    }

    void write_full_buffer() noexcept {
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
};

void blkwrite_no_compression(const char* buffer, size_t size, uint8_t bfinal, BitWriter& out) {
    uint8_t block_type = static_cast<uint8_t>(BType::NO_COMPRESSION);
    uint8_t btype = static_cast<uint8_t>(BType::NO_COMPRESSION);
    uint16_t len = size;
    uint16_t nlen = len ^ 0xffffu;
    out.write_bits(bfinal, 1);
    out.write_bits(block_type, 2);
    out.flush();
    // TODO: technically need to force little endian
    out.write(&len, sizeof(len));
    out.write(&nlen, sizeof(nlen));
    out.write(&buffer[0], size);
}

void write_block(const std::vector<int>& lits, const std::vector<int>& dsts, const std::vector<int>& lens,
                 const HuffTrees& tree, BitWriter& out) {
    assert(lits.size() == dsts.size() && lits.size() == lens.size());
    for (size_t i = 0; i < lits.size(); ++i) {
        auto lit = lits[i];
        xassert(0 <= lit && lit <= 285, "invalid literal: %d", lit);
        uint16_t lit_huff_code = tree.codes[lit];
        int lit_n_bits = tree.codelens[lit];
        xassert(lit_n_bits > 0, "invalid code length: %u", lit_n_bits);
        assert(1 <= lit_n_bits && lit_n_bits <= MaxBits);
        out.write_bits(lit_huff_code, lit_n_bits);
        if (lit >= 257) {
            auto len = lens[i];
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

CodeLengths make_header_tree_length_data(const Tree& tree) {
    constexpr std::array<int, NumHeaderCodeLengths> order = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                             11, 4,  12, 3, 13, 2, 14, 1, 15};
    CodeLengths results(order.size(), 0);
    for (size_t index = 0; index < order.size(); ++index) {
        results[index] = tree.codelens[order[index]];
    }
    while (results.size() > 4 && results.back() == 0) {
        results.pop_back();
    }
    assert(4 <= results.size() && results.size() <= 19);
    return results;
}

constexpr uint32_t update_hash(uint32_t current, char c) noexcept {
    constexpr uint32_t mask = (1u << 24) - 1;
    return ((current << 8) | c) & mask;
}

int longest_match(const char* wnd, const char* str, int max_length) {
    int i = 0;
    for (; i < max_length; ++i) {
        if (wnd[i] != str[i])
            break;
    }
    return i;
}

struct BlockResults {
    CodeLengths codelens;
    size_t hlit;
    size_t hdist;
    std::vector<int> lit_codes;
    std::vector<int> dst_vals;
    std::vector<int> len_vals;
    int64_t fix_cost;
    int64_t dyn_cost;
};

BlockResults analyze_block(const char* const buf, size_t size) {
    // TODO: add fast path for analyzing very small blocks. no point in even trying
    //       dynamic encoding in that case, and potentially gives optimization ability
    //       to know that there are at least N bytes of input

    DEBUG("analyze_block -- %zu", size);
    std::vector<int> lit_codes;
    std::vector<int> len_vals;
    std::vector<int> dst_vals;

    using Hash = std::tuple<char, char, char>;
    std::map<Hash, std::vector<int>> htable;
    std::map<int, int> lit_counts;
    std::map<int, int> dst_counts;
    // uint32_t h = 0;
    // if (size >= 2) {
    //     h = update_hash(h, buf[0]);
    //     h = update_hash(h, buf[1]);
    // }

    size_t i = 0;
    while (i + 3 < size) {
        xassert(i + 2 < size, "i=%zu size=%zu", i, size);
        // h = update_hash(h, buf[i + 2]);
        auto h = std::make_tuple(buf[i+0], buf[i+1], buf[i+2]);
        auto& locs = htable[h];
        int length = 2;
        int distance = 0;
        for (auto rit = locs.rbegin(); rit != locs.rend(); ++rit) {
            int pos = *rit;
            int match_length = longest_match(buf + pos, buf + i, std::min(MaxMatchLength, size - i));
            if (match_length > length) {
                length = match_length;
                distance = i - pos;
                xassert(3 <= length && length <= MaxMatchLength, "invalid match length (too long): %d", length);
                xassert(0 <= distance && distance <= MaxMatchDistance, "invalid distance (too far): %d", distance);
            }
        }
        locs.push_back(i);
        if (length >= 3) {
            // DEBUG("pos=%3zu len=%3d dist=%3d", i, length, distance);
            i += length;
            lit_codes.push_back(get_length_code(length));
            len_vals.push_back(length);
            dst_vals.push_back(distance);
            lit_counts[lit_codes.back()]++;
            dst_counts[get_distance_code(dst_vals.back())]++;
        } else {
            int value = static_cast<int>(*reinterpret_cast<const uint8_t*>(buf + i));
            lit_codes.push_back(value);
            len_vals.push_back(0);
            dst_vals.push_back(0);
            lit_counts[lit_codes.back()]++;
            i += 1;
        }
    }
    for (; i < size; ++i) {
        int value = static_cast<int>(*reinterpret_cast<const uint8_t*>(buf + i));
        lit_codes.push_back(value);
        len_vals.push_back(0);
        dst_vals.push_back(0);
        lit_counts[lit_codes.back()]++;
    }

    // TODO: remove this, shouldn't do dynamic encoding if the input is empty
    // edge case for when input is empty
    if (lit_counts.empty()) {
        assert(size == 0u);
        lit_counts[0] = 1;
    }

    // must have code for END_BLOCK
    lit_codes.push_back(256);
    dst_vals.push_back(0);
    len_vals.push_back(0);
    lit_counts[lit_codes.back()] = 1;

    assert(lit_codes.size() == dst_vals.size());
    assert(lit_codes.size() == len_vals.size());

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
        value += hlit;
        xassert(hlit <= value && value < codelens.size(), "invalid value: %d", value);
        xassert(1 <= codelen && codelen <= MaxBits, "invalid codelen: %d", codelen);
        codelens[value] = codelen;
    }
    assert(codelens[256] != 0);

    int64_t fix_cost = 0;
    int64_t dyn_cost = 0;
    for (auto&& [lit, count] : lit_counts) {
        dyn_cost += count * codelens[lit];
        fix_cost += count * fixed_codelens[lit];
    }
    for (auto&& [dst, count] : dst_counts) {
        dyn_cost += count * codelens[hlit + dst];
        fix_cost += count * fixed_codelens[NumFixedTreeLiterals + dst];
    }

    return {codelens, hlit, hdist, lit_codes, dst_vals, len_vals, fix_cost, dyn_cost};
}

int64_t calculate_header_cost(const Tree& htree, const std::vector<int>& hcodes, const CodeLengths& htree_codelens) {
    int64_t cost = 5 + 5 + 4;
    cost += 3 * htree_codelens.size();
    for (size_t i = 0; i < hcodes.size(); ++i) {
        cost += htree.codelens[hcodes[i]];
        switch (hcodes[i]) {
        case 16:
            cost += 2;
            break;
        case 17:
            cost += 3;
            break;
        case 18:
            cost += 7;
            break;
        default:
            break;
        }
    }
    return cost;
}

void compress_block(const char* buf, size_t size, uint8_t bfinal, BitWriter& out) {
    DEBUG("compress_block: bfinal=%s size=%zu", bfinal ? "TRUE" : "FALSE", size);
    auto&& [codelens, hlit, hdist, lits, dsts, lens, fix_cost, dyn_cost] = analyze_block(buf, size);
    auto&& [hcodes, hextra, htree] = make_header_tree(codelens);
    auto htree_length_data = make_header_tree_length_data(htree);
    auto hdr_cost = calculate_header_cost(htree, hcodes, htree_length_data);
    // TODO(peter): better way to detect this?
    bool is_possible = std::all_of(htree.codelens.begin(), htree.codelens.end(),
                                   [](uint8_t codelen) { return codelen <= MaxHeaderCodeLength; });
    auto tot_dyn_cost = is_possible ? hdr_cost + dyn_cost : INT64_MAX;
    int64_t nc_cost = 8 + 16 + 16 + 8*size; // "Header Block flush" + LEN + NLEN + `LEN` bytes

    DEBUG("dyn=%ld + hdr=%ld = totdyn=%ld; fix=%ld; nc=%ld", dyn_cost, hdr_cost, tot_dyn_cost, fix_cost, nc_cost);

    if (nc_cost < fix_cost && nc_cost < tot_dyn_cost) {
        DEBUG("Using NO_COMPRESSION")
        blkwrite_no_compression(buf, size, bfinal, out);
    } else if (tot_dyn_cost < fix_cost) {
        DEBUG("Using DYNAMIC_HUFFMAN");
        static uint16_t codes[MaxNumCodes + 1];   // TODO: figure out where to put this data
        init_huffman_tree(&codelens[0], hlit, &codes[0]);
        init_huffman_tree(&codelens[hlit], hdist, &codes[hlit]);
        auto hclen = htree_length_data.size();
        xassert(257 <= hlit && hlit <= 286, "hlit = %zu", hlit);
        xassert(1 <= hdist && hdist <= 32, "hdist = %zu", hdist);
        xassert(4 <= hclen && hclen <= 19, "hclen = %zu", hclen);

        DEBUG("hlit = %zu", hlit);
        DEBUG("hdist = %zu", hdist);
        DEBUG("hclen = %zu", hclen);

        uint8_t block_type = static_cast<uint8_t>(BType::DYNAMIC_HUFFMAN);
        out.write_bits(bfinal, 1);
        out.write_bits(block_type, 2);
        out.write_bits(hlit - 257, 5);
        out.write_bits(hdist - 1, 5);
        out.write_bits(hclen - 4, 4);

        // header tree code lengths
        for (auto codelen : htree_length_data) {
            out.write_bits(codelen, 3);
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
        write_block(lits, dsts, lens, trees, out);
    } else {
        DEBUG("Using FIXED_HUFFMAN");
        uint8_t block_type = static_cast<uint8_t>(BType::FIXED_HUFFMAN);
        out.write_bits(bfinal, 1);
        out.write_bits(block_type, 2);
        write_block(lits, dsts, lens, fixed_tree, out);
    }
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s [FILE]\n", argv[0]);
        exit(0);
    }
    std::string input_filename = argv[1];
    std::string output_filename = argc == 3 ? argv[2] : input_filename + ".gz";

    printf("Input Filename : %s\n", input_filename.c_str());
    printf("Output Filename: %s\n", output_filename.c_str());

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

    uint32_t crc = calc_crc32(0, NULL, 0);
    // This contains the size of the original (uncompressed) input
    // data modulo 2^32.
    uint32_t isize = 0;

    auto* compress_fn = &compress_block;
    static char buf[BUFSIZE];
    size_t size = 0;
    size_t read;
    while ((read = fread(&buf[size], 1, READSIZE, fp)) > 0) {
        crc = calc_crc32(crc, reinterpret_cast<const uint8_t*>(&buf[size]), read);
        isize += read;
        size += read;
        if (size >= BLOCKSIZE) {
            compress_fn(&buf[0], BLOCKSIZE, 0, writer);
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
    //
    // NOTE: it is compliant to have unnecessary empty blocks so in the very
    // rare case that `size` == 0, and isize wraps to exactly 0, will write
    // an unnecessary empty block, but that is OK.
    if (size > 0 || isize == 0) {
        compress_fn(&buf[0], size, 1, writer);
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
