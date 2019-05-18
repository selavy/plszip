#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <libgen.h>
#include <cassert>
#include <memory>

#define panic(fmt, ...) do { fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); exit(1); } while(0)

#define xassert(c, fmt, ...) do { auto r = (c); if (!r) { fprintf(stderr, "ASSERT: " fmt "\n", ##__VA_ARGS__); assert(0); } } while(0)

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

#define DEBUG(fmt, ...) fprintf(stderr, "DBG: " fmt "\n", ##__VA_ARGS__)

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

struct BitReader
{
    BitReader(FILE* f) noexcept : fp(f), index(bufsize()), buffer(0) {}

    // TODO: error handling
    bool read_bit() noexcept
    {
        if (index >= bufsize()) {
            assert(index == bufsize());
            if (fread(&buffer, sizeof(buffer), 1, fp) != 1) {
                panic("BitReader: ERR: short read");
            }
            index = 0;
        }
        bool result = (buffer & (1u << index)) != 0;
        ++index;
        return result;
    }

    uint16_t read_bits(size_t nbits) noexcept
    {
        assert(nbits <= 16);
        uint16_t result = 0;
        for (size_t i = 0; i < nbits; ++i) {
            result |= (read_bit() ? 1u : 0u) << i;
        }
        return result;
    }

    void read_aligned_to_buffer(uint8_t* buf, size_t bytes) noexcept
    {
        // precondition: either should know are aligned to byte boundary, or
        //               need to have called flush_byte() before calling this
        assert(index % 8 == 0);
        // XXX: could call flush_byte() if not aligned... does hide that it is
        //      throwing away bits potentially.

        // VERSION 0
        for (size_t i = 0; i < bytes; ++i) {
            buf[i] = read_bits(8);
        }
    }

    size_t bufsize() const noexcept { return sizeof(buffer)*8; }

    // force the next call to read_bit/s to grab another byte
    void flush_byte() noexcept
    {
        auto bits_used_in_byte = index % 8;
        if (bits_used_in_byte > 0) {
            index += 8 - bits_used_in_byte;
        }
        assert((index % 8 == 0) && "index should be on byte boundary");
    }

    FILE*    fp;
    uint32_t index;
    uint32_t buffer;
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

bool init_huffman_tree(std::vector<uint16_t>& tree, const uint16_t* code_lengths, size_t n)
{
    constexpr size_t MaxCodes = 512;
    constexpr size_t MaxBitLength = 16;
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    static uint16_t codes[MaxCodes];

    if (!(n < MaxCodes)) {
        assert((n < MaxCodes) && "code lengths too long");
        return false;
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the number
    // of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < n; ++i) {
        assert(code_lengths[i] <= MaxBitLength && "Unsupported bit length");
        ++bl_count[code_lengths[i]];
        max_bit_length = std::max<uint16_t>(code_lengths[i], max_bit_length);
    }
    bl_count[0] = 0;

    // 2) Find the numerical value of the smallest code for each code length:
    memset(&next_code[0], 0, sizeof(next_code));
    uint32_t code = 0;
    for (size_t bits = 1; bits <= max_bit_length; ++bits) {
        code = (code + bl_count[bits-1]) << 1;
        next_code[bits] = code;
    }

    // 3) Assign numerical values to all codes, using consecutive values for all codes of
    // the same length with the base values determined at step 2.  Codes that are never
    // used (which have a bit length of zero) must not be assigned a value.
    memset(&codes[0], 0, sizeof(codes));
    for (size_t i = 0; i < n; ++i) {
        if (code_lengths[i] != 0) {
            codes[i] = next_code[code_lengths[i]]++;
            assert((16 - __builtin_clz(codes[i])) <= code_lengths[i]);
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
            index = 2*index + isset;
        }
        assert((tree[index] == EmptySentinel) && "Assigned multiple values to same index");
        tree[index] = value;
    }
    assert(tree.size() == table_size);

    return true;
}

uint16_t read_huffman_value(const uint16_t* tree, size_t length, BitReader& reader)
{
    size_t index = 1;
    do {
        index *= 2;
        index += reader.read_bit() ? 1 : 0;
        assert(index < length && "invalid index");
    } while (tree[index] == EmptySentinel);
    return tree[index];
}

bool read_dynamic_header_tree(BitReader& reader, size_t hclen,
        std::vector<uint16_t>& header_tree)
{
    constexpr size_t NumCodeLengths = 19;
    constexpr static uint16_t order[NumCodeLengths] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    static uint16_t lengths[NumCodeLengths];
    memset(&lengths[0], 0, sizeof(lengths));
    for (size_t i = 0; i < hclen; ++i) {
        lengths[order[i]] = reader.read_bits(3);
    }
    return init_huffman_tree(header_tree, &lengths[0], NumCodeLengths);
}

void read_dynamic_huffman_trees(BitReader& reader,
        std::vector<uint16_t>& literal_tree, std::vector<uint16_t>& distance_tree)
{
    size_t hlit = reader.read_bits(5) + 257;
    size_t hdist = reader.read_bits(5) + 1;
    size_t hclen = reader.read_bits(4) + 4;
    size_t ncodes = hlit + hdist;

    std::vector<uint16_t> header_tree;
    if (!read_dynamic_header_tree(reader, hclen, header_tree)) {
        panic("failed to initialize dynamic huffman tree header.");
    }

    std::vector<uint16_t> dynamic_code_lengths;
    while (dynamic_code_lengths.size() < ncodes) {
        uint16_t value = read_huffman_value(header_tree.data(), header_tree.size(), reader);
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
                assert(0 && "branch should never be hit");
            }
            size_t repeat_times = reader.read_bits(nbits) + offset;
            dynamic_code_lengths.insert(dynamic_code_lengths.end(), repeat_times, repeat_value);
        } else {
            panic("invalid value: %u", value);
        }
    }
    assert(dynamic_code_lengths.size() == ncodes && "Went over the number of expected codes");
    assert(dynamic_code_lengths.size() > 256 && dynamic_code_lengths[256] != 0 && "invalid code -- missing end-of-block");

    if (!init_huffman_tree(literal_tree, dynamic_code_lengths.data(), hlit)) {
        panic("failed to initialize dynamic huffman tree");
    }
    assert((dynamic_code_lengths.size() - hlit) == hdist);
    if (!init_huffman_tree(distance_tree, dynamic_code_lengths.data() + hlit, dynamic_code_lengths.size() - hlit)) {
        panic("failed to initialize dynamic distance tree");
    }
}

void get_fixed_huffman_lengths(std::vector<uint16_t>& code_lengths)
{
    //   Lit Value    Bits        Codes
    //   ---------    ----        -----
    //     0 - 143     8          00110000 through
    //                            10111111
    //   144 - 255     9          110010000 through
    //                            111111111
    //   256 - 279     7          0000000 through
    //                            0010111
    //   280 - 287     8          11000000 through
    //                            11000111
    code_lengths.assign(288, 0);
    struct TableEntry {
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
            code_lengths[i] = xs[j].bits;
        }
    }
}

bool init_fixed_huffman_data(std::vector<uint16_t>& lit_tree, std::vector<uint16_t>& dist_tree)
{
    std::vector<uint16_t> code_lengths;
    get_fixed_huffman_lengths(code_lengths);
    if (!init_huffman_tree(lit_tree, code_lengths.data(), code_lengths.size())) {
        panic("failed to initialize fixed huffman tree.");
    }
    code_lengths.assign(32, 5);
    if (!init_huffman_tree(dist_tree, code_lengths.data(), code_lengths.size())) {
        panic("failed to initialize fixed distance huffman tree.");
    }
    return true;
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

    std::vector<uint16_t> literal_tree;
    std::vector<uint16_t> distance_tree;

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

    printf("GzipHeader:\n");
    printf("\tid1   = %u (0x%02x)\n", hdr.id1, hdr.id1);
    printf("\tid2   = %u (0x%02x)\n", hdr.id2, hdr.id2);
    printf("\tcm    = %u\n", hdr.cm);
    printf("\tflg   = %u\n", hdr.flg);
    printf("\tmtime = %u\n", hdr.mtime);
    printf("\txfl   = %u\n", hdr.xfl);
    printf("\tos    = %u\n", hdr.os);

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
        printf("XLEN = %u\n", xlen);
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
    printf("Original Filename: '%s'\n", fname.c_str());

    std::string fcomment = "<none>";
    if ((hdr.flg & static_cast<uint8_t>(Flags::FCOMMENT)) != 0) {
        // +===================================+
        // |...file comment, zero-terminated...| (more-->)
        // +===================================+
        if (!read_null_terminated_string(fp, fcomment)) {
            panic("failed to read FCOMMENT.");
        }
    }
    printf("File comment: '%s'\n", fcomment.c_str());

    uint16_t crc16 = 0;
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
        if (fread(&crc16, sizeof(crc16), 1, fp) != 1) {
            panic("failed to read CRC16.");
        }
    }
    printf("CRC16: %u (0x%04X)\n", crc16, crc16);

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
    std::vector<uint8_t>  write_buffer;
    BitReader reader(fp);
    uint8_t bfinal = 0;
    do {
        size_t write_length = 0;
        bfinal = reader.read_bit();
        BType btype = static_cast<BType>(reader.read_bits(2));
        if (btype == BType::NO_COMPRESSION) {
            DEBUG("Block Encoding: No Compression");
            // discard remaining bits in first byte
            reader.flush_byte();
            auto read2B_le = [&]() {
                uint16_t b1 = reader.read_bits(8);
                uint16_t b2 = reader.read_bits(8);
                return (b2 << 8) | b1;
            };
            uint16_t len  = read2B_le();
            uint16_t nlen = read2B_le();
            if ((len & 0xffff) != (nlen ^ 0xffff)) {
                panic("invalid stored block lengths: %u %u", len, nlen);
            }
            size_t start_index = write_buffer.size();
            write_buffer.insert(write_buffer.end(), len, '\0');
            reader.read_aligned_to_buffer(&write_buffer[start_index], len);
            write_length = len;
        } else if (btype == BType::FIXED_HUFFMAN || btype == BType::DYNAMIC_HUFFMAN) {
            // if compressed with dynamic Huffman codes
            //    read representation of code trees (see
            //       subsection below)
            // loop (until end of block code recognized)
            //    decode literal/length value from input stream
            //    if value < 256
            //       copy value (literal byte) to output stream
            //    otherwise
            //       if value = end of block (256)
            //          break from loop
            //       otherwise (value = 257..285)
            //          decode distance from input stream
            //          move backwards distance bytes in the output
            //          stream, and copy length bytes from this
            //          position to the output stream.
            // end loop

            if (btype == BType::FIXED_HUFFMAN) {
                DEBUG("Block Encoding: Fixed Huffman");
                init_fixed_huffman_data(literal_tree, distance_tree);
            } else {
                DEBUG("Block Encoding: Dynamic Huffman");
                read_dynamic_huffman_trees(reader, literal_tree, distance_tree);
            }

            for (;;) {
                uint16_t value = read_huffman_value(
                        literal_tree.data(),
                        literal_tree.size(),
                        reader);
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
                    size_t distance_code = read_huffman_value(
                            distance_tree.data(),
                            distance_tree.size(),
                            reader);
                    assert((distance_code < 32) && "invalid distance code");
                    size_t base_distance = DISTANCE_BASES[distance_code];
                    size_t extra_distance = reader.read_bits(
                            DISTANCE_EXTRA_BITS[distance_code]);
                    size_t distance = base_distance + extra_distance;
                    if (distance >= write_buffer.size()) {
                        panic("invalid distance: %zu >= %zu",
                                distance, write_buffer.size());
                    }
                    size_t start = write_buffer.size() - distance;
                    for (size_t i = 0; i < length; ++i) {
                        write_buffer.push_back(write_buffer[start + i]);
                    }
                    write_length += length;
                } else {
                    panic("invalid fixed huffman value: %u", value);
                }
            }
        } else {
            panic("unsupported block encoding: %u", (uint8_t)btype);
        }

        if (write_length > 0) {
            size_t index = write_buffer.size() - write_length;
            if (fwrite(&write_buffer[index], write_length, 1, output) != 1) {
                panic("short write");
            }
        }
        constexpr size_t MaxLookbackDistance = (1u << 15);
        int64_t overflow = write_buffer.size() - MaxLookbackDistance;
        if (overflow > 0) {
            write_buffer.erase(write_buffer.begin(), write_buffer.begin() + overflow);
        }

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
