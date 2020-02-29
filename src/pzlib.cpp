#include "pzlib.h"

#ifndef USE_ZLIB

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>  // TEMP TEMP

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
    BEGIN_BLOCK,
    NO_COMPRESSION,
    FIXED_HUFFMAN,
    DYNAMIC_HUFFMAN,
    NO_COMPRESSION_READ,
};
typedef enum inflate_mode inflate_mode;

struct internal_state {
    inflate_mode mode;
    uInt bits;  /* # of bits in bit accumulator */
    uLong buff; /* bit accumator */
    uLong temp;
    Byte flag;
    std::string filename;
    Byte blkfinal;
    Byte blktype;
    uInt len;
    int block_number;  // TEMP TEMP
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
    strm->state->blkfinal = 0;
    strm->state->blktype = 0;
    strm->state->block_number = 0;

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
        if (avail_in == 0) goto exit;    \
        avail_in--;                      \
        buff = (buff << 8) | *in++; \
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

#define DROPREMBYTE()      \
    do {                   \
        buff >>= bits & 7; \
        bits -= bits & 7;  \
    } while (0)

int PZ_inflate(z_streamp strm, int flush) {
    // printf("pzlib::inflate\n");

    int ret = Z_OK;
    struct internal_state *state = strm->state;
    inflate_mode mode = state->mode;
    z_const Bytef *in = strm->next_in;
    uInt avail_in = strm->avail_in;
    uLong read = 0;

    Bytef *out = strm->next_out;
    uInt avail_out = strm->avail_out;
    uLong wrote = 0;

    uInt bits = state->bits;
    uLong buff = state->buff;
    uInt use;
    uint8_t id1, id2, cm;
    uint32_t mtime;
    uint16_t xlen, crc16;
    uInt nlen;

    // auto PEEKBITS = [buff, bits](uInt n) -> uInt {
    //     assert(bits >= n);
    //     return buff & ((1u << n) - 1);
    // };

    if (in == Z_NULL || out == Z_NULL) {
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
            mtime |= ((buff >> 0) & 0xFFu) << 24;
            mtime |= ((buff >> 8) & 0xFFu) << 16;
            mtime |= ((buff >> 16) & 0xFFu) << 8;
            mtime |= ((buff >> 24) & 0xFFu) << 0;
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
                NEEDBITS(2 * 8);
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
                crc16 |= ((buff >> 0) & 0xFFu) << 8;
                crc16 |= ((buff >> 8) & 0xFFu) << 0;
                NEEDBITS(16);
                DEBUG("\tCRC = %u", crc16);
                DROPBITS(16);
            }
            DEBUG("Finished parsing GZIP header");
            mode = BEGIN_BLOCK;
        begin_block:
        case BEGIN_BLOCK:
            NEEDBITS(3);
            state->blkfinal = PEEKBITS(1);
            DROPBITS(1);
            state->blktype = PEEKBITS(2);
            DROPBITS(2);

            // DEBUG("Final Block: %u", state->blkfinal);
            // DEBUG("Block Type:  %u", state->blktype);
            if (state->blktype == 0x0u) {
                DEBUG("Block #%d Encoding: No Compression%s",
                      state->block_number++, state->blkfinal ? " -- final" : "");
                mode = NO_COMPRESSION;
                goto no_compression_block;
            } else if (state->blktype == 0x1u) {
                DEBUG("Block #%d Encoding: Fixed Huffman%s", state->block_number++, state->blkfinal ? " -- final" : "");
                // TEMP TEMP
                panic(Z_DATA_ERROR, "invalid block type");
                // mode = FIXED_HUFFMAN;
                // goto fixed_huffman_block;
            } else if (state->blktype == 0x2u) {
                DEBUG("Block #%d Encoding: Dynamic Huffman%s",
                      state->block_number++, state->blkfinal ? " -- final" : "");
                // TEMP TEMP
                panic(Z_DATA_ERROR, "invalid block type");
                // mode = DYNAMIC_HUFFMAN;
                // goto dynamic_huffman_block;
            } else {
                panic(Z_DATA_ERROR, "invalid block type: %u", state->blktype);
            }
        no_compression_block:
        case NO_COMPRESSION:
            DROPREMBYTE();
            NEEDBITS(32);
                   nlen = (((buff >>  8) & 0xFFu) << 0) | (((buff >>  0) & 0xFFu) << 8);
            state->len  = (((buff >> 24) & 0xFFu) << 0) | (((buff >> 16) & 0xFFu) << 8);
            DROPBITS(32);
            DEBUG("len = %u nlen = %u", state->len, nlen);
            if ((state->len & 0xFFFFu) != (nlen ^ 0xFFFFu)) {
                panic(Z_DATA_ERROR, "invalid stored block lengths: %u %u",
                      state->len, nlen);
            }
            mode = NO_COMPRESSION_READ;
        case NO_COMPRESSION_READ:
            // precondition: on a byte boundary
            // NOTE: switching to byte-oriented reading/writing
            {
                // should be on a byte boundary at this point after flush to byte boundary
                // then 2 x 2B reads
                assert(bits % 8 == 0);
                int bytes = bits / 8;
                int amount = std::min<int>(bytes, std::min<int>(state->len, avail_out));
                state->len -= amount;
                avail_out  -= amount;
                while (amount-- > 0) {
                    *out++ = (Bytef)PEEKBITS(8);
                    DROPBITS(8);
                }
                assert(bits == 0);
                amount = std::min<int>(avail_out, std::min<int>(state->len, avail_in));
                state->len -= amount;
                avail_in   -= amount;
                avail_out  -= amount;
                read       += amount;
                wrote      += amount;
                while (amount-- > 0) {
                    *out++ = *in++;
                }
                assert(state->len == 0 || (avail_in == 0 || avail_out == 0));
                if (state->len != 0)
                    goto exit;
            }

            assert(state->len == 0);
            if (state->blkfinal) {
                ret = Z_STREAM_END;
                goto exit;
            } else {
                mode = BEGIN_BLOCK;
                // TODO(peter): why doesn't this work? -- less efficient, but it *should* work...
                // goto exit;
                goto begin_block;
            }

        // fixed_huffman_block:
        // case FIXED_HUFFMAN:
        // dynamic_huffman_block:
        // case DYNAMIC_HUFFMAN:

        default:
            panic(Z_STREAM_ERROR, "state not implemented yet: %d", mode);
    }

exit:
    // TODO(peter): remove `read` and `wrote` because already have that information.
    assert(avail_in  <= strm->avail_in);
    assert(avail_out <= strm->avail_out);
    assert(avail_in + read == strm->avail_in);
    assert(avail_out + wrote == strm->avail_out);
    strm->next_in = in;
    strm->avail_in = avail_in;
    strm->total_in += read;
    strm->next_out = out;
    strm->avail_out = avail_out;
    strm->total_out += wrote;
    state->bits = bits;
    state->buff = buff;
    state->mode = mode;

    return ret;
}

#endif
