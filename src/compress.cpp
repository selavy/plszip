#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <map>
#include <string>
#include <vector>

#define panic(fmt, ...)                                   \
    do {                                                  \
        fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); \
        exit(1);                                          \
    } while (0)

#define xassert(c, fmt, ...)                                     \
    do {                                                         \
        auto r = (c);                                            \
        if (!r) {                                                \
            fprintf(stderr, "ASSERT: " fmt "\n", ##__VA_ARGS__); \
            assert(0);                                           \
        }                                                        \
    } while (0)

#define DEBUG0(msg) fprintf(stderr, "DEBUG: " msg "\n");
#define DEBUG(fmt, ...) fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__);

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

#define BUFSIZE 2056
#define READSIZE 32
#define BLOCKSIZE 1024

constexpr uint16_t EmptySentinel = UINT16_MAX;
constexpr size_t MaxCodeLength = 16;

uint32_t crc_table[256];

void init_crc_table() {
    for (int n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

uint32_t calc_crc32(uint32_t crc, const char* buf, size_t len) {
    if (buf == NULL) return 0;
    crc = crc ^ 0xffffffffUL;
    for (size_t i = 0; i < len; ++i) {
        crc = crc_table[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffUL;
}

uint8_t ID1_GZIP = 31;
uint8_t ID2_GZIP = 139;
uint8_t CM_DEFLATE = 8;

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
    constexpr Code(uint16_t code_, uint16_t bits_) noexcept : code{code_}, bits{bits_} {}
    uint16_t code;
    uint16_t bits;
};

using Value = uint16_t;

using Tree = std::map<Value, Code>;

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

Tree init_huffman_tree(const uint16_t* code_lengths, size_t n) {
    constexpr size_t MaxCodes = 512; // TODO: why 1^9? should this be 1^8?
    size_t bl_count[MaxCodeLength];
    uint16_t next_code[MaxCodeLength];
    uint16_t codes[MaxCodes];

    if (!(n < MaxCodes)) {
        throw std::runtime_error{"too many code lengths"};
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the
    // number of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < n; ++i) {
        xassert(code_lengths[i] <= MaxCodeLength, "Unsupported bit length");
        ++bl_count[code_lengths[i]];
        max_bit_length = std::max<uint16_t>(code_lengths[i], max_bit_length);
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
            xassert((16 - __builtin_clz(codes[i])) <= code_lengths[i], "overflowed code length");
        }
    }

    Tree tree;
    auto n_values = static_cast<uint16_t>(n);
    for (uint16_t value = 0; value < n_values; ++value) {
        uint16_t code_length = code_lengths[value];
        if (code_length == 0) {
            continue;
        }
        uint16_t code = codes[value];
        tree.emplace(value, Code{flip_code(code, code_length), code_length});
        // tree.emplace(value, Code{code, code_length});
    }

    return tree;
}

std::pair<Tree, Tree> init_fixed_huffman_data() noexcept {
    std::vector<uint16_t> lit_codes;
    while (lit_codes.size() < 144) {
        lit_codes.push_back(8);
    }
    while (lit_codes.size() < 256) {
        lit_codes.push_back(9);
    }
    while (lit_codes.size() < 280) {
        lit_codes.push_back(7);
    }
    while (lit_codes.size() < 288) {
        lit_codes.push_back(8);
    }
    assert(lit_codes.size() == 288);
    auto lit_tree = init_huffman_tree(lit_codes.data(), lit_codes.size());

    std::vector<uint16_t> dst_codes(32, 5);
    assert(dst_codes.size() == 32);
    auto dst_tree = init_huffman_tree(dst_codes.data(), dst_codes.size());

    return std::make_pair(lit_tree, dst_tree);
}

//-------------------------------------------------------
//
// Working on implementing fixed huffman codes to replace
// where I'm always choosing non-compressed at the moment.
// However, 2 things:
//
// 1) I mistakenly was using the interface of fwrite() directly
//    from blkwrite_XXX(), but that isn't correct because a
//    block does NOT have to be byte aligned. So I need
//    a BitWriter interface.
//
// 2) Huffman codes are bit oriented so also need #1. However,
//    need to be extra careful that the bits are in the correct
//    order.
//
//-------------------------------------------------------

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

    BitWriter(FILE* fp) noexcept : out_{fp}, buff_{0}, bits_{0} {}

    void write_bits(uint16_t val, size_t n_bits) noexcept
    {
        DEBUG("Enter write_bits: n_bits=%2zu bits_=%2zu buff_=0x%08x", n_bits, bits_, buff_);
        assert(n_bits <= MaxCodeLength);
        auto room = BufferSizeInBits - bits_;
        if (room >= n_bits) {
            buff_ |= val << bits_;
            bits_ += n_bits;
        } else {
            DEBUG("Hitting side case!");
            auto n1 = std::min(room, n_bits);
            auto n2 = n_bits - n1;
            buff_ |= (val & _ones_mask(n1)) << bits_;
            // buff_ |= (val >> n2) << bits_;
            bits_ += n1;
            write_full_buffer();
            // buff_ |= (val & _ones_mask(n2)) << bits_; // TODO(peter): bits_ == 0 at this point
            buff_ |= (val >> n1) << bits_;
            bits_ += n2;
        }
        assert(bits_ <= BufferSizeInBits);
        DEBUG("Exit  write_bits: n_bits=%2zu bits_=%2zu buff_=0x%08x", n_bits, bits_, buff_);
    }

    void write(const void* p, size_t size) noexcept {
        flush();
        xwrite(p, 1, size, out_);
    }

    void flush() noexcept {
        auto n_bytes = (bits_ + 7) / 8;
        DEBUG("flush: bits_=%zu buff_=0x%08x n_bytes=%zu", bits_, buff_, n_bytes);
        xwrite(&buff_, (bits_ + 7) / 8, 1, out_);
        buff_ = 0;
        bits_ = 0;
    }

    void write_full_buffer() noexcept {
        assert(bits_ == BufferSizeInBits);
        DEBUG("write_full_buffer: bits_=%zu buff_=0x%08x", bits_, buff_);
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
    FILE*  out_ = nullptr;

    constexpr static size_t BufferSizeInBits = 32;
    static_assert((sizeof(Buffer) * CHAR_BIT) >= BufferSizeInBits);
};

void blkwrite_no_compression(const char* buffer, size_t size, uint8_t bfinal, BitWriter& out) {
    uint8_t btype = static_cast<uint8_t>(BType::NO_COMPRESSION);
    uint8_t blkhdr = bfinal | (btype << 1);
    // TODO: technically need to force little endian
    uint16_t len = size;
    uint16_t nlen = len ^ 0xffffu;
    out.write(&blkhdr, sizeof(blkhdr));
    out.write(&len, sizeof(len));
    out.write(&nlen, sizeof(nlen));
    out.write(&buffer[0], size);
}

void blkwrite_fixed(const char* buf, size_t size, uint8_t bfinal, BitWriter& out) {
    auto&& [lits, dsts] = init_fixed_huffman_data();
    uint8_t block_type = static_cast<uint8_t>(BType::FIXED_HUFFMAN);
    out.write_bits(bfinal, 1);
    out.write_bits(block_type, 2);
    for (const char *p = buf, *end = buf + size; p != end; ++p) {
        auto it = lits.find(static_cast<uint16_t>(*p));
        if (it == lits.end()) {
            panic("no code for literal: %c (%d)", *p, *p);
        }
        auto&& [code, n_bits] = it->second;
        out.write_bits(code, n_bits);
        DEBUG("value(%c) => code(0x%02x) len=%u, flipped=0x%02x", *p, code, n_bits, flip_code(code, n_bits));
    }
    {
        auto it = lits.find(static_cast<uint16_t>(256));
        assert(it != lits.end());
        auto&& [code, n_bits] = it->second;
        out.write_bits(code, n_bits);
        DEBUG("value(%c) => code(0x%02x) len=%u, flipped=0x%02x", 256, code, n_bits, flip_code(code, n_bits));
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

    init_crc_table();

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

    char buf[BUFSIZE];
    size_t size = 0;
    size_t read;
    while ((read = fread(&buf[size], 1, READSIZE, fp)) > 0) {
        crc = calc_crc32(crc, &buf[size], read);
        isize += read;
        size += read;
        if (size > BLOCKSIZE) {
            // blkwrite_no_compression(buf, BLOCKSIZE, 0, writer);
            blkwrite_fixed(buf, BLOCKSIZE, 0, writer);
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
        DEBUG("Flushing final block");
        // blkwrite_no_compression(buf, size, 1, writer);
        blkwrite_fixed(buf, size, 1, writer);
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
