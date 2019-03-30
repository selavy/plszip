#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

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

void print_operating_system_debug(uint8_t os) {
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

void print_flags_debug(uint8_t flags) {
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
bool read_null_terminated_string(FILE* fp, std::string& result) {
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

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [FILE]\n", argv[0]);
        exit(0);
    }

    const char* filename = argv[1];
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    GzipHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        fprintf(stderr, "fread: short read\n");
        fclose(fp);
        exit(1);
    }
    // TODO: handle big endian platform

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
        fclose(fp);
        exit(0);
    }
    if (hdr.id2 != ID2_GZIP) {
        fprintf(stderr, "Unsupported identifier #2: %u\n", hdr.id2);
        fclose(fp);
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
            fclose(fp);
            exit(1);
        }
        printf("XLEN = %u\n", xlen);
        // TODO: read xlen bytes
        fprintf(stderr, "ERR: FEXTRA flag not supported.\n");
        fclose(fp);
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
            fclose(fp);
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
            fclose(fp);
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
            fclose(fp);
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
        fclose(fp);
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

    fclose(fp);
    return 0;
}
