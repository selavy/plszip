#include "pzlib.h"

#ifndef USE_ZLIB

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef NDEBUG
#define DEBUG
#else
#define DEBUG(fmt, ...) fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__);
#endif

enum inflate_mode {
    HEADER, /* | ID1 | ID2 | CM | */
    FLAGS,  /* FLG */
    MTIME,  /* MTIME */
    XFL,    /* XFL */
    OS,     /* OS */
    FEXTRA, /* FEXTRA fields */
    FNAME,
    FCOMMENT,
    FHCRC,
    BEGIN_BLOCK
};
typedef enum inflate_mode inflate_mode;

struct internal_state {
    inflate_mode mode;
    uInt have;
    uLong last;
    Byte flg;
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
    strm->state->last = 0UL;

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

int PZ_inflate(z_streamp strm, int flush) {
    printf("pzlib::inflate\n");

    int ret = Z_OK;
    struct internal_state *state = strm->state;
    inflate_mode mode = state->mode;
    z_const Bytef *next = strm->next_in;
    uInt avail = strm->avail_in;
    uInt have = state->have;
    uLong last = state->last;
    uLong read = 0;
    uInt use;

    if (strm->next_in == Z_NULL || strm->next_out == Z_NULL) {
        return Z_STREAM_ERROR;
    }

    switch (mode) {
        case HEADER:
            while (have < 3) {
                if (avail == 0) {
                    goto exit;
                }
                --avail;
                ++read;
                ++have;
                last = (last << 8) | *next++;
            }

            if (
                    ((last >> 16) & 0xFFu) != 0x1Fu ||
                    ((last >>  8) & 0xFFu) != 0x8Bu
            ) {
                panic(Z_DATA_ERROR, "invalid gzip header bytes: 0x%02x 0x%02x",
                        (uInt)((last >> 16) & 0xFFu),
                        (uInt)((last >>  8) & 0xFFu));
            }
            if ((last & 0xFFu) != 8) {
                panic(Z_DATA_ERROR, "invalid compression method: %u", (uInt)(last & 0xFFu));
            }

            DEBUG("GZIP HEADER");
            DEBUG("\tID1   = %lu (0x%02lx)", ((last >> 16) & 0xFFu), ((last >> 16) & 0xFFu));
            DEBUG("\tID2   = %lu (0x%02lx)", ((last >>  8) & 0xFFu), ((last >> 16) & 0xFFu));
            DEBUG("\tCM    = %lu", ((last >>  0) & 0xFFu));
            mode = FLAGS;
        case FLAGS:
            if (avail == 0)
                goto exit;
            --avail;
            ++read;
            state->flg = *next++;
            DEBUG("\tFLG   = %u", state->flg);
            have = 0;
            last = 0;
            mode = MTIME;
        case MTIME:
            while (have < 4) {
                if (avail == 0)
                    goto exit;
                --avail;
                ++read;
                last |= *next++ << (8*have++);
            }
            DEBUG("\tMTIME = %lu", last);
            have = 0;
            last = 0;
            mode = XFL;
        case XFL:
            if (avail == 0)
                goto exit;
            --avail;
            ++read;
            last = *next++;
            DEBUG("\tXFL   = %lu", last);
            mode = OS;
        case OS:
            if (avail == 0)
                goto exit;
            --avail;
            ++read;
            last = *next++;
            DEBUG("\tOS    = %lu", last);
            mode = FEXTRA;
            last = 0;
            have = 0;
        case FEXTRA:
            if ((state->flg & (1u << 2)) != 0) {
                while (have < 2) {
                    if (avail == 0)
                        goto exit;
                    --avail;
                    ++read;
                    last |= *next++ << (8*have++);
                }
                DEBUG("\tXLEN = %lu", last);
                while (last > 0) {
                    if (avail == 0)
                        goto exit;
                    --avail;
                    --last;
                }
            }
            last = 0;
            have = 0;
            mode = FNAME;
        case FNAME:
            if ((state->flg & (1u << 3)) != 0) {
                for (;;) {
                    if (avail == 0)
                        goto exit;
                    --avail;
                    if (*next++ == '\0')
                        break;
                }
            }
            mode = FCOMMENT;
        case FCOMMENT:
            if ((state->flg & (1u << 4)) != 0) {
                for (;;) {
                    if (avail == 0)
                        goto exit;
                    --avail;
                    if (*next++ == '\0')
                        break;
                }
            }
            mode = FHCRC;
        case FHCRC:
            if ((state->flg & (1u << 1)) != 0) {
                while (have < 2) {
                    if (avail == 0)
                        goto exit;
                    --avail;
                    last |= *next++ << (8*have++);
                }
                DEBUG("\tCRC = %lu", last);
                have = 0;
                last = 0;
            }
            DEBUG("Finished parsing GZIP header");
            mode = BEGIN_BLOCK;
        case BEGIN_BLOCK:
            DEBUG("BEGIN_BLOCK");
        default:
            panic(Z_STREAM_ERROR, "state not implemented yet: %d", mode);
    }

exit:
    strm->next_in = next;
    strm->avail_in = avail;
    strm->total_in += read;
    state->have = have;
    state->last = last;
    state->mode = mode;

    return ret;
}

#endif
