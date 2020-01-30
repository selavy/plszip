#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cassert>

#define panic(fmt, ...) do { fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); exit(1); } while(0)

#define BUFSIZE   2056
#define READSIZE  32
#define BLOCKSIZE 1024

uint32_t crc_table[256];

void init_crc_table()
{
    for (int n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

uint32_t calc_crc32(uint32_t crc, const char* buf, size_t len)
{
    if (buf == NULL)
        return 0;
    crc = crc ^ 0xffffffffUL;
    for (size_t i = 0; i < len; ++i) {
        crc = crc_table[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffUL;
}

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
    if (fwrite(ptr, size, nmemb, stream) != nmemb)
        panic("short write");
}

void blkwrite_no_compression(const char* buffer, size_t size, uint8_t bfinal, FILE* fp)
{
    uint8_t btype = static_cast<uint8_t>(BType::NO_COMPRESSION);
    uint8_t blkhdr = bfinal | (btype << 1);
    uint16_t len = size;
    uint16_t nlen = len ^ 0xffffu;
    xwrite(&blkhdr, sizeof(blkhdr), 1, fp);
    xwrite(&len,    sizeof(len),    1, fp);
    xwrite(&nlen,   sizeof(nlen),   1, fp);
    xwrite(&buffer[0], 1, size, fp);
}

int main(int argc, char** argv)
{
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

    //   +=========================================+
    //   |...original file name, zero-terminated...| (more-->)
    //   +=========================================+
    xwrite(input_filename.c_str(), input_filename.size() + 1, 1, out); // FNAME

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
            blkwrite_no_compression(buf, BLOCKSIZE, 0, out);
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
        blkwrite_no_compression(buf, size, 1, out);
    }

    //   0   1   2   3   4   5   6   7
    // +---+---+---+---+---+---+---+---+
    // |     CRC32     |     ISIZE     |
    // +---+---+---+---+---+---+---+---+
    xwrite(&crc,   sizeof(crc),   1, out); // CRC32
    xwrite(&isize, sizeof(isize), 1, out); // ISIZE

    return 0;
}
