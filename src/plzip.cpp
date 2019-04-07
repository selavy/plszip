#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <libgen.h>
#include <cassert>

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

// TODO: better names?
//  ID1 (IDentification 1)
//  ID2 (IDentification 2)
//     These have the fixed values ID1 = 31 (0x1f, \037), ID2 = 139
//     (0x8b, \213), to identify the file as being in gzip format.
const uint8_t ID1_GZIP = 31;
const uint8_t ID2_GZIP = 139;

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

void print_operating_system_debug(uint8_t os)
{
    const char* name;
    auto oss = static_cast<OperatingSystem>(os);
    switch (oss) {
        case OperatingSystem::FAT:
            name = "FAT";
            break;
        case OperatingSystem::AMIGA:
            name = "AMIGA";
            break;
        case OperatingSystem::VMS:
            name = "VMS";
            break;
        case OperatingSystem::UNIX:
            name = "UNIX";
            break;
        case OperatingSystem::VM_CMS:
            name = "VM_CMS";
            break;
        case OperatingSystem::ATARI_TOS:
            name = "ATARI_TOS";
            break;
        case OperatingSystem::HPFS:
            name = "HPFS";
            break;
        case OperatingSystem::MACINTOSH:
            name = "MACINTOSH";
            break;
        case OperatingSystem::Z_SYSTEM:
            name = "Z_SYSTEM";
            break;
        case OperatingSystem::CP_M:
            name = "CP_M";
            break;
        case OperatingSystem::TOPS_20:
            name = "TOPS_20";
            break;
        case OperatingSystem::NTFS:
            name = "NTFS";
            break;
        case OperatingSystem::QDOS:
            name = "QDOS";
            break;
        case OperatingSystem::ACORN_RISCOS:
            name = "ACORN_RISCOS";
            break;
        case OperatingSystem::UNKNOWN:
            name = "UNKNOWN";
            break;
        default:
            name = "unknown";
            break;
    }
    printf("Operating System: %s (%u)\n", name, os);
}

void print_flags_debug(uint8_t flags)
{
    printf("Flags: ");
    if ((flags & (uint8_t)Flags::FTEXT) != 0) {
        printf("FTEXT ");
    }
    if ((flags & (uint8_t)Flags::FHCRC) != 0) {
        printf("FHCRC ");
    }
    if ((flags & (uint8_t)Flags::FEXTRA) != 0) {
        printf("FEXTRA ");
    }
    if ((flags & (uint8_t)Flags::FNAME) != 0) {
        printf("FNAME ");
    }
    if ((flags & (uint8_t)Flags::FCOMMENT) != 0) {
        printf("FCOMMENT ");
    }
    if ((flags & (uint8_t)Flags::RESERV1) != 0) {
        printf("RESERV1 ");
    }
    if ((flags & (uint8_t)Flags::RESERV2) != 0) {
        printf("RESERV2 ");
    }
    if ((flags & (uint8_t)Flags::RESERV3) != 0) {
        printf("RESERV3 ");
    }
    printf("\n");
}

// TODO: get Boost.Outcome
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
std::vector<uint16_t> g_FixedHuffmanTree;
std::vector<uint16_t> g_ExtraBits;
std::vector<uint16_t> g_BaseLengths;

bool init_huffman_lengths(std::vector<uint16_t>& extra_bits, std::vector<uint16_t>& base_lengths)
{
//       Extra               Extra               Extra
//  Code Bits Length(s) Code Bits Lengths   Code Bits Length(s)
//  ---- ---- ------     ---- ---- -------   ---- ---- -------
//   257   0     3       267   1   15,16     277   4   67-82
//   258   0     4       268   1   17,18     278   4   83-98
//   259   0     5       269   2   19-22     279   4   99-114
//   260   0     6       270   2   23-26     280   4  115-130
//   261   0     7       271   2   27-30     281   5  131-162
//   262   0     8       272   2   31-34     282   5  163-194
//   263   0     9       273   3   35-42     283   5  195-226
//   264   0    10       274   3   43-50     284   5  227-257
//   265   1  11,12      275   3   51-58     285   0    258
//   266   1  13,14      276   3   59-66

    // TODO: can subtract 257
    constexpr size_t Elements = 285;
    extra_bits.assign(Elements, 0);
    base_lengths.assign(Elements, 0);
    for (size_t i = 257; i <= 264; ++i) {
        extra_bits[i] = 0;
        base_lengths[i] = i - 257 + 3;
    }
    for (size_t i = 265; i <= 268; ++i) {
        extra_bits[i] = 1;
        base_lengths[i] = (i - 265 + 11) + (i - 256)*2;
    }
    for (size_t i = 269; i <= 272; ++i) {
        extra_bits[i] = 2;
        base_lengths[i] = (i - 269 + 19) + (i - 269)*4;
    }
    for (size_t i = 273; i <= 276; ++i) {
        extra_bits[i] = 3;
        base_lengths[i] = (i - 273 + 35) + (i - 273)*8;
    }
    for (size_t i = 277; i <= 280; ++i) {
        extra_bits[i] = 4;
        base_lengths[i] = (i - 277 + 67) + (i - 273)*16;
    }
    for (size_t i = 281; i <= 284; ++i) {
        extra_bits[i] = 5;
        base_lengths[i] = (i - 281 + 131) + (i - 281)*32;
    }
    for (size_t i = 285; i <= 285; ++i) {
        extra_bits[i] = 0;
        base_lengths[i] = 258;
    }

    return true;
}

bool init_huffman_tree(std::vector<uint16_t>& tree, const uint16_t* code_lengths, size_t n)
{
    constexpr size_t MaxCodes = 512;
    constexpr size_t MaxBitLength = 16;
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    static uint16_t codes[MaxCodes];

    if (!(n < MaxCodes)) {
        assert(false && "code lengths too long");
        return false;
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the number
    // of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < n; ++i) {
        assert(code_lengths[i] <= MaxBitLength && "Unsupported bit length");
        ++bl_count[code_lengths[i]];
        if (code_lengths[i] > max_bit_length) {
            max_bit_length = code_lengths[i];
        }
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
        assert(tree[index] == EmptySentinel && "Assigned multiple values to same index");
        tree[index] = value;
    }

    return true;
}

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
    BitReader(FILE* f) : fp(f), index(bufsize()), buffer(0) {}

    // TODO: error handling
    bool read_bit()
    {
        if (index >= bufsize()) {
            if (fread(&buffer, sizeof(buffer), 1, fp) != 1) {
                fprintf(stderr, "BitReader: ERR: short read\n");
                exit(1);
            }
            index = 0;
        }
        bool result = (buffer & (1u << index)) != 0;
        ++index;
        return result;
    }

    uint8_t read_bits(size_t nbits)
    {
        assert(nbits <= 8);
        uint8_t result = 0;
#if 0
        for (size_t i = 0; i < nbits; ++i) {
            result <<= 1;
            result |= read_bit() ? 1 : 0;
        }
#else
        for (size_t i = 0; i < nbits; ++i) {
            result |= (read_bit() ? 1 : 0) << i;
        }
#endif
        return result;
    }

    size_t bufsize() const noexcept { return sizeof(buffer)*8; }

    FILE*   fp;
    uint8_t index;
    uint8_t buffer;
};

uint16_t read_huffman_value(const uint16_t* huffman_tree, BitReader& reader)
{
    size_t index = 1;
    do {
        index *= 2;
        index += reader.read_bit() ? 1 : 0;
    } while (huffman_tree[index] == EmptySentinel);
    return huffman_tree[index];
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
    for (size_t i = 0; i <= 143; ++i) {
        code_lengths[i] = 8;
    }
    for (size_t i = 144; i <= 255; ++i) {
        code_lengths[i] = 9;
    }
    for (size_t i = 256; i <= 279; ++i) {
        code_lengths[i] = 7;
    }
    for (size_t i = 280; i <= 287; ++i) {
        code_lengths[i] = 8;
    }
}

bool init_fixed_huffman_data()
{
    std::vector<uint16_t> code_lengths;
    get_fixed_huffman_lengths(code_lengths);

    if (!init_huffman_tree(g_FixedHuffmanTree, code_lengths.data(), code_lengths.size())) {
        fprintf(stderr, "ERR: failed to initialize fixed huffman decoding tree\n");
        return false;
    }

    if (!init_huffman_lengths(g_ExtraBits, g_BaseLengths)) {
        fprintf(stderr, "ERR: failed to initialize fixed huffman data\n");
        return false;
    }

    return true;
}

int main(int argc, char** argv)
{
    const char* output_filename;
    if (argc == 2) {
        output_filename = nullptr;
    } else if (argc == 3) {
        output_filename = argv[2];
    } else {
        fprintf(stderr, "Usage: %s [FILE] [OUT]\n", argv[0]);
        exit(0);
    }

    if (!init_fixed_huffman_data()) {
        exit(1);
    }

    const char* input_filename = argv[1];
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
        fprintf(stderr, "fread: short read\n");
        exit(1);
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
        fprintf(stderr, "Unsupported identifier #1: %u\n", hdr.id1);
        exit(0);
    }
    if (hdr.id2 != ID2_GZIP) {
        fprintf(stderr, "Unsupported identifier #2: %u\n", hdr.id2);
        exit(0);
    }

    print_flags_debug(hdr.flg);

    // (if FLG.FEXTRA set)
    //
    // +---+---+=================================+
    // | XLEN  |...XLEN bytes of "extra field"...| (more-->)
    // +---+---+=================================+
    if ((hdr.flg & static_cast<uint8_t>(Flags::FEXTRA)) != 0) {
        uint16_t xlen;
        if (fread(&xlen, sizeof(xlen), 1, fp) != 1) {
            fprintf(stderr, "ERR: short read on xlen\n");
            exit(1);
        }
        printf("XLEN = %u\n", xlen);
        std::vector<uint8_t> buffer;
        buffer.assign(xlen, 0u);
        if (fread(buffer.data(), xlen, 1, fp) != 1) {
            fprintf(stderr, "ERR: short read on FEXTRA bytes\n");
        }
        fprintf(stderr, "ERR: FEXTRA flag not supported.\n");
        exit(1);
    }

    // (if FLG.FNAME set)
    //
    // +=========================================+
    // |...original file name, zero-terminated...| (more-->)
    // +=========================================+
    std::string fname = "<none>";
    if ((hdr.flg & static_cast<uint8_t>(Flags::FNAME)) != 0) {
        if (!read_null_terminated_string(fp, fname)) {
            fprintf(stderr, "ERR: failed to read FNAME\n");
            exit(1);
        }
    }
    printf("Original Filename: '%s'\n", fname.c_str());

    // (if FLG.FCOMMENT set)
    //
    // +===================================+
    // |...file comment, zero-terminated...| (more-->)
    // +===================================+
    std::string fcomment = "<none>";
    if ((hdr.flg & static_cast<uint8_t>(Flags::FCOMMENT)) != 0) {
        if (!read_null_terminated_string(fp, fcomment)) {
            fprintf(stderr, "ERR: failed to read FCOMMENT\n");
            exit(1);
        }
    }
    printf("File comment: '%s'\n", fcomment.c_str());

    // (if FLG.FHCRC set)
    //
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
    if ((hdr.flg & static_cast<uint8_t>(Flags::FHCRC)) != 0) {
        if (fread(&crc16, sizeof(crc16), 1, fp) != 1) {
            fprintf(stderr, "ERR: failed to read CRC16\n");
            exit(1);
        }
    }
    printf("CRC16: %u (0x%04X)\n", crc16, crc16);

    // Reserved FLG bits must be zero.
    uint8_t mask = static_cast<uint8_t>(Flags::RESERV1) |
                   static_cast<uint8_t>(Flags::RESERV2) |
                   static_cast<uint8_t>(Flags::RESERV3);
    if ((hdr.flg & mask) != 0) {
        fprintf(stderr, "ERR: reserved bits are not 0\n");
        exit(1);
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
    print_operating_system_debug(hdr.os);

    //------------------------------------------------
    // Read Compressed Data
    //------------------------------------------------

    std::vector<uint16_t> dynamic_huffman_tree;
    std::vector<uint8_t> copy_buffer;
    for (;;) {
        BitReader reader(fp);
        uint8_t bfinal = reader.read_bit();
        BType btype = static_cast<BType>(reader.read_bits(2));
        copy_buffer.clear();

        printf("BlockHeader:\n");
        printf("\tbfinal = %u\n", bfinal);
        printf("\tbtype  = %u\n", (uint8_t)btype);
        printf("\n");

        if (btype == BType::NO_COMPRESSION) {
            printf("Block Encoding: No Compression\n");
            // discard remaining bits in first byte
            uint16_t len;
            uint16_t nlen;
            if (fread(&len, sizeof(len), 1, fp) != 1) {
                fprintf(stderr, "ERR: short read on len\n");
                exit(1);
            }
            if (fread(&nlen, sizeof(nlen), 1, fp) != 1) {
                fprintf(stderr, "ERR: short read on nlen\n");
                exit(1);
            }

            // TODO: faster API for copying from input to output? Might be on non-aligned though.
            // copy `len` bytes directly to output
            copy_buffer.assign('\0', len);
            if (fread(copy_buffer.data(), len, 1, fp) != 1) {
                fprintf(stderr, "ERR: short read on uncompressed data\n");
                exit(1);
            }
            if (fwrite(copy_buffer.data(), len, 1, output) != 1) {
                fprintf(stderr, "ERR: short write of uncompressed data\n");
                exit(1);
            }
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

            const uint16_t* huffman_tree = nullptr; // TEMP: error about unitialized huffman_tree
            const uint16_t* extra_bits;
            const uint16_t* base_lengths;
            copy_buffer.clear();
            if (btype == BType::FIXED_HUFFMAN) {
                printf("Block Encoding: Fixed Huffman\n");
                huffman_tree = g_FixedHuffmanTree.data();
                extra_bits = g_ExtraBits.data();
                base_lengths = g_BaseLengths.data();
            } else {
                printf("Block Encoding: Dynamic Huffman\n");

                constexpr size_t NumCodeLengths = 19;
                static uint16_t code_lengths_order[NumCodeLengths] = {
                    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
                };
                static uint16_t code_lengths[NumCodeLengths];
                memset(&code_lengths[0], 0, sizeof(code_lengths));

                size_t hlit = reader.read_bits(5) + 257;
                size_t hdist = reader.read_bits(5) + 1;
                size_t hclen = reader.read_bits(4) + 4;

                printf("hlit:  %zu\n", hlit);
                printf("hdist: %zu\n", hdist);
                printf("hclen: %zu\n", hclen);

                for (size_t i = 0; i < hclen; ++i) {
                    uint8_t codelen = reader.read_bits(3);
                    printf("\tcodelen = %u\n", codelen);
                    code_lengths[code_lengths_order[i]] = codelen;
                }

                printf("Code Lengths Table:\n");
                for (size_t i = 0; i < NumCodeLengths; ++i) {
                    printf("%zu\t%u\n", i, code_lengths[i]);
                }
                printf("\n");

                if (!init_huffman_tree(dynamic_huffman_tree, &code_lengths[0], NumCodeLengths)) {
                    fprintf(stderr, "ERR: unable to initialize huffman tree for dynamic huffman encoding.\n");
                }

                auto repeat_value = [](std::vector<uint16_t>& v, uint16_t value, size_t repeat) {
                    for (size_t i = 0; i < repeat; ++i) {
                        v.push_back(value);
                    }
                };

                std::vector<uint16_t> clens;
                for (size_t i = 0; i < (hlit + hdist); ++i) {
                    uint16_t value = read_huffman_value(huffman_tree, reader);
                    if (value <= 15) {
                        clens.push_back(value);
                    } else if (value == 16) {
                        size_t repeat = reader.read_bits(2) + 3;
                        if (!clens.empty()) {
                            fprintf(stderr, "ERR: received repeat code with no codes.\n");
                            exit(1);
                        }
                        repeat_value(clens, clens.back(), repeat);
                    } else if (value == 17) {
                        size_t repeat = reader.read_bits(3) + 3;
                        repeat_value(clens, 0, repeat);
                    } else if (value == 18) {
                        size_t repeat = reader.read_bits(7) + 11;
                        repeat_value(clens, 0, repeat);
                    }
                }

                printf("Read %zu code lengths\n", clens.size());

                fprintf(stderr, "ERR: dynamic huffman not supported yet\n");
                exit(1);
            }

            for (;;) {
                uint16_t value = read_huffman_value(huffman_tree, reader);
                if (value < 256) {
                    copy_buffer.push_back(static_cast<uint8_t>(value));
                } else if (value == 256) {
                    // TEMP TEMP
                    printf("Found end of compressed block!\n");
                    break;
                } else if (value < 285) {
                    size_t length = base_lengths[value];
                    size_t extra = 0;
                    size_t distance = 0;
                    for (int i = 0; i < extra_bits[value]; ++i) {
                        extra <<= 1;
                        extra |= reader.read_bit() ? 1 : 0;
                    }
                    length += extra;
                    for (int i = 0; i < 5; ++i) {
                        distance <<= 1;
                        distance |= reader.read_bit() ? 1 : 0;
                    }
                    // TODO: is there a more efficient way to copy from a portion of the buffer
                    //       to another? I could pre-allocate the size, then memcpy the section over?
                    size_t start_index = copy_buffer.size() - distance - 1;
                    for (size_t i = 0; i < length; ++i) {
                        copy_buffer.push_back(copy_buffer[start_index + i]);
                    }
                } else {
                    fprintf(stderr, "ERR: invalid fixed huffman value: %u\n", value);
                    exit(1);
                }
            }
        } else {
            fprintf(stderr, "ERR: unsupported block encoding: %u\n", (uint8_t)btype);
            exit(1);
        }

        if (fwrite(copy_buffer.data(), copy_buffer.size(), 1, output) != 1) {
            fprintf(stderr, "ERR: short write\n");
            exit(1);
        }

        if (bfinal) {
            printf("Processed final compressed block.\n");
            break;
        }
    }

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
