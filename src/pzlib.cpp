#include "pzlib.h"

#ifndef USE_ZLIB

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string> // TEMP TEMP

#ifdef NDEBUG
#define DEBUG
#else
#define DEBUG(fmt, ...) fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__);
#endif

enum inflate_mode {
    HEADER, /* ID1 | ID2 | CM */
    FLAGS,  /* FLG */
    MTIME,  /* MTIME */
    XFL,    /* XFL */
    OS,     /* OS */
    FEXTRA, /* FEXTRA fields */
    FEXTRA_DATA,
    FNAME,
    FCOMMENT,
    FHCRC,
    BEGIN_BLOCK
};
typedef enum inflate_mode inflate_mode;

struct internal_state {
    inflate_mode mode;
    uInt  bits; /* # of bits in bit accumulator */
    uLong buff; /* bit accumator */
    uLong temp;
    Byte  flag;
    std::string filename;
};

voidpf zcalloc(voidpf opaque, uInt items, uInt size) {
    return calloc(items, size);
}

void zcfree(voidpf opaque, voidpf ptr) { free(ptr); }

const char *zlibVersion() { return "pzlib 0.0.1"; }

int inflateInit2_(z_streamp strm, int windowBits, const char *version,
                  int stream_size) {
    printf("pzlib::inflateInit2_\n");
    if (strcmp(version, ZLIB_VERSION) != 0) {
        return Z_VERSION_ERROR;
    }
    if (stream_size != sizeof(z_stream)) {
        return Z_VERSION_ERROR;
    }
    if (strm == Z_NULL) {
        return Z_STREAM_ERROR;
    }

    if (strm->zalloc == Z_NULL) {
        strm->zalloc = &zcalloc;
        strm->opaque = Z_NULL;
    }
    if (strm->zfree == Z_NULL) {
        strm->zfree = &zcfree;
    }

    if (windowBits != 15 + 16) {
        strm->msg = "invalid windowBits parameter -- 31 only supported value";
        return Z_STREAM_ERROR;
    }

    strm->state = (struct internal_state *)strm->zalloc(
        strm->opaque, 1, sizeof(struct internal_state));
    if (!strm->state) {
        strm->msg = "failed to allocate memory for internal state";
        return Z_MEM_ERROR;
    }

    strm->state->mode = HEADER;
    strm->state->buff = 0UL;
    strm->state->bits = 0;
    strm->state->flag = 0;
    strm->state->temp = 0;

    return Z_OK;
}

int inflateEnd(z_streamp strm) {
    printf("pzlib::inflateEnd\n");
    strm->zfree(strm->opaque, strm->state);
    strm->state = Z_NULL;
    return Z_OK;
}

static char msgbuf[1024];

#define panic(rc, fmt, ...)                                   \
    do {                                                      \
        snprintf(msgbuf, sizeof(msgbuf), fmt, ##__VA_ARGS__); \
        strm->msg = msgbuf;                                   \
        ret = rc;                                             \
        goto exit;                                            \
    } while (0)

#define NEXTBYTE()                    \
    do {                              \
        if (avail == 0) goto exit;    \
        avail--;                      \
        buff = (buff << 8) | *next++; \
        read++;                       \
        bits += 8;                    \
    } while (0)

#define NEEDBITS(n)                    \
    do {                               \
        while (bits < (n)) NEXTBYTE(); \
    } while (0)

#define PEEKBITS(n) (buff & ((1u << (n)) - 1))

#define DROPBITS(n)          \
    do {                     \
        assert(bits >= (n)); \
        buff >>= (n);        \
        bits -= (n);         \
    } while (0)

int PZ_inflate(z_streamp strm, int flush) {
    // printf("pzlib::inflate\n");

    int ret = Z_OK;
    struct internal_state *state = strm->state;
    inflate_mode mode = state->mode;
    z_const Bytef *next = strm->next_in;
    uInt avail = strm->avail_in;
    uInt bits = state->bits;
    uLong buff = state->buff;
    uLong read = 0;
    uInt use;
    uint8_t id1, id2, cm;
    uint32_t mtime;
    uint16_t xlen, crc16;

    // auto PEEKBITS = [buff, bits](uInt n) -> uInt {
    //     assert(bits >= n);
    //     return buff & ((1u << n) - 1);
    // };

    if (strm->next_in == Z_NULL || strm->next_out == Z_NULL) {
        return Z_STREAM_ERROR;
    }

    switch (mode) {
        case HEADER:
            NEEDBITS(8 + 8 + 8);
            id1 = (buff >> 16) & 0xFFu;
            id2 = (buff >> 8) & 0xFFu;
            cm = (buff >> 0) & 0xFFu;
            if (id1 != 0x1Fu || id2 != 0x8Bu) {
                panic(Z_DATA_ERROR, "invalid gzip header bytes: 0x%02x 0x%02x",
                      id1, id2);
            }
            if (cm != 8) {
                panic(Z_DATA_ERROR, "invalid compression method: %u", cm);
            }

            DEBUG("GZIP HEADER");
            DEBUG("\tID1   = %3u (0x%02x)", id1, id1);
            DEBUG("\tID2   = %3u (0x%02x)", id2, id2);
            DEBUG("\tCM    = %3u", cm);
            DROPBITS(8 + 8 + 8);

            mode = FLAGS;
        case FLAGS:
            NEEDBITS(8);
            state->flag = PEEKBITS(8);
            DEBUG("\tFLG   = %3u", state->flag);
            DROPBITS(8);
            mode = MTIME;
        case MTIME:
            NEEDBITS(32);
            mtime = 0;
            mtime |= ((buff >>  0) & 0xFFu) << 24;
            mtime |= ((buff >>  8) & 0xFFu) << 16;
            mtime |= ((buff >> 16) & 0xFFu) <<  8;
            mtime |= ((buff >> 24) & 0xFFu) <<  0;
            DEBUG("\tMTIME = %u", mtime);
            DROPBITS(32);
            mode = XFL;
        case XFL:
            NEEDBITS(8);
            DEBUG("\tXFL   = %3lu", PEEKBITS(8));
            DROPBITS(8);
            mode = OS;
        case OS:
            NEEDBITS(8);
            DEBUG("\tOS    = %3lu", PEEKBITS(8));
            DROPBITS(8);
            mode = FEXTRA;
        case FEXTRA:
            state->temp = 0;
            if ((state->flag & (1u << 2)) != 0) {
                NEEDBITS(2*8);
                state->temp |= ((buff >> 0) & 0xFFu) << 8;
                state->temp |= ((buff >> 8) & 0xFFu) << 0;
            }
            mode = FEXTRA_DATA;
        case FEXTRA_DATA:
            while (state->temp > 0) {
                NEEDBITS(8);
                DROPBITS(8);
                state->temp--;
            }
            mode = FNAME;
        case FNAME:
            if ((state->flag & (1u << 3)) != 0) {
                for (;;) {
                    NEEDBITS(8);
                    state->filename.push_back((char)PEEKBITS(8));
                    DROPBITS(8);
                    if (state->filename.back() == '\0') {
                        state->filename.pop_back();
                        break;
                    }
                }
                DEBUG("Original Filename: '%s'", state->filename.c_str());
            }
            mode = FCOMMENT;
        case FCOMMENT:
            if ((state->flag & (1u << 4)) != 0) {
                for (;;) {
                    NEEDBITS(8);
                    if (PEEKBITS(8) == '\0') {
                        DROPBITS(8);
                        break;
                    }
                    DROPBITS(8);
                }
            }
            mode = FHCRC;
        case FHCRC:
            if ((state->flag & (1u << 1)) != 0) {
                crc16 = 0;
                crc16 |= ((buff >>  0) & 0xFFu) << 8;
                crc16 |= ((buff >>  8) & 0xFFu) << 0;
                NEEDBITS(16);
                DEBUG("\tCRC = %u", crc16);
                DROPBITS(16);
            }
            DEBUG("Finished parsing GZIP header");
            mode = BEGIN_BLOCK;
#if 0
        case BEGIN_BLOCK:
            DEBUG("BEGIN_BLOCK");
            if (have == 0) {
                if (avail == 0)
                    goto exit;
                --avail;
                last = *next
            }
#endif
        default:
            panic(Z_STREAM_ERROR, "state not implemented yet: %d", mode);
    }

exit:
    strm->next_in = next;
    strm->avail_in = avail;
    strm->total_in += read;
    state->bits = bits;
    state->buff = buff;
    state->mode = mode;

    return ret;
}

#endif
