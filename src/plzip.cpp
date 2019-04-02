#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <libgen.h>
#include <cassert>

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
    constexpr size_t Elements = 259;
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

bool init_huffman_tree(std::vector<uint16_t>& tree)
{
    constexpr size_t MaxBitLength = 16;
    static size_t bl_count[MaxBitLength];
    static uint16_t next_code[MaxBitLength];
    constexpr size_t AlphaLen = 288;
    static size_t nbits[AlphaLen];
    static uint16_t codes[AlphaLen];

    memset(&nbits[0], 0, sizeof(nbits));

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
    for (size_t i = 0; i <= 143; ++i) {
        nbits[i] = 8;
    }
    for (size_t i = 144; i <= 255; ++i) {
        nbits[i] = 9;
    }
    for (size_t i = 256; i <= 279; ++i) {
        nbits[i] = 7;
    }
    for (size_t i = 280; i <= 287; ++i) {
        nbits[i] = 8;
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the number of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < AlphaLen; ++i) {
        assert(nbits[i] <= MaxBitLength && "Unsupported bit length");
        ++bl_count[nbits[i]];
        if (nbits[i] > max_bit_length) {
            max_bit_length = nbits[i];
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

    // 3) Assign numerical values to all codes, using consecutive values for all codes of the same length with
    // the base values determined at step 2.  Codes that are never used (which have a bit length of zero) must
    // not be assigned a value.
    memset(&codes[0], 0, sizeof(codes));
    for (size_t i = 0; i < AlphaLen; ++i) {
        if (nbits[i] != 0) {
            codes[i] = next_code[nbits[i]]++;
        }
    }

    // Table size is 2**(max_bit_length + 1)
    size_t table_size = 1u << (max_bit_length + 1);
    tree.assign(table_size, EmptySentinel);
    for (size_t value = 0; value < AlphaLen; ++value) {
        size_t len = nbits[value];
        uint16_t code = codes[value];
        size_t index = 1;
        for (int i = len - 1; i >= 0; --i) {
            int isset = ((code & (1u << i)) != 0) ? 1 : 0;
            index = 2*index + isset;
        }
        assert(tree[index] == EmptySentinel && "Assigned multiple values to same index");
        tree[index] = value;
    }

    return true;
}

int huffman_test_main()
{
    // TODO: Data Structure Rep: store the huffman encoding tree in an array where
    // 0 = left, 1 = right, so do 0 => 2*n, 1 => 2*n+1. If a value is a leaf, then
    // the corresponding character goes into that slot, otherwise put some sentinal
    // value.
    //
    // For an FPGA representation, maybe just do a linear search everytime?

    printf("Huffman Decoding Test\n");
    printf("---------------------\n");
    printf("\n");

    constexpr size_t MaxBitLength = 32;
    const char alpha[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H' };
    const size_t nbits[] = { 3, 3, 3, 3, 3, 2, 4, 4 };
    const size_t nalpha = sizeof(alpha);

    if (nalpha != sizeof(alpha) || nalpha != (sizeof(nbits)/sizeof(nbits[0]))) {
        fprintf(stderr, "ERR: alphabet length and # of bit lengths don't match\n");
        return 1;
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the number of codes of length N, N >= 1.
    static size_t bl_count[MaxBitLength];
    size_t max_bit_length = 0;
    memset(&bl_count[0], 0, sizeof(bl_count));
    for (size_t i = 0; i < nalpha; ++i) {
        if (nbits[i] >= MaxBitLength) {
            fprintf(stderr, "ERR: bit length too long: %zu\n", nbits[i]);
            return 1;
        }
        ++bl_count[nbits[i]];
        if (nbits[i] > max_bit_length) {
            max_bit_length = nbits[i];
        }
    }

    // DEBUG
    printf("N      bl_count[N]\n");
    printf("-      -----------\n");
    for (size_t i = 1; i <= max_bit_length; ++i) {
        printf("%zu      %zu\n", i, bl_count[i]);
    }
    printf("\n");
    // GUBED

    // 2) Find the numerical value of the smallest code for each code length:
    // code = 0;
    // bl_count[0] = 0;
    // for (bits = 1; bits <= MAX_BITS; bits++) {
    //     code = (code + bl_count[bits-1]) << 1;
    //     next_code[bits] = code;
    // }
    static uint32_t next_code[MaxBitLength];
    memset(&next_code[0], 0, sizeof(next_code));
    bl_count[0] = 0;
    uint32_t code = 0;
    for (size_t bits = 1; bits <= max_bit_length; ++bits) {
        code = (code + bl_count[bits-1]) << 1;
        next_code[bits] = code;
    }

    // DEBUG
    printf("N      next_code[N]\n");
    printf("-      -----------\n");
    for (size_t i = 1; i <= max_bit_length; ++i) {
        printf("%zu      %u\n", i, next_code[i]);
    }
    printf("\n");
    // GUBED

    // N      next_code[N]
    // -      ------------
    // 1      0  => 0000
    // 2      0  => 0000
    // 3      2  => 0010
    // 4      14 => 1110

    // 3) Assign numerical values to all codes, using consecutive values for all codes of the same length with
    // the base values determined at step 2.  Codes that are never used (which have a bit length of zero) must
    // not be assigned a value.
    // for (n = 0;  n <= max_code; n++) {
    //     len = tree[n].Len;
    //     if (len != 0) {
    //         tree[n].Code = next_code[len];
    //         next_code[len]++;
    //     }
    // }
    static uint32_t codes[512];
    memset(&codes[0], 0, sizeof(codes));
    for (size_t i = 0; i < nalpha; ++i) {
        size_t len = nbits[i];
        if (len != 0) {
            codes[i] = next_code[len]++;
        }
    }

    printf("Symbol Length   Code\n");
    printf("------ ------   ----\n");
    for (size_t i = 0; i < nalpha; ++i) {
        printf("%c      %zu        %u\n", alpha[i], nbits[i], codes[i]);
    }
    printf("\n");

    // Symbol Length   Code
    // ------ ------   ----
    // A       3        010
    // B       3        011
    // C       3        100
    // D       3        101
    // E       3        110
    // F       2         00
    // G       4       1110
    // H       4       1111

#if 0
    // table size = 2**(max_bit_length) + 1)
    static char tree[32];
    memset(&tree[0], ' ', sizeof(tree));

    for (size_t i = 0; i < nalpha; ++i) {
        char letter = alpha[i];
        size_t len = nbits[i];
        uint32_t code = codes[i];

        size_t idx = 1;
        printf("Handling %c -> %zu -> %u\n", letter, len, code);
        for (int j = len - 1; j >= 0; --j) {
            bool isset = (code & (1u << j)) != 0;
            printf("BitSet? %d\n", isset);
            idx = 2*idx + int(isset);
        }
        printf("Index = %zu\n", idx);
        tree[idx] = letter;
    }


    for (size_t i = 0; i < sizeof(tree); ++i) {
        printf("%zu\t%c\n", i, tree[i]);
    }
#endif
    return 0;
}

struct FileHandle
{
    FileHandle(FILE* f = nullptr) noexcept : fp(f) {}
    FileHandle(FileHandle&) noexcept = delete;
    FileHandle& operator=(FileHandle&) noexcept = delete;
    ~FileHandle() noexcept { if (fp) { fclose(fp); } }
    operator FILE* () noexcept { return fp; }
    explicit operator bool() const noexcept { return fp != nullptr; }
    FILE* fp;
};

struct BitReader
{
    BitReader(FILE* f) : fp(f), index(sizeof(buffer)*8), buffer(0) {}

    // TODO: error handling
    bool GetBit()
    {
        if (index >= sizeof(buffer)*8) {
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

    FILE*   fp;
    uint8_t index;
    uint8_t buffer;
};

uint16_t read_huffman_value(const uint16_t* huffman_tree, BitReader& reader)
{
    size_t index = 1;
    do {
        index *= 2;
        index += reader.GetBit() ? 1 : 0;
    } while (huffman_tree[index] == EmptySentinel);
    return huffman_tree[index];
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s [FILE] [OUT]\n", argv[0]);
        exit(0);
    }

    if (!init_huffman_tree(g_FixedHuffmanTree)) {
        fprintf(stderr, "ERR: failed to initialize fixed huffman decoding tree\n");
        exit(1);
    }
    if (!init_huffman_lengths(g_ExtraBits, g_BaseLengths)) {
        fprintf(stderr, "ERR: failed to initialize fixed huffman data\n");
    }

    const char* input_filename = argv[1];
    FileHandle fp = fopen(input_filename, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    const char* output_filename = argv[2];
    FileHandle output = fopen(output_filename, "wb");
    if (!fp) {
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

    std::vector<uint8_t> copy_buffer;
    for (;;) {
        BitReader reader(fp);
        uint8_t bfinal = reader.GetBit();
        uint8_t btype_ = 0u;
        btype_ |= reader.GetBit() << 0;
        btype_ |= reader.GetBit() << 1;
        BType btype = static_cast<BType>(btype_);
        copy_buffer.clear();

        printf("BlockHeader:\n");
        printf("\tbfinal = %u\n", bfinal);
        printf("\tbtype  = %u\n", btype_);
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

            // TODO: faster API for copying from input to output? Might be on non-aligned
            //       address though.
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

            const uint16_t* huffman_tree;
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
                        extra |= reader.GetBit() ? 1 : 0;
                    }
                    length += extra;
                    for (int i = 0; i < 5; ++i) {
                        distance <<= 1;
                        distance |= reader.GetBit() ? 1 : 0;
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

    printf("\n\n\n");
    if (huffman_test_main() != 0) {
        fprintf(stderr, "ERR: huffman_test_main failed\n");
        exit(1);
    }

    return 0;
}
