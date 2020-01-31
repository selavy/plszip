#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

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

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

#define BUFSIZE 2056
#define READSIZE 32
#define BLOCKSIZE 1024

constexpr uint16_t EmptySentinel = UINT16_MAX;

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
    if (fwrite(ptr, size, nmemb, stream) != nmemb) panic("short write");
}

bool init_huffman_tree(std::vector<uint16_t>& tree,
                       const uint16_t* code_lengths, size_t n) {
    constexpr size_t MaxCodes = 512;
    constexpr size_t MaxBitLength = 16;
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    static uint16_t codes[MaxCodes];

    if (!(n < MaxCodes)) {
        xassert(n < MaxCodes, "code lengths too long");
        return false;
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the
    // number of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < n; ++i) {
        xassert(code_lengths[i] <= MaxBitLength, "Unsupported bit length");
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
            xassert((16 - __builtin_clz(codes[i])) <= code_lengths[i],
                    "overflowed code length");
        }
    }

    // Table size is 2**(max_bit_length + 1)
    size_t table_size = 1u << (max_bit_length + 1);
    tree.assign(table_size, EmptySentinel);
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
        xassert(tree[index] == EmptySentinel,
                "Assigned multiple values to same index");
        tree[index] = value;
    }
    assert(tree.size() == table_size);

    return true;
}

void init_fixed_huffman_data(std::vector<uint16_t>& lit_tree,
                             std::vector<uint16_t>& dist_tree) noexcept {
    static uint16_t codes[288];

    // Literal Tree

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

    if (!init_huffman_tree(lit_tree, &codes[0], 288)) {
        panic("failed to initialize fixed huffman tree.");
    }

    // Distance Tree

    for (size_t i = 0; i < 32; ++i) {
        codes[i] = 5;
    }

    if (!init_huffman_tree(dist_tree, &codes[0], 32)) {
        panic("failed to initialize fixed distance huffman tree.");
    }
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

struct BitWriter
{
    BitWriter(FILE* fp) noexcept : out{fp} {}
    void write(uint8_t x, size_t bits) noexcept
    {
        // REVISIT(peter) :definitely not the fastest implementation
        for (size_t i = 0; i < bits; ++i) {
            if (idx == bufsize()) {
                flush();
            }
            buf |= 
        }
    }
    size_t bufsize() noexcept const { return 8*sizeof(buf); }
    void flush() noexcept {
        xwrite(&buf, 1, 1, out);
        buf = 0;
        idx = 0;
    }

    size_t  idx = 0;
    uint8_t buf = 0;
    FILE*   out;
};

void blkwrite_no_compression(const char* buffer, size_t size, uint8_t bfinal,
                             FILE* fp) {
    uint8_t btype = static_cast<uint8_t>(BType::NO_COMPRESSION);
    uint8_t blkhdr = bfinal | (btype << 1);
    uint16_t len = size;
    uint16_t nlen = len ^ 0xffffu;
    xwrite(&blkhdr, sizeof(blkhdr), 1, fp);
    xwrite(&len, sizeof(len), 1, fp);
    xwrite(&nlen, sizeof(nlen), 1, fp);
    xwrite(&buffer[0], 1, size, fp);
}

void blkwrite_fixed(const char* buf, size_t size, uint8_t bfinal, FILE* fp) {
    // static char outbuf[BLOCKSIZE];
    std::vector<uint16_t> literal_tree;
    std::vector<uint16_t> distance_tree;
    init_fixed_huffman_data(literal_tree, distance_tree);
    for (const char *p = buf, *end = buf + size; p != end; ++p) {
        auto it =
            std::find(literal_tree.begin(), literal_tree.end(), (uint16_t)*p);
        assert(it != literal_tree.end());
        size_t code = std::distance(literal_tree.begin(), it);
        printf("CODE(%c) = %u\n", *p, code);
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
            // blkwrite_no_compression(buf, BLOCKSIZE, 0, out);
            blkwrite_fixed(buf, BLOCKSIZE, 0, out);
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
        // blkwrite_no_compression(buf, size, 1, out);
        blkwrite_fixed(buf, size, 1, out);
    }

    //   0   1   2   3   4   5   6   7
    // +---+---+---+---+---+---+---+---+
    // |     CRC32     |     ISIZE     |
    // +---+---+---+---+---+---+---+---+
    xwrite(&crc, sizeof(crc), 1, out);      // CRC32
    xwrite(&isize, sizeof(isize), 1, out);  // ISIZE

    return 0;
}
