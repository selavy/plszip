#include "pzlib.h"

#ifndef USE_ZLIB

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>  // TEMP TEMP

#include "fixed_huffman_trees.h"

#define UNREACHABLE()            \
    do {                         \
        assert(0);               \
        __builtin_unreachable(); \
    } while (0)

#ifdef NDEBUG
#define DEBUG(fmt, ...)
#else
#define DEBUG(fmt, ...) fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__);
#endif

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

/* Global Static Data */
static constexpr size_t LENGTH_BASE_CODE = 257;
static constexpr size_t LENGTH_EXTRA_BITS[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};

static constexpr size_t LENGTH_BASES[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static_assert(ARRSIZE(LENGTH_EXTRA_BITS) == ARRSIZE(LENGTH_BASES));

static constexpr size_t DISTANCE_EXTRA_BITS[32] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 0, 0,
};

static constexpr size_t DISTANCE_BASES[32] = {
    1,   2,   3,   4,   5,    7,    9,    13,   17,   25,   33,   49,    65,    97,    129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0,   0,
};

static constexpr size_t WINDOW_SIZE = 1u << 16;

/* Internal Types */
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
    // DYNAMIC_HUFFMAN,
    NO_COMPRESSION_READ,
    HUFFMAN_READ,
    WRITE_HUFFMAN_VALUE,
    HUFFMAN_LENGTH_CODE,
    HUFFMAN_DISTANCE_CODE,
    READ_HUFFMAN_DISTANCE,
    WRITE_HUFFMAN_LEN_DIST,
    END_BLOCK,
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
    const uint16_t *lits;
    size_t litlen;
    const uint16_t *dists;
    size_t distlen;
    size_t length;
    size_t index;

    /* circular buffer window */
    uint32_t mask;
    uint32_t head;
    uint32_t size;
    Bytef *wnd;  // TODO(peter): just put at end of internal_state with window[1]
};

voidpf zcalloc(voidpf opaque, uInt items, uInt size) {
    (void)opaque;
    return calloc(items, size);
}

void zcfree(voidpf opaque, voidpf ptr) {
    (void)opaque;
    free(ptr);
}

const char *zlibVersion() { return "pzlib 0.0.1"; }

int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size) {
    printf("pzlib::inflateInit2_\n");
    if (strcmp(version, ZLIB_VERSION) != 0) {
        return Z_VERSION_ERROR;
    }
    if (static_cast<size_t>(stream_size) != sizeof(z_stream)) {
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

    void *mem = strm->zalloc(strm->opaque, 1, sizeof(internal_state));
    if (!mem) {
        strm->msg = "failed to allocate memory for internal state";
        return Z_MEM_ERROR;
    }
    // TEMP TEMP: only have to do this because have std::string
    strm->state = new (mem) internal_state{};

    // strm->state = reinterpret_cast<internal_state *>(strm->zalloc(strm->opaque, 1, sizeof(struct internal_state)));
    // if (!strm->state) {
    //     strm->msg = "failed to allocate memory for internal state";
    //     return Z_MEM_ERROR;
    // }

    strm->state->mode = HEADER;
    strm->state->buff = 0UL;
    strm->state->bits = 0;
    strm->state->flag = 0;
    strm->state->temp = 0;
    strm->state->blkfinal = 0;
    strm->state->blktype = 0;
    strm->state->block_number = 0;
    strm->state->lits = NULL;
    strm->state->litlen = 0;
    strm->state->dists = NULL;
    strm->state->distlen = 0;
    strm->state->length = 0;
    strm->state->index = 0;

    // TODO(peter): use windowBits instead
    strm->state->wnd = reinterpret_cast<Bytef *>(strm->zalloc(strm->opaque, sizeof(Bytef), WINDOW_SIZE));
    if (!strm->state->wnd) {
        strm->msg = "failed to allocate memory for window";
        return Z_MEM_ERROR;
    }
    strm->state->mask = WINDOW_SIZE - 1;
    strm->state->head = 0;
    strm->state->size = 0;
    // TEMP TEMP
    memset(strm->state->wnd, 0, sizeof(strm->state->wnd[0]) * WINDOW_SIZE);

    return Z_OK;
}

static void windowAdd(internal_state *s, const Bytef *buf, uint32_t n) {
    s->size = std::min(s->size + n, s->mask + 1);
    size_t n1 = std::min(s->mask + 1 - s->head, n);
    size_t n2 = n - n1;
    Bytef *p = &s->wnd[s->head];
    while (n1-- > 0) {
        *p++ = *buf++;
    }
    p = &s->wnd[0];
    while (n2-- > 0) {
        *p++ = *buf++;
    }
    s->head = (s->head + n) & s->mask;
}

static bool checkDistance(const internal_state *s, size_t distance) {
    return distance <= s->size && distance <= s->mask;
}

int inflateEnd(z_streamp strm) {
    printf("pzlib::inflateEnd\n");
    if (strm->state) {
        strm->zfree(strm->opaque, strm->state->wnd);
        strm->state->wnd = Z_NULL;
        strm->zfree(strm->opaque, strm->state);
    }
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
        if (avail_in == 0) goto exit; \
        avail_in--;                   \
        buff = (buff << 8) | *in++;   \
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

    (void)flush;

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
    uint8_t id1, id2, cm;
    uint32_t mtime;
    uInt crc16;
    uInt nlen;
    uint16_t val;

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
            panic(Z_DATA_ERROR, "invalid gzip header bytes: 0x%02x 0x%02x", id1, id2);
        }
        if (cm != 8) {
            panic(Z_DATA_ERROR, "invalid compression method: %u", cm);
        }

        // TODO(peter): figure out solution for:
        // "error: ISO C++11 requires at least one argument for the "..." in
        // a variadic macro [-Werror]"
        DEBUG("%s", "GZIP HEADER");
        DEBUG("\tID1   = %3u (0x%02x)", id1, id1);
        DEBUG("\tID2   = %3u (0x%02x)", id2, id2);
        DEBUG("\tCM    = %3u", cm);
        DROPBITS(8 + 8 + 8);

        mode = FLAGS;
        goto flags;
        break;
    flags:
    case FLAGS:
        NEEDBITS(8);
        state->flag = PEEKBITS(8);
        DEBUG("\tFLG   = %3u", state->flag);
        DROPBITS(8);
        mode = MTIME;
        goto mtime;
        break;
    mtime:
    case MTIME:
        NEEDBITS(32);
        mtime = 0;
        mtime |= static_cast<uint32_t>(((buff >> 0) & 0xFFu) << 24);
        mtime |= static_cast<uint32_t>(((buff >> 8) & 0xFFu) << 16);
        mtime |= static_cast<uint32_t>(((buff >> 16) & 0xFFu) << 8);
        mtime |= static_cast<uint32_t>(((buff >> 24) & 0xFFu) << 0);
        DEBUG("\tMTIME = %u", mtime);
        DROPBITS(32);
        mode = XFL;
        goto xfl;
        break;
    xfl:
    case XFL:
        NEEDBITS(8);
        DEBUG("\tXFL   = %3lu", PEEKBITS(8));
        DROPBITS(8);
        mode = OS;
        goto os;
        break;
    os:
    case OS:
        NEEDBITS(8);
        DEBUG("\tOS    = %3lu", PEEKBITS(8));
        DROPBITS(8);
        mode = FEXTRA;
        goto fextra;
        break;
    fextra:
    case FEXTRA:
        state->temp = 0;
        if ((state->flag & (1u << 2)) != 0) {
            NEEDBITS(2 * 8);
            state->temp |= ((buff >> 0) & 0xFFu) << 8;
            state->temp |= ((buff >> 8) & 0xFFu) << 0;
        }
        mode = FEXTRA_DATA;
        goto fextra_data;
        break;
    fextra_data:
    case FEXTRA_DATA:
        while (state->temp > 0) {
            NEEDBITS(8);
            DROPBITS(8);
            state->temp--;
        }
        mode = FNAME;
        goto fname;
        break;
    fname:
    case FNAME:
        if ((state->flag & (1u << 3)) != 0) {
            for (;;) {
                NEEDBITS(8);
                state->filename.push_back(static_cast<char>(PEEKBITS(8)));
                DROPBITS(8);
                if (state->filename.back() == '\0') {
                    state->filename.pop_back();
                    break;
                }
            }
            DEBUG("Original Filename: '%s'", state->filename.c_str());
        }
        mode = FCOMMENT;
        goto fcomment;
        break;
    fcomment:
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
        goto fhcrc;
        break;
    fhcrc:
    case FHCRC:
        if ((state->flag & (1u << 1)) != 0) {
            // REVISIT(peter): this is a bit ridiculous just to satisfy the spurious
            // warnings. The issue is that the shift operators promote to an integer...
            NEEDBITS(16);
            (void)crc16;  // unused variable in release mode
            const uInt bytes[2] = {
                static_cast<uInt>(buff >> 0) & 0xFFu,
                static_cast<uInt>(buff >> 8) & 0xFFu,
            };
            crc16 = bytes[1] | bytes[0];
            DEBUG("\tCRC = %u", crc16);
            DROPBITS(16);
        }
        // TODO(peter): figure out solution for:
        // "error: ISO C++11 requires at least one argument for the "..." in
        // a variadic macro [-Werror]"
        DEBUG("%s", "Finished parsing GZIP header");
        mode = BEGIN_BLOCK;
        goto begin_block;
        break;
    begin_block:
    case BEGIN_BLOCK:
        NEEDBITS(3);
        state->blkfinal = PEEKBITS(1);
        DROPBITS(1);
        state->blktype = PEEKBITS(2);
        DROPBITS(2);
        if (state->blktype == 0x0u) {
            DEBUG("Block #%d Encoding: No Compression%s", state->block_number, state->blkfinal ? " -- final" : "");
            state->block_number++;
            mode = NO_COMPRESSION;
            goto no_compression_block;
        } else if (state->blktype == 0x1u) {
            DEBUG("Block #%d Encoding: Fixed Huffman%s", state->block_number, state->blkfinal ? " -- final" : "");
            state->block_number++;
            mode = FIXED_HUFFMAN;
            goto fixed_huffman_block;
        } else if (state->blktype == 0x2u) {
            DEBUG("Block #%d Encoding: Dynamic Huffman%s", state->block_number, state->blkfinal ? " -- final" : "");
            state->block_number++;
            // TEMP TEMP
            panic(Z_DATA_ERROR, "invalid block type: %u", state->blktype);
            // mode = DYNAMIC_HUFFMAN;
            // goto dynamic_huffman_block;
        } else {
            panic(Z_DATA_ERROR, "invalid block type: %u", state->blktype);
        }
        UNREACHABLE();
        break;
    no_compression_block:
    case NO_COMPRESSION:
        DROPREMBYTE();
        NEEDBITS(32);
        nlen = static_cast<uint16_t>((((buff >> 8) & 0xFFu) << 0) | (((buff >> 0) & 0xFFu) << 8));
        state->len = static_cast<uint16_t>((((buff >> 24) & 0xFFu) << 0) | (((buff >> 16) & 0xFFu) << 8));
        DROPBITS(32);
        DEBUG("len = %u nlen = %u", state->len, nlen);
        if ((state->len & 0xFFFFu) != (nlen ^ 0xFFFFu)) {
            panic(Z_DATA_ERROR, "invalid stored block lengths: %u %u", state->len, nlen);
        }
        mode = NO_COMPRESSION_READ;
        goto no_compression_read;
        break;
    no_compression_read:
    case NO_COMPRESSION_READ:
        // precondition: on a byte boundary
        // NOTE: switching to byte-oriented reading/writing
        {
            // should be on a byte boundary at this point after flush to
            // byte boundary then 2 x 2B reads
            assert(bits % 8 == 0);
            uInt bytes = bits / 8;
            uInt amount = std::min<uInt>(bytes, std::min<uInt>(state->len, avail_out));
            state->len -= amount;
            avail_out -= amount;
            while (amount-- > 0) {
                *out++ = static_cast<Bytef>(PEEKBITS(8));
                DROPBITS(8);
            }
            assert(bits == 0);
            amount = std::min<uInt>(avail_out, std::min<uInt>(state->len, avail_in));
            state->len -= amount;
            avail_in -= amount;
            avail_out -= amount;
            read += amount;
            wrote += amount;
            while (amount-- > 0) {
                windowAdd(state, in, sizeof(in));
                DEBUG("WRITING VALUE(%d): %d '%c'", __LINE__, static_cast<int>(*in), *in);
                *out++ = *in++;
            }
            assert(state->len == 0 || (avail_in == 0 || avail_out == 0));
            if (state->len != 0) goto exit;
        }

        assert(state->len == 0);
        goto end_block;
        // if (state->blkfinal) {
        //     ret = Z_STREAM_END;
        //     goto exit;
        // } else {
        //     mode = BEGIN_BLOCK;
        //     // TODO(peter): why doesn't this work? -- less efficient, but it
        //     // *should* work... goto exit;
        //     goto begin_block;
        // }
        UNREACHABLE();
        break;
    fixed_huffman_block:
    case FIXED_HUFFMAN:
        state->lits = fixed_huffman_literals_tree;
        state->litlen = sizeof(fixed_huffman_literals_tree);
        state->dists = fixed_huffman_distance_tree;
        state->distlen = sizeof(fixed_huffman_distance_tree);
        mode = HUFFMAN_READ;
        state->temp = 1;
        goto huffman_read;
        break;
    huffman_read:
    case HUFFMAN_READ:
        // REVISIT(peter): this is the very slow path
        while (state->lits[state->temp] == EMPTY_SENTINEL) {
            NEEDBITS(1);
            state->temp = (state->temp << 1) | PEEKBITS(1);
            DROPBITS(1);
            assert(state->temp < state->litlen);
        }
        assert(state->temp < state->litlen);
        assert(state->lits[state->temp] != EMPTY_SENTINEL);
        val = state->lits[state->temp];
        if (val < 256) {
            Bytef c = static_cast<Bytef>(val);
            DEBUG("WRITING VALUE(%d): %d '%c'", __LINE__, static_cast<int>(c), c);
            windowAdd(state, &c, sizeof(c));
            *out++ = c;
            avail_out--;
            wrote++;
            state->temp = 1;
            mode = HUFFMAN_READ;  // TEMP TEMP: unneeded
            goto huffman_read;
        } else if (val == 256) {
            DEBUG("inflate: end of %s huffman block found", "fixed");
            // TODO: deallocate tables if needed for dynamic huffman
            goto end_block;
        } else if (val <= 285) {
            assert(257 <= val && val <= 285);
            state->temp = val - LENGTH_BASE_CODE;
            mode = HUFFMAN_LENGTH_CODE;
            goto huffman_length_code;
        } else {
            panic(Z_DATA_ERROR, "invalid huffman value: %u", val);
        }
        UNREACHABLE();
        break;
        // dynamic_huffman_block:
        // case DYNAMIC_HUFFMAN:
    huffman_length_code:
    case HUFFMAN_LENGTH_CODE:
        NEEDBITS(LENGTH_EXTRA_BITS[state->temp]);
        {
            assert(state->temp < ARRSIZE(LENGTH_BASES));
            size_t base_length = LENGTH_BASES[state->temp];
            size_t extra_length = PEEKBITS(LENGTH_EXTRA_BITS[state->temp]);
            size_t length = base_length + extra_length;
            DEBUG("value=%lu, base_length=%zu extra_length=%zu length=%zu", state->temp, base_length, extra_length,
                  length);
            state->length = length;
            state->temp = 1;
            mode = HUFFMAN_DISTANCE_CODE;
            goto huffman_distance_code;
        }
        UNREACHABLE();
        break;
    huffman_distance_code:
    case HUFFMAN_DISTANCE_CODE:
        while (state->dists[state->temp] == EMPTY_SENTINEL) {
            NEEDBITS(1);
            state->temp = (state->temp << 1) | PEEKBITS(1);
            DROPBITS(1);
            assert(state->temp < state->distlen);
        }
        assert(state->temp < state->distlen);
        assert(state->dists[state->temp] != EMPTY_SENTINEL);
        mode = READ_HUFFMAN_DISTANCE;
        goto read_huffman_distance;
        break;
    read_huffman_distance:
    case READ_HUFFMAN_DISTANCE:
        val = state->dists[state->temp];
        assert(val < 32);  // invalid distance code
        NEEDBITS(DISTANCE_EXTRA_BITS[val]);
        {
            size_t base_distance = DISTANCE_BASES[val];
            size_t extra_distance = PEEKBITS(DISTANCE_EXTRA_BITS[val]);
            size_t distance = base_distance + extra_distance;
            DEBUG("value=%u, base_dist=%zu extra_dist=%zu dist=%zu", val, base_distance, extra_distance, distance);
            if (!checkDistance(state, distance)) panic(Z_DATA_ERROR, "invalid distance %zu", distance);
            state->index = (state->head + ((state->mask + 1) - distance));
            mode = WRITE_HUFFMAN_LEN_DIST;
            goto write_huffman_len_dist;
        }
        break;
    write_huffman_len_dist:
    case WRITE_HUFFMAN_LEN_DIST:
        while (state->length > 0) {
            if (avail_out == 0) goto exit;
            Bytef c = state->wnd[state->index & state->mask];
            *out++ = c;
            avail_out--;
            wrote++;
            DEBUG("WRITING VALUE(%d): %d '%c'", __LINE__, static_cast<int>(c), c);
            windowAdd(state, &c, sizeof(c));
            state->index++;
            state->length--;
        }
        state->temp = 1;
        mode = HUFFMAN_READ;
        goto huffman_read;
        break;
    end_block:
    case END_BLOCK:
        if (state->blkfinal) {
            ret = Z_STREAM_END;
            goto exit;
        } else {
            mode = BEGIN_BLOCK;
            goto begin_block;
        }
    default:
        panic(Z_STREAM_ERROR, "state not implemented yet: %d", mode);
    }

exit:
    // TODO(peter): remove `read` and `wrote` because already have that
    // information.
    assert(avail_in <= strm->avail_in);
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
