#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <libgen.h>
#include <cassert>

#define fatal_error(fmt, ...) do { fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); exit(1); } while(0)

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

    uint16_t read_bits(size_t nbits, bool verbose=false)
    {
        assert(nbits <= 16);
        uint16_t result = 0;
        for (size_t i = 0; i < nbits; ++i) {
            result |= (read_bit() ? 1u : 0u) << i;
            if (verbose) {
                printf("\t0x%02u\n", result);
            }
        }
        return result;
    }

    size_t bufsize() const noexcept { return sizeof(buffer)*8; }

    FILE*   fp;
    uint8_t index;
    uint8_t buffer;
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
std::vector<uint16_t> g_FixedDistanceTree;
std::vector<uint16_t> g_ExtraLengthBits;
std::vector<uint16_t> g_BaseLengths;
std::vector<uint16_t> g_ExtraDistanceBits;
std::vector<uint16_t> g_BaseDistanceLengths;

bool init_huffman_distances(std::vector<uint16_t>& extra_bits, std::vector<uint16_t>& base_lengths)
{
//       Extra           Extra               Extra
//  Code Bits Dist  Code Bits   Dist     Code Bits Distance
//  ---- ---- ----  ---- ----  ------    ---- ---- --------
//    0   0    1     10   4     33-48    20    9   1025-1536
//    1   0    2     11   4     49-64    21    9   1537-2048
//    2   0    3     12   5     65-96    22   10   2049-3072
//    3   0    4     13   5     97-128   23   10   3073-4096
//    4   1   5,6    14   6    129-192   24   11   4097-6144
//    5   1   7,8    15   6    193-256   25   11   6145-8192
//    6   2   9-12   16   7    257-384   26   12  8193-12288
//    7   2  13-16   17   7    385-512   27   12 12289-16384
//    8   3  17-24   18   8    513-768   28   13 16385-24576
//    9   3  25-32   19   8   769-1024   29   13 24577-32768
    constexpr size_t NumElements = 32;
    extra_bits.assign(NumElements, 0);
    base_lengths.assign(NumElements, 0);

    struct {
        size_t extra_bits;
        size_t base_length;
    } entries[NumElements] = {
        /* 0*/ {  0,     1 },
        /* 1*/ {  0,     2 },
        /* 2*/ {  0,     3 },
        /* 3*/ {  0,     4 },
        /* 4*/ {  1,     5 },
        /* 5*/ {  1,     7 },
        /* 6*/ {  2,     9 },
        /* 7*/ {  2,    13 },
        /* 8*/ {  3,    17 },
        /* 9*/ {  3,    25 },
        /*10*/ {  4,    33 },
        /*11*/ {  4,    49 },
        /*12*/ {  5,    65 },
        /*13*/ {  5,    97 },
        /*14*/ {  6,   129 },
        /*15*/ {  6,   193 },
        /*16*/ {  7,   257 },
        /*17*/ {  7,   385 },
        /*18*/ {  8,   513 },
        /*19*/ {  8,   769 },
        /*20*/ {  9,  1025 },
        /*21*/ {  9,  1537 },
        /*22*/ { 10,  2049 },
        /*23*/ { 10,  3073 },
        /*24*/ { 11,  4097 },
        /*25*/ { 11,  6145 },
        /*26*/ { 12,  8193 },
        /*27*/ { 12, 12289 },
        /*28*/ { 13, 16385 },
        /*29*/ { 13, 24577 },
        /*30*/ {  0,     0 },
        /*31*/ {  0,     0 },
    };

    for (size_t i = 0; i < NumElements; ++i) {
        extra_bits[i] = entries[i].extra_bits;
        base_lengths[i] = entries[i].base_length;
    }

    return true;
}

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
    // constexpr size_t NumElements = 28;
    constexpr size_t NumElements = 29;
    extra_bits.assign(257+NumElements, 0);
    base_lengths.assign(257+NumElements, 0);

    struct {
        size_t extra_bits;
        size_t base_length;
    } entries[NumElements] = {
        /*257*/ { 0,   3 },
        /*258*/ { 0,   4 },
        /*259*/ { 0,   5 },
        /*260*/ { 0,   6 },
        /*261*/ { 0,   7 },
        /*262*/ { 0,   8 },
        /*263*/ { 0,   9 },
        /*264*/ { 0,  10 },
        /*265*/ { 1,  11 },
        /*266*/ { 1,  13 },
        /*267*/ { 1,  15 },
        /*268*/ { 1,  17 },
        /*269*/ { 2,  19 },
        /*270*/ { 2,  23 },
        /*271*/ { 2,  27 },
        /*272*/ { 2,  31 },
        /*273*/ { 3,  35 },
        /*274*/ { 3,  43 },
        /*275*/ { 3,  51 },
        /*276*/ { 3,  59 },
        /*277*/ { 4,  67 },
        /*278*/ { 4,  83 },
        /*279*/ { 4,  99 },
        /*280*/ { 4, 115 },
        /*281*/ { 5, 131 },
        /*282*/ { 5, 163 },
        /*283*/ { 5, 195 },
        /*284*/ { 5, 227 },
        /*285*/ { 0, 258 },
    };

    for (size_t i = 0; i < NumElements; ++i) {
        extra_bits[i+257] = entries[i].extra_bits;
        base_lengths[i+257] = entries[i].base_length;
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

    return true;
}

uint16_t read_huffman_value(const uint16_t* huffman_tree, size_t huffman_tree_length, BitReader& reader)
{
    size_t index = 1;
    do {
        index *= 2;
        index += reader.read_bit() ? 1 : 0;
        assert(index < huffman_tree_length && "invalid index");
    } while (huffman_tree[index] == EmptySentinel);
    return huffman_tree[index];
}

bool read_dynamic_huffman_tree(BitReader& reader, std::vector<uint16_t>& literal_tree, std::vector<uint16_t>& distance_tree)
{
    constexpr size_t NumCodeLengths = 19;
    static const uint16_t code_lengths_order[NumCodeLengths] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    static uint16_t code_lengths[NumCodeLengths];
    memset(&code_lengths[0], 0, sizeof(code_lengths));

    size_t hlit = reader.read_bits(5) + 257;
    size_t hdist = reader.read_bits(5) + 1;
    size_t hclen = reader.read_bits(4) + 4;
    size_t ncodes = hlit + hdist;

    DEBUG("hlit:  %zu", hlit);
    DEBUG("hdist: %zu", hdist);
    DEBUG("hclen: %zu", hclen);

    for (size_t i = 0; i < hclen; ++i) {
        uint8_t codelen = reader.read_bits(3);
        code_lengths[code_lengths_order[i]] = codelen;
    }

    if (!init_huffman_tree(literal_tree, &code_lengths[0], NumCodeLengths)) {
        fatal_error("failed to initialize dynamic huffman tree.");
    }

    std::vector<uint16_t> dynamic_code_lengths;
    while (dynamic_code_lengths.size() < ncodes) {
        uint16_t value = read_huffman_value(literal_tree.data(), literal_tree.size(), reader);
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
                    fatal_error("received repeat code 16 with no codes to repeat.");
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
            fatal_error("invalid value: %u", value);
        }
    }
    assert(dynamic_code_lengths.size() == ncodes && "Went over the number of expected codes");
    assert(dynamic_code_lengths.size() > 256 && dynamic_code_lengths[256] != 0 && "invalid code -- missing end-of-block");

    if (!init_huffman_tree(literal_tree, dynamic_code_lengths.data(), hlit)) {
        fatal_error("failed to initialize dynamic huffman tree");
    }
    assert((dynamic_code_lengths.size() - hlit) == hdist);
    if (!init_huffman_tree(distance_tree, dynamic_code_lengths.data() + hlit, dynamic_code_lengths.size() - hlit)) {
        fatal_error("failed to initialize dynamic distance tree");
    }

    return true;
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
        fatal_error("failed to initialize fixed huffman tree.");
        return false;
    }

    code_lengths.assign(32, 5);
    if (!init_huffman_tree(g_FixedDistanceTree, code_lengths.data(), code_lengths.size())) {
        fatal_error("failed to initialize fixed distance huffman tree.");
    }

    if (!init_huffman_lengths(g_ExtraLengthBits, g_BaseLengths)) {
        fatal_error("failed to initialize huffman length data.");
        return false;
    }

    if (!init_huffman_distances(g_ExtraDistanceBits, g_BaseDistanceLengths)) {
        fatal_error("failed to initialize huffman distance data.");
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
        fatal_error("fread: short read");
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
        fatal_error("Unsupported identifier #1: %u.", hdr.id1);
    }
    if (hdr.id2 != ID2_GZIP) {
        fatal_error("Unsupported identifier #2: %u.", hdr.id2);
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
            fatal_error("short read on xlen.");
        }
        printf("XLEN = %u\n", xlen);
        std::vector<uint8_t> buffer;
        buffer.assign(xlen, 0u);
        if (fread(buffer.data(), xlen, 1, fp) != 1) {
            fatal_error("short read on FEXTRA bytes.");
        }
        fatal_error("FEXTRA flag not supported.");
    }

    // (if FLG.FNAME set)
    //
    // +=========================================+
    // |...original file name, zero-terminated...| (more-->)
    // +=========================================+
    std::string fname = "<none>";
    if ((hdr.flg & static_cast<uint8_t>(Flags::FNAME)) != 0) {
        if (!read_null_terminated_string(fp, fname)) {
            fatal_error("failed to read FNAME.");
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
            fatal_error("failed to read FCOMMENT.");
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
            fatal_error("failed to read CRC16.");
        }
    }
    printf("CRC16: %u (0x%04X)\n", crc16, crc16);

    // Reserved FLG bits must be zero.
    uint8_t mask = static_cast<uint8_t>(Flags::RESERV1) |
                   static_cast<uint8_t>(Flags::RESERV2) |
                   static_cast<uint8_t>(Flags::RESERV3);
    if ((hdr.flg & mask) != 0) {
        fatal_error("reserved bits are not 0.");
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
    std::vector<uint16_t> dynamic_distance_tree;
    std::vector<uint8_t> copy_buffer;
    for (;;) {
        BitReader reader(fp);
        uint8_t bfinal = reader.read_bit();
        BType btype = static_cast<BType>(reader.read_bits(2, /*verbose*/true));
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
                fatal_error("short read on len.");
            }
            if (fread(&nlen, sizeof(nlen), 1, fp) != 1) {
                fatal_error("short read on nlen.");
            }
            copy_buffer.assign(len, '\0');
            if (fread(copy_buffer.data(), len, 1, fp) != 1) {
                fatal_error("short read on uncompressed data.");
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
            size_t huffman_tree_length;
            const uint16_t* distance_tree;
            size_t distance_tree_length;
            const uint16_t* extra_bits = g_ExtraLengthBits.data();
            const uint16_t* base_lengths = g_BaseLengths.data();
            const uint16_t* extra_distance_bits = g_ExtraDistanceBits.data();
            const uint16_t* base_distance_lengths = g_BaseDistanceLengths.data();
            copy_buffer.clear();
            if (btype == BType::FIXED_HUFFMAN) {
                printf("Block Encoding: Fixed Huffman\n");
                huffman_tree = g_FixedHuffmanTree.data();
                huffman_tree_length = g_FixedHuffmanTree.size();
                distance_tree = g_FixedDistanceTree.data();
                distance_tree_length = g_FixedDistanceTree.size();
            } else {
                DEBUG("Block Encoding: Dynamic Huffman");
                if (!read_dynamic_huffman_tree(reader, dynamic_huffman_tree, dynamic_distance_tree)) {
                    fatal_error("failed to read dynamic huffman tree");
                }
                huffman_tree = dynamic_huffman_tree.data();
                huffman_tree_length = dynamic_huffman_tree.size();
                distance_tree = dynamic_distance_tree.data();
                distance_tree_length = dynamic_distance_tree.size();
            }

            for (;;) {
                uint16_t value = read_huffman_value(huffman_tree, huffman_tree_length, reader);
                if (value < 256) {
                    DEBUG("inflate: literal(%3u): '%c'", value, (char)value);
                    copy_buffer.push_back(static_cast<uint8_t>(value));
                } else if (value == 256) {
                    DEBUG("inflate: end of block found");
                    break;
                } else if (value <= 285) {
                    DEBUG("inflate: code %u", value);
                    size_t base_length = base_lengths[value];
                    size_t extra_length = reader.read_bits(extra_bits[value]);
                    size_t length = base_length + extra_length;
                    size_t distance_code = read_huffman_value(distance_tree, distance_tree_length, reader);
                    assert((distance_code < 32) && "invalid distance code");
                    size_t base_distance = base_distance_lengths[distance_code];
                    size_t extra_distance = reader.read_bits(extra_distance_bits[distance_code]);
                    size_t distance = base_distance + extra_distance;
                    DEBUG("inflate: distance %zu", distance);
                    DEBUG("Length Code = %u, "
                          "Base Length = %zu, "
                          "Extra Bits = %u, "
                          "Extra Length = %zu, "
                          "Length = %zu, "
                          "Base Distance = %zu, "
                          "Extra Distance = %zu, "
                          "Distance Code = %zu, "
                          "Extra Distance Bits = %d",
                           value,                             // Length Code
                           base_length,                       // Base Length
                           extra_bits[value],                 // Extra Bits
                           extra_length,                      // Extra Length
                           length,                            // Length
                           base_distance,                     // Base Distance
                           extra_distance,                    // Extra Distance
                           distance_code,                     // Distance Code
                           extra_distance_bits[distance_code] // Extra Distance Bits
                           );
                    // TODO: is there a more efficient way to copy from a portion of the buffer
                    //       to another? I could pre-allocate the size, then memcpy the section over?
                    // TEMP TEMP
                    std::string copy_value;
                    assert(copy_buffer.size() >= distance && "invalid distance");
                    size_t start_index = copy_buffer.size() - distance;
                    for (size_t i = 0; i < length; ++i) {
                        copy_value += copy_buffer[start_index + i];
                        copy_buffer.push_back(copy_buffer[start_index + i]);
                    }
                    printf("COPIED VALUE: %s\n", copy_value.c_str());
                    // const char* ss = "    s->head[s->ins_h] = (Pos)(str))\n#else\n=====================================================================";
                    // const char* ss = "s->max_lazy_match";
                    // if (memmem(copy_buffer.data(), copy_buffer.size(), ss, strlen(ss)) != nullptr) {
                    //     printf("BUFFER CONTENTS:\n%.*s\n", 600, copy_buffer.data() + copy_buffer.size() - 600);
                    //     fatal_error("FOUND IT!");
                    // }
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
            DEBUG("Processed final compressed block.");
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
