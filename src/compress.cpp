#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define panic(fmt, ...) do { fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); exit(1); } while(0)

uint8_t ID1_GZIP = 31;
uint8_t ID2_GZIP = 139;
uint8_t CM_DEFLATE = 8;

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

void xwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (fwrite(ptr, size, nmemb, stream) != nmemb) {
        panic("short write");
    }
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [FILE]\n", argv[0]);
        exit(0);
    }
    std::string input_filename = argv[1];
    std::string output_filename = input_filename + ".gz";

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

// +---+---+---+---+---+---+---+---+---+---+
// |ID1|ID2|CM |FLG|     MTIME     |XFL|OS | (more-->)
// +---+---+---+---+---+---+---+---+---+---+

    uint8_t flags = static_cast<uint8_t>(Flags::FNAME);
    uint32_t mtime = 0; // TODO: set mtime to seconds since epoch
    uint8_t xfl = 0;
    uint8_t os = 3; // UNIX
    xwrite(&ID1_GZIP,   sizeof(ID1_GZIP),   1, out); // ID1
    xwrite(&ID2_GZIP,   sizeof(ID2_GZIP),   1, out); // ID2
    xwrite(&CM_DEFLATE, sizeof(CM_DEFLATE), 1, out); // CM
    xwrite(&flags,      sizeof(flags),      1, out); // FLG
    xwrite(&mtime,      sizeof(mtime),      1, out); // MTIME
    xwrite(&xfl,        sizeof(xfl),        1, out); // XFL
    xwrite(&os,         sizeof(os),         1, out); // OS

// (if FLG.FNAME set)
//   +=========================================+
//   |...original file name, zero-terminated...| (more-->)
//   +=========================================+
    xwrite(input_filename.c_str(), input_filename.size() + 1, 1, out); // FNAME

    uint32_t crc32 = 0;
    // This contains the size of the original (uncompressed) input
    // data modulo 2^32.
    uint32_t isize = 0;

    uint8_t blkhdr;
    uint8_t bfinal;
    uint8_t btype;
    uint16_t len;
    uint16_t nlen;

    std::vector<uint8_t> buffer;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        uint8_t byte = static_cast<uint8_t>(c);
        buffer.push_back(byte);
        ++isize;
    }

    bfinal = 1;
    btype = static_cast<uint8_t>(BType::NO_COMPRESSION);
    // blkhdr = (bfinal << 7) | (btype << 6);
    blkhdr = bfinal | (btype << 1);
    len = buffer.size();
    nlen = len ^ 0xffff;
    xwrite(&blkhdr, sizeof(blkhdr), 1, out); // BLOCK HEADER
    xwrite(&len,    sizeof(len),    1, out); // LEN
    xwrite(&nlen,   sizeof(nlen),   1, out); // NLEN
    xwrite(buffer.data(), buffer.size(), 1, out);

//   0   1   2   3   4   5   6   7
// +---+---+---+---+---+---+---+---+
// |     CRC32     |     ISIZE     |
// +---+---+---+---+---+---+---+---+
    xwrite(&crc32, sizeof(crc32), 1, out); // CRC32
    xwrite(&isize, sizeof(isize), 1, out); // ISIZE

    return 0;
}
