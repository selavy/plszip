#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <libgen.h>
#include <cassert>
#include <memory>

// TEMP TEMP
#include "fixed_huffman_trees.old.h"

#define BUFFERSZ size_t(1u << 15)

#define panic(fmt, ...) do { fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); exit(1); } while(0)

#ifdef NDEBUG
#define xassert(c, fmt, ...)
#define DEBUG(fmt, ...)
#else
#define xassert(c, fmt, ...)                                     \
    do {                                                         \
        if (!(c)) {                                              \
            fprintf(stderr, "ASSERT: " fmt "\n", ##__VA_ARGS__); \
            assert(0);                                           \
        }                                                        \
    } while (0)
#define DEBUG(fmt, ...) fprintf(stderr, "DBG: " fmt "\n", ##__VA_ARGS__)
#endif

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

#define INFO(fmt, ...) fprintf(stdout, "INFO: " fmt "\n", ##__VA_ARGS__)

//  ID1 (IDentification 1)
//  ID2 (IDentification 2)
//     These have the fixed values ID1 = 31 (0x1f, \037), ID2 = 139
//     (0x8b, \213), to identify the file as being in gzip format.
const uint8_t ID1_GZIP = 31;
const uint8_t ID2_GZIP = 139;

struct FileHandle
{
    FileHandle(FILE* f = nullptr) noexcept : fp(f) {}
    FileHandle(FileHandle&) noexcept = delete;
    FileHandle& operator=(FileHandle&) noexcept = delete;
    FileHandle(FileHandle&& rhs) noexcept : fp(rhs.fp) { rhs.fp = nullptr; }
    FileHandle& operator=(FileHandle&& rhs) noexcept { fp = rhs.fp; rhs.fp = nullptr; return *this; }
    ~FileHandle() noexcept { if (fp) { fclose(fp); } }
    operator FILE* () noexcept { return fp; }
    explicit operator bool() const noexcept { return fp != nullptr; }
    FILE* fp;
};

struct Reader
{
    uint8_t* start;
    uint8_t* end;
    uint8_t* cur;
    int      error;
    int    (*refill)(Reader* reader);
    void*    data;
};

// constexpr int REFILL_BUFFER_SIZE = 2048;
constexpr int REFILL_BUFFER_SIZE = (1u << 15);

struct RefillFileData
{
    FILE* fp;
    char  buf[REFILL_BUFFER_SIZE];
};

struct ReaderErrorData
{
    uint8_t buf;
} reader_error_data;

int refill_file(Reader* r)
{
    auto d = reinterpret_cast<RefillFileData*>(r->data);
    FILE* fp = d->fp;
    char* buf = d->buf;
    size_t rem = r->end - r->cur;
    memmove(r->start, r->cur, rem);
    size_t left = REFILL_BUFFER_SIZE - rem;
    size_t read = fread(buf, left, 1, fp);
    if (read > 0) {
        assert(rem + read <= REFILL_BUFFER_SIZE);
        r->start = reinterpret_cast<uint8_t*>(&buf[0]);
        r->cur   = reinterpret_cast<uint8_t*>(&buf[rem]);
        r->end   = reinterpret_cast<uint8_t*>(&buf[rem+read]);
        return 0;
    } else {
        reader_error_data.buf = 0;
        r->data = reinterpret_cast<void*>(&reader_error_data);
        r->start = reinterpret_cast<uint8_t*>(&reader_error_data.buf);
        r->cur = r->start;
        r->end = r->start + 1;
        r->error = ferror(fp);
        return r->error;
    }
}

struct BitReader
{
    BitReader(FILE* f) noexcept : fp(f), buff(0), bits(0) {}

    void nextbyte() noexcept
    {
        uint8_t byte;
        if (fread(&byte, sizeof(byte), 1, fp) != 1) {
            panic("ran out of input when more was expected");
        }
        buff += static_cast<uint32_t>(byte) << bits;
        bits += 8;
    }

    uint16_t peek(size_t nbits) noexcept
    {
        xassert(nbits <= bits, "tried to peek %zu bits, but only have %zu bits", nbits, bits);
        return buff & ((1u << nbits) - 1);
    }

    void need(size_t nbits) noexcept
    {
        while (bits < nbits)
            nextbyte();
    }

    bool read_bit() noexcept
    {
        return read_bits(1) ? true : false;
    }

    uint16_t read_bits(size_t nbits) noexcept
    {
        assert(0 <= nbits && nbits <= 15);
        need(nbits);
        auto result = peek(nbits);
        drop(nbits);
        return result;
    }

    void drop(size_t nbits) noexcept
    {
        xassert(nbits <= bits, "tried to drop %zu bits, but only have %zu", nbits, bits);
        buff >>= nbits;
        bits -= nbits;
    }

    void read_aligned_to_buffer(uint8_t* buf, size_t nbytes) noexcept
    {
        // precondition: either should know are aligned to byte boundary, or
        //               need to have called flush_byte() before calling this
        xassert(bits % 8 == 0, "index should be byte-aligned");
        size_t bytes_left = std::min(bits / 8, nbytes);
        for (size_t i = 0; i < bytes_left; ++i) {
            *buf++ = peek(8);
            drop(8);
        }
        assert(buff == 0u);
        assert(bits == 0);
        nbytes -= bytes_left;
        if (nbytes > 0) {
            if (fread(buf, nbytes, 1, fp) != 1) {
                panic("BitReader: ERR: short read, tried to read %zu bytes", nbytes);
            }
        }
    }

    // force the next call to read_bit(s) to grab another byte
    void flush_byte() noexcept
    {
        buff >>= bits % 8;
        bits -= bits % 8;
    }

    FILE*    fp;
    uint32_t buff;
    size_t   bits;
};

class WriteBuffer {
public:
    WriteBuffer(uint32_t size)
        : _mask(_force_power_of_two(size) - 1)
        , _head(0)
        , _buffer(std::make_unique<uint8_t[]>(capacity()))
    {
        assert((capacity() & _mask) == 0);
    }

    size_t capacity() const noexcept { return _mask + 1; }

    size_t size() const noexcept { return capacity(); }

    void push_back(uint8_t value) noexcept {
        // NOTE: the back location would normally be `(_head + size()) % capacity`,
        // but the buffer is always "full", in this case that will always be _head.
        _buffer[_head] = value;
        _head = _get_index(_head + 1);
    }

    template <class Iter>
    void insert_at_end(Iter first, Iter last) noexcept {
        // TODO: optimize
        while (first != last) {
            push_back(*first++);
        }
    }

    uint8_t& operator[](size_t index) noexcept {
        return _buffer[_get_index(_head + index)];
    }

    const uint8_t& operator[](size_t index) const noexcept {
        return _buffer[_get_index(_head + index)];
    }

    const uint8_t* data() const noexcept {
        return _buffer.get();
    }

    size_t index_of(size_t index) const noexcept {
        return _get_index(_head + index);
    }

private:
    static uint32_t _force_power_of_two(uint32_t x) noexcept {
        x = std::max(x, 8u);
        x--;
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        x++;
        assert(x > 0);
        assert((x & (x -1)) == 0);
        return x;
    }

    uint32_t _get_index(uint32_t index) const noexcept {
        uint32_t rv = index & _mask;
        assert(rv < capacity());
        return rv;
    }

    uint32_t                   _mask; // == capacity - 1
    uint32_t                   _head;
    std::unique_ptr<uint8_t[]> _buffer;
};

//  +---+---+---+---+---+---+---+---+---+---+
//  |ID1|ID2|CM |FLG|     MTIME     |XFL|OS | (more-->)
//  +---+---+---+---+---+---+---+---+---+---+
struct GzipHeader
{
    uint8_t id1;
    uint8_t id2;
    uint8_t cm;
    uint8_t flg;
    uint32_t mtime;
    uint8_t xfl;
    uint8_t os;
} __attribute__((packed));

// bit 0   FTEXT
// bit 1   FHCRC
// bit 2   FEXTRA
// bit 3   FNAME
// bit 4   FCOMMENT
// bit 5   reserved
// bit 6   reserved
// bit 7   reserved
enum class Flags : uint8_t
{
    FTEXT    = 1u << 0,
    FHCRC    = 1u << 1,
    FEXTRA   = 1u << 2,
    FNAME    = 1u << 3,
    FCOMMENT = 1u << 4,
    RESERV1  = 1u << 5,
    RESERV2  = 1u << 6,
    RESERV3  = 1u << 7,
};

// Operating System
//      0 - FAT filesystem (MS-DOS, OS/2, NT/Win32)
//      1 - Amiga
//      2 - VMS (or OpenVMS)
//      3 - Unix
//      4 - VM/CMS
//      5 - Atari TOS
//      6 - HPFS filesystem (OS/2, NT)
//      7 - Macintosh
//      8 - Z-System
//      9 - CP/M
//     10 - TOPS-20
//     11 - NTFS filesystem (NT)
//     12 - QDOS
//     13 - Acorn RISCOS
//    255 - unknown
enum class OperatingSystem : uint8_t
{
    FAT          = 0,
    AMIGA        = 1,
    VMS          = 2,
    UNIX         = 3,
    VM_CMS       = 4,
    ATARI_TOS    = 5,
    HPFS         = 6,
    MACINTOSH    = 7,
    Z_SYSTEM     = 8,
    CP_M         = 9,
    TOPS_20      = 10,
    NTFS         = 11,
    QDOS         = 12,
    ACORN_RISCOS = 13,
    UNKNOWN      = 255,
};

// Each block of compressed data begins with 3 header bits containing the following data:
// first bit       BFINAL
// next 2 bits     BTYPE
struct BlockHeader
{
    uint8_t bfinal : 1;
    uint8_t btype  : 2;
};

// BTYPE specifies how the data are compressed, as follows:
// 00 - no compression
// 01 - compressed with fixed Huffman codes
// 10 - compressed with dynamic Huffman codes
// 11 - reserved (error)
enum class BType : uint8_t
{
    NO_COMPRESSION   = 0x0u,
    FIXED_HUFFMAN    = 0x1u,
    DYNAMIC_HUFFMAN  = 0x2u,
    RESERVED         = 0x3u,
};

const char* BTypeStr[] =
{
    "NO COMPRESSION",
    "FIXED HUFFMAN",
    "DYNAMIC HUFFMAN",
    "RESERVED",
};

struct HTree
{
    std::vector<uint16_t> codes;    // code  -> value
    std::vector<uint16_t> codelens; // value -> codelen
    size_t maxlen;                  // max bit length
};

bool read_null_terminated_string(FILE* fp, std::string& result)
{
    char c;
    result = "";
    for (;;) {
        if (fread(&c, sizeof(c), 1, fp) != 1) {
            return false;
        }
        if (c == '\0') {
            break;
        }
        result += c;
    }
    return true;
}

constexpr uint16_t EmptySentinel = UINT16_MAX;
const size_t LENGTH_BASE_CODE = 257;
const size_t LENGTH_EXTRA_BITS[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};

const size_t LENGTH_BASES[29] = {
      3,   4,   5,   6,   7,   8,   9,  10,
     11,  13,  15,  17,  19,  23,  27,  31,
     35,  43,  51,  59,  67,  83,  99, 115,
    131, 163, 195, 227, 258,
};
static_assert(ARRSIZE(LENGTH_EXTRA_BITS) == ARRSIZE(LENGTH_BASES), "");

const size_t DISTANCE_EXTRA_BITS[32] = {
     0,  0,  0,  0,  1,  1,  2,  2,
     3,  3,  4,  4,  5,  5,  6,  6,
     7,  7,  8,  8,  9,  9, 10, 10,
    11, 11, 12, 12, 13, 13,  0,  0,
};

const size_t DISTANCE_BASES[32] = {
       1,    2,    3,     4,     5,     7,    9,   13,
      17,   25,   33,    49,    65,    97,  129,  193,
     257,  385,  513,   769,  1025,  1537, 2049, 3073,
    4097, 6145, 8193, 12289, 16385, 24577,    0,    0,
};

static const unsigned char BitReverseTable256[256] = 
{
#   define R2(n)     n,     n + 2*64,     n + 1*64,     n + 3*64
#   define R4(n) R2(n), R2(n + 2*16), R2(n + 1*16), R2(n + 3*16)
#   define R6(n) R4(n), R4(n + 2*4 ), R4(n + 1*4 ), R4(n + 3*4 )
    R6(0), R6(2), R6(1), R6(3)
};

uint16_t flip_u16(uint16_t v) noexcept
{
    return
        (BitReverseTable256[(v >> 0) & 0xff] << 8) |
        (BitReverseTable256[(v >> 8) & 0xff] << 0) ;
}

uint16_t flip_code(uint16_t code, size_t codelen)
{
    assert(0 < codelen && codelen <= 16);
    code = flip_u16(code);
    code >>= (16 - codelen);
    return code;
}

void init_huffman_tree(HTree& tree)
{
    constexpr size_t MaxCodes = 512;
    constexpr size_t MaxBitLength = 16;
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    static uint16_t codes[MaxCodes];
    size_t max_bit_length = 0;

    const std::vector<uint16_t>& code_lengths = tree.codelens;
    const size_t n = code_lengths.size();

    xassert(n < MaxCodes, "code lengths too long");

    { // 1) Count the number of codes for each code length. Let bl_count[N] be the number
        // of codes of length N, N >= 1.
        memset(&bl_count[0], 0, sizeof(bl_count));
        for (size_t i = 0; i < n; ++i) {
            xassert(code_lengths[i] <= MaxBitLength, "Unsupported bit length");
            ++bl_count[code_lengths[i]];
            max_bit_length = std::max<uint16_t>(code_lengths[i], max_bit_length);
        }
        bl_count[0] = 0;
    }

    { // 2) Find the numerical value of the smallest code for each code length:
        memset(&next_code[0], 0, sizeof(next_code));
        uint32_t code = 0;
        for (size_t bits = 1; bits <= max_bit_length; ++bits) {
            code = (code + bl_count[bits-1]) << 1;
            next_code[bits] = code;
        }
    }

    { // 3) Assign numerical values to all codes, using consecutive values for all codes of
        // the same length with the base values determined at step 2.  Codes that are never
        // used (which have a bit length of zero) must not be assigned a value.
        memset(&codes[0], 0, sizeof(codes));
        for (size_t i = 0; i < n; ++i) {
            if (code_lengths[i] != 0) {
                codes[i] = next_code[code_lengths[i]]++;
                xassert((16 - __builtin_clz(codes[i])) <= code_lengths[i], "overflowed code length");
            }
        }
    }

    // 4) Generate dense table. This means that can read `max_bit_length` bits at a
    // time, and do a lookup immediately; should then use `code_lengths` to
    // determine how many of the peek'd bits should be removed.
    size_t tablesz = 1u << max_bit_length;
    tree.codes.assign(tablesz, EmptySentinel);
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
            uint16_t flipped = flip_code(code, max_bit_length);
            xassert(tree.codes[flipped] == EmptySentinel, "reused index: %u", flipped);
            tree.codes[flipped] = value;
            ++code;
        }
    }
    tree.maxlen = max_bit_length;
}

uint16_t read_huffman_value(BitReader& reader, HTree& tree)
{
    reader.need(tree.maxlen);
    auto bits = reader.peek(tree.maxlen);
    auto value = tree.codes[bits];
    if (value == EmptySentinel)
        panic("invalid huffman bit pattern: 0x%04x (%zu bits)", bits, tree.maxlen);
    assert(0 <= value && value < tree.codelens.size());
    reader.drop(tree.codelens[value]);
    return value;
}

void read_dynamic_header_tree(BitReader& reader, size_t hclen, HTree& header_tree)
{
    constexpr size_t NumCodeLengths = 19;
    constexpr static uint16_t order[NumCodeLengths] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    header_tree.codes.clear(); // TEMP TEMP
    header_tree.codelens.assign(NumCodeLengths, 0); // TODO: should this be `hclen`?
    header_tree.maxlen = 0; // TEMP TEMP
    for (size_t i = 0; i < hclen; ++i) {
        header_tree.codelens[order[i]] = reader.read_bits(3);
    }
    init_huffman_tree(header_tree);
    assert(header_tree.maxlen != 0);
}

void read_dynamic_huffman_trees(BitReader& reader, HTree& literal_tree, HTree& distance_tree)
{
    reader.need(5 + 5 + 4);
    size_t hlit = reader.read_bits(5) + 257;
    size_t hdist = reader.read_bits(5) + 1;
    size_t hclen = reader.read_bits(4) + 4;
    size_t ncodes = hlit + hdist;

    HTree header_tree;
    read_dynamic_header_tree(reader, hclen, header_tree);

    std::vector<uint16_t> dynamic_code_lengths;
    dynamic_code_lengths.reserve(ncodes);
    while (dynamic_code_lengths.size() < ncodes) {
        uint16_t value = read_huffman_value(reader, header_tree);
        if (value <= 15) {
            dynamic_code_lengths.push_back(value);
        } else if (value <= 18) {
            size_t nbits;
            size_t offset;
            uint16_t repeat_value;
            if (value == 16) {
                nbits = 2;
                offset = 3;
                if (dynamic_code_lengths.empty()) {
                    panic("received repeat code 16 with no codes to repeat.");
                }
                repeat_value = dynamic_code_lengths.back();
            } else if (value == 17) {
                nbits = 3;
                offset = 3;
                repeat_value = 0;
            } else if (value == 18) {
                nbits = 7;
                offset = 11;
                repeat_value = 0;
            } else {
                xassert(0, "branch should never be hit");
            }
            size_t repeat_times = reader.read_bits(nbits) + offset;
            dynamic_code_lengths.insert(dynamic_code_lengths.end(), repeat_times, repeat_value);
        } else {
            panic("invalid value: %u", value);
        }
    }
    xassert(dynamic_code_lengths.size() == ncodes, "Went over the number of expected codes");
    xassert(dynamic_code_lengths.size() > 256 && dynamic_code_lengths[256] != 0, "invalid code -- missing end-of-block");

    literal_tree.codes.clear();
    literal_tree.codelens.assign(&dynamic_code_lengths[0], &dynamic_code_lengths[hlit]);
    literal_tree.maxlen = 0;
    init_huffman_tree(literal_tree);
    assert(literal_tree.maxlen != 0);
    xassert((dynamic_code_lengths.size() - hlit) == hdist,
            "invalid number distance codes: received %zu, expected %zu",
            dynamic_code_lengths.size() - hlit, hdist);
    distance_tree.codes.clear();
    distance_tree.codelens.assign(&dynamic_code_lengths[hlit], &dynamic_code_lengths[ncodes]);
    distance_tree.maxlen = 0;
    init_huffman_tree(distance_tree);
    assert(distance_tree.maxlen != 0);
}

void init_fixed_huffman_data(HTree& lit_tree, HTree& dist_tree) noexcept
{
    // Literal Tree
    lit_tree.codes.clear();
    lit_tree.codelens.assign(288, 0);
    lit_tree.maxlen = 0;
    struct LiteralCodeTableEntry {
        size_t start, stop, bits;
    } xs[] = {
        // start, stop, bits
        {      0,  143,    8, },
        {    144,  255,    9, },
        {    256,  279,    7, },
        {    280,  287,    8, },
    };
    for (size_t j = 0; j < ARRSIZE(xs); ++j) {
        for (size_t i = xs[j].start; i <= xs[j].stop; ++i) {
            lit_tree.codelens[i] = xs[j].bits;
        }
    }
    assert(lit_tree.codelens.size() == 288);
    init_huffman_tree(lit_tree);
    assert(lit_tree.maxlen != 0);

    // Distance Tree
    dist_tree.codes.clear();
    dist_tree.codelens.assign(32, 5);
    dist_tree.maxlen = 0;
    init_huffman_tree(dist_tree);
    assert(dist_tree.maxlen != 0);

    // TEMP TEMP
    assert(lit_tree.maxlen          == fixed_huffman_literals_maxlen);
    assert(lit_tree.codes.size()    == (sizeof(fixed_huffman_literals_codes)    / sizeof(uint16_t)));
    assert(lit_tree.codelens.size() == (sizeof(fixed_huffman_literals_codelens) / sizeof(uint16_t)));
    for (size_t i = 0; i < lit_tree.codes.size(); ++i) {
        assert(lit_tree.codes[i]    == fixed_huffman_literals_codes[i]);
    }
    for (size_t i = 0; i < lit_tree.codelens.size(); ++i) {
        assert(lit_tree.codelens[i] == fixed_huffman_literals_codelens[i]);
    }

    assert(dist_tree.maxlen          == fixed_huffman_distance_maxlen);
    assert(dist_tree.codes.size()    == (sizeof(fixed_huffman_distance_codes)    / sizeof(uint16_t)));
    assert(dist_tree.codelens.size() == (sizeof(fixed_huffman_distance_codelens) / sizeof(uint16_t)));
    for (size_t i = 0; i < dist_tree.codes.size(); ++i) {
        assert(dist_tree.codes[i]    == fixed_huffman_distance_codes[i]);
    }
    for (size_t i = 0; i < dist_tree.codelens.size(); ++i) {
        assert(dist_tree.codelens[i] == fixed_huffman_distance_codelens[i]);
    }
}

void flush_buffer(FILE* fp, const WriteBuffer& buffer, size_t nbytes) noexcept
{
    // the data in write buffer could wrap around the end of the ring buffer,
    // so do 2 writes, 1 for the data from the start_index to the end, and
    // 1 write for the rest at the beginning of the buffer.
    size_t start_index = buffer.index_of(buffer.size() - nbytes);
    size_t length1 = std::min(buffer.size() - start_index, nbytes);
    size_t length2 = nbytes - length1;
    if (length1 > 0) {
        if (fwrite(buffer.data() + start_index, length1, 1, fp) != 1) {
            panic("short write");
        }
    }
    if (length2 > 0) {
        if (fwrite(buffer.data(),               length2, 1, fp) != 1) {
            panic("short write");
        }
    }
}

int main(int argc, char** argv)
{
    const char* input_filename = argv[1];
    const char* output_filename;
    if (argc == 2) {
        output_filename = nullptr;
    } else if (argc == 3) {
        output_filename = argv[2];
    } else {
        fprintf(stderr, "Usage: %s [FILE] [OUT]\n", argv[0]);
        exit(0);
    }

    HTree literal_tree;
    HTree distance_tree;

    FileHandle fp = fopen(input_filename, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    FileHandle output = output_filename ? fopen(output_filename, "wb") : stdout;
    if (!output) {
        perror("fopen");
        exit(1);
    }

    //------------------------------------------------
    // Header
    //------------------------------------------------
    GzipHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        panic("fread: short read");
    }
    // TODO: handle big endian platform

    //  A compliant decompressor must check ID1, ID2, and CM, and
    //  provide an error indication if any of these have incorrect
    //  values.  It must examine FEXTRA/XLEN, FNAME, FCOMMENT and FHCRC
    //  at least so it can skip over the optional fields if they are
    //  present.  It need not examine any other part of the header or
    //  trailer; in particular, a decompressor may ignore FTEXT and OS
    //  and always produce binary output, and still be compliant.  A
    //  compliant decompressor must give an error indication if any
    //  reserved bit is non-zero, since such a bit could indicate the
    //  presence of a new field that would cause subsequent data to be
    //  interpreted incorrectly.

    INFO("GzipHeader:");
    INFO("\tid1   = %u (0x%02x)", hdr.id1, hdr.id1);
    INFO("\tid2   = %u (0x%02x)", hdr.id2, hdr.id2);
    INFO("\tcm    = %u", hdr.cm);
    INFO("\tflg   = %u", hdr.flg);
    INFO("\tmtime = %u", hdr.mtime);
    INFO("\txfl   = %u", hdr.xfl);
    INFO("\tos    = %u", hdr.os);

    if (hdr.id1 != ID1_GZIP) {
        panic("Unsupported identifier #1: %u.", hdr.id1);
    }
    if (hdr.id2 != ID2_GZIP) {
        panic("Unsupported identifier #2: %u.", hdr.id2);
    }

    if ((hdr.flg & static_cast<uint8_t>(Flags::FEXTRA)) != 0) {
        // +---+---+=================================+
        // | XLEN  |...XLEN bytes of "extra field"...| (more-->)
        // +---+---+=================================+
        uint16_t xlen;
        if (fread(&xlen, sizeof(xlen), 1, fp) != 1) {
            panic("short read on xlen.");
        }
        std::vector<uint8_t> buffer;
        buffer.assign(xlen, 0u);
        if (fread(buffer.data(), xlen, 1, fp) != 1) {
            panic("short read on FEXTRA bytes.");
        }
        panic("FEXTRA flag not supported.");
    }

    std::string fname = "<none>";
    if ((hdr.flg & static_cast<uint8_t>(Flags::FNAME)) != 0) {
        // +=========================================+
        // |...original file name, zero-terminated...| (more-->)
        // +=========================================+
        if (!read_null_terminated_string(fp, fname)) {
            panic("failed to read FNAME.");
        }
    }
    INFO("Original Filename: '%s'", fname.c_str());

    if ((hdr.flg & static_cast<uint8_t>(Flags::FCOMMENT)) != 0) {
        // +===================================+
        // |...file comment, zero-terminated...| (more-->)
        // +===================================+
        std::string fcomment = "<none>";
        if (!read_null_terminated_string(fp, fcomment)) {
            panic("failed to read FCOMMENT.");
        }
        INFO("File comment: '%s'", fcomment.c_str());
    }

    if ((hdr.flg & static_cast<uint8_t>(Flags::FHCRC)) != 0) {
        // +---+---+
        // | CRC16 |
        // +---+---+
        //
        // +=======================+
        // |...compressed blocks...| (more-->)
        // +=======================+
        //
        // 0   1   2   3   4   5   6   7
        // +---+---+---+---+---+---+---+---+
        // |     CRC32     |     ISIZE     |
        // +---+---+---+---+---+---+---+---+
        uint16_t crc16 = 0;
        if (fread(&crc16, sizeof(crc16), 1, fp) != 1) {
            panic("failed to read CRC16.");
        }
        INFO("CRC16: %u (0x%04X)", crc16, crc16);
    }

    // Reserved FLG bits must be zero.
    uint8_t mask = static_cast<uint8_t>(Flags::RESERV1) |
                   static_cast<uint8_t>(Flags::RESERV2) |
                   static_cast<uint8_t>(Flags::RESERV3);
    if ((hdr.flg & mask) != 0) {
        panic("reserved bits are not 0.");
    }

    // TODO: read time
    //  MTIME (Modification TIME)
    //     This gives the most recent modification time of the original
    //     file being compressed.  The time is in Unix format, i.e.,
    //     seconds since 00:00:00 GMT, Jan.  1, 1970.  (Note that this
    //     may cause problems for MS-DOS and other systems that use
    //     local rather than Universal time.)  If the compressed data
    //     did not come from a file, MTIME is set to the time at which
    //     compression started.  MTIME = 0 means no time stamp is
    //     available.

    // XFL (eXtra FLags)
    //    These flags are available for use by specific compression
    //    methods.  The "deflate" method (CM = 8) sets these flags as
    //    follows:
    //
    //       XFL = 2 - compressor used maximum compression,
    //                 slowest algorithm
    //       XFL = 4 - compressor used fastest algorithm

    // OS (Operating System)
    //    This identifies the type of file system on which compression
    //    took place.  This may be useful in determining end-of-line
    //    convention for text files.  The currently defined values are
    //    as follows:

    //------------------------------------------------
    // Read Compressed Data
    //------------------------------------------------
    WriteBuffer write_buffer(1u << 16);
    BitReader reader(fp);
    uint8_t bfinal = 0;
    int block_number = 0;
    size_t block_size = 0;
    do {
        block_size = 0;
        size_t write_length = 0;
        reader.need(1 + 2);
        bfinal = reader.read_bits(1);
        BType btype = static_cast<BType>(reader.read_bits(2));
        if (btype == BType::NO_COMPRESSION) {
            DEBUG("Block #%d Encoding: No Compression", block_number);
            reader.flush_byte();
            auto read2B_le = [&]() {
                reader.need(8 + 8);
                uint16_t b1 = reader.read_bits(8);
                uint16_t b2 = reader.read_bits(8);
                return (b2 << 8) | b1;
            };
            uint16_t len  = read2B_le();
            uint16_t nlen = read2B_le();
            DEBUG("len = %u nlen = %u", len, nlen);
            if ((len & 0xffff) != (nlen ^ 0xffff)) {
                panic("invalid stored block lengths: %u %u", len, nlen);
            }
            std::vector<uint8_t> temp_buffer;
            while (len >= BUFFERSZ) {
                temp_buffer.assign(BUFFERSZ, '\0');
                reader.read_aligned_to_buffer(&temp_buffer[0], BUFFERSZ);
                if (fwrite(&temp_buffer[0], BUFFERSZ, 1, output) != 1) {
                    panic("short write");
                }
                write_buffer.insert_at_end(temp_buffer.begin(), temp_buffer.end());
                len -= BUFFERSZ;
            }
            xassert(0 <= len && len < BUFFERSZ, "invalid length: %u", len);

            if (len > 0) {
                temp_buffer.assign(len, '\0');
                reader.read_aligned_to_buffer(&temp_buffer[0], len);
                write_buffer.insert_at_end(temp_buffer.begin(), temp_buffer.end());
            }
            write_length = len;
        } else if (btype == BType::FIXED_HUFFMAN || btype == BType::DYNAMIC_HUFFMAN) {
            if (btype == BType::FIXED_HUFFMAN) {
                DEBUG("Block #%d Encoding: Fixed Huffman", block_number);
                init_fixed_huffman_data(literal_tree, distance_tree);
            } else {
                DEBUG("Block #%d Encoding: Dynamic Huffman", block_number);
                read_dynamic_huffman_trees(reader, literal_tree, distance_tree);
            }

            for (;;) {
                uint16_t value = read_huffman_value(reader, literal_tree);
                if (value < 256) {
                    write_buffer.push_back(static_cast<uint8_t>(value));
                    ++write_length;
                } else if (value == 256) {
                    DEBUG("inflate: end of block found");
                    break;
                } else if (value <= 285) {
                    value -= LENGTH_BASE_CODE;
                    assert(value < ARRSIZE(LENGTH_EXTRA_BITS));
                    size_t base_length = LENGTH_BASES[value];
                    size_t extra_length = reader.read_bits(LENGTH_EXTRA_BITS[value]);
                    size_t length = base_length + extra_length;
                    xassert(length <= 258, "invalid length");
                    size_t distance_code = read_huffman_value(reader, distance_tree);
                    xassert(distance_code < 32, "invalid distance code");
                    size_t base_distance = DISTANCE_BASES[distance_code];
                    size_t extra_distance = reader.read_bits(
                            DISTANCE_EXTRA_BITS[distance_code]);
                    size_t distance = base_distance + extra_distance;
                    if (distance >= write_buffer.size()) {
                        panic("invalid distance: %zu >= %zu",
                                distance, write_buffer.size());
                    }
                    size_t index = write_buffer.size() - distance;
                    for (size_t i = 0; i < length; ++i) {
                        // NOTE: because the ring buffer is always full, the
                        // head is getting updated on every push_back, which
                        // means that it naturally increments the index that we
                        // are accessing.
                        write_buffer.push_back(write_buffer[index]);
                    }
                    write_length += length;

                    // flush buffer if sufficiently full
                    if (write_length > (1u << 12)) {
                        block_size += write_length;
                        flush_buffer(output, write_buffer, write_length);
                        write_length = 0;
                    }
                } else {
                    panic("invalid fixed huffman value: %u", value);
                }
            }
        } else {
            panic("unsupported block encoding: %u", (uint8_t)btype);
        }

        if (write_length > 0) {
            flush_buffer(output, write_buffer, write_length);
            block_size += write_length;
        }

        ++block_number;
        DEBUG("Block size = %zu", block_size);
    } while (bfinal == 0);

    //------------------------------------------------
    // Footer
    //------------------------------------------------

    //  CRC32 (CRC-32)
    //     This contains a Cyclic Redundancy Check value of the
    //     uncompressed data computed according to CRC-32 algorithm
    //     used in the ISO 3309 standard and in section 8.1.1.6.2 of
    //     ITU-T recommendation V.42.  (See http://www.iso.ch for
    //     ordering ISO documents. See gopher://info.itu.ch for an
    //     online version of ITU-T V.42.)
    //
    //  ISIZE (Input SIZE)
    //     This contains the size of the original (uncompressed) input
    //     data modulo 2^32.

    return 0;
}
