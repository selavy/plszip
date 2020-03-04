#include "pzlib.h"

// TODO: faster huffman code reading using linked-listed LUT implementation
// TODO: can I remove the allocation for the dynamic trees?

#ifndef USE_ZLIB

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "crc32.h"
#include "fixed_huffman_trees.h"

#define UNREACHABLE()            \
    do {                         \
        assert(0);               \
        __builtin_unreachable(); \
    } while (0)

#ifdef NDEBUG
#define DEBUG0(msg)
#define DEBUG(fmt, ...)
#else
#define DEBUG0(msg) fprintf(stderr, "DEBUG: " msg "\n");
#define DEBUG(fmt, ...) fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__);
#endif

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

#define AS_U32(x) static_cast<uint32_t>((x)&0xFFFFFFFFu)
#define AS_U16(x) static_cast<uint16_t>((x)&0xFFFFu)
#define AS_U8(x) static_cast<uint16_t>((x)&0xFFu)

/* Global Static Data */
static constexpr uint16_t LENGTH_BASE_CODE = 257;
static constexpr uInt LENGTH_EXTRA_BITS[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};

static constexpr size_t LENGTH_BASES[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static_assert(ARRSIZE(LENGTH_EXTRA_BITS) == ARRSIZE(LENGTH_BASES));

static constexpr uInt DISTANCE_EXTRA_BITS[32] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 0, 0,
};

static constexpr size_t DISTANCE_BASES[32] = {
    1,   2,   3,   4,   5,    7,    9,    13,   17,   25,   33,   49,    65,    97,    129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0,   0,
};

static constexpr size_t WINDOW_SIZE = 1u << 16;

static constexpr size_t NUM_CODE_LENGTHS = 19;
static size_t order[NUM_CODE_LENGTHS] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
static constexpr size_t MAX_HTREE_BIT_LENGTH = 7;
static constexpr size_t HTREE_TABLE_SIZE = 1u << MAX_HTREE_BIT_LENGTH;
static constexpr size_t MAX_CODE_LENGTHS = 322;

/* Internal Types */
enum inflate_mode {
    HEADER, /* ID1 | ID2 | CM */
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
    HUFFMAN_READ,
    WRITE_HUFFMAN_VALUE,
    HUFFMAN_LENGTH_CODE,
    READ_HUFFMAN_DISTANCE_CODE,
    HUFFMAN_DISTANCE_CODE,
    WRITE_HUFFMAN_LEN_DIST,
    HEADER_TREE,
    DYNAMIC_CODE_LENGTHS,
    END_BLOCK,
    CHECK_CRC32,
    CHECK_ISIZE,
};
typedef enum inflate_mode inflate_mode;

struct internal_state {
    inflate_mode mode;
    gz_header *head;
    uInt bits;   // # of bits in bit accumulator
    uLong buff;  // bit accumator
    Byte flags;
    Byte blkfinal;
    Byte blktype;
    uInt len;
    int block_number;  // TEMP TEMP
    const uint16_t *lits;
    // either points to `fixed_huffman_literals_codelens` or into `dynlens`
    const uint16_t *litlens;
    size_t litmaxlen;
    const uint16_t *dsts;
    // either points to `fixed_huffman_distance_codelens` or into `dynlens`
    const uint16_t *dstlens;
    size_t dstmaxlen;
    size_t index;
    size_t hlit;
    size_t hdist;
    size_t hclen;
    uint16_t *dynlits;
    uint16_t *dyndsts;

    uint16_t htree[HTREE_TABLE_SIZE];
    uint16_t dynlens[MAX_CODE_LENGTHS];
    uint16_t hlengths[NUM_CODE_LENGTHS];

    /* circular buffer window */
    uint32_t wnd_mask;
    uint32_t wnd_head;
    uint32_t wnd_size;
    Bytef wnd[1];
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

// avoiding bringing in <algorithm> just for std::max<>
uint16_t max_u16(uint16_t x, uint16_t y) noexcept {
    return x > y ? x : y;
}

uint32_t min_u32(uint32_t x, uint32_t y) noexcept {
    return x < y ? x : y;
}

uint16_t max_length(const uint16_t* first, const uint16_t* last) noexcept {
    uint16_t result = 0;
    while (first != last) {
        result = max_u16(*first++, result);
    }
    return result;
}

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

    strm->adler = 0;

    size_t window_size = (1u << windowBits);
    size_t window_bytes = sizeof(Bytef) * window_size;
    size_t alloc_size = sizeof(internal_state) + window_bytes;
    void *mem = strm->zalloc(strm->opaque, 1, static_cast<uInt>(alloc_size));
    if (!mem) {
        strm->msg = "failed to allocate memory for internal state";
        return Z_MEM_ERROR;
    }
    strm->state = reinterpret_cast<internal_state*>(mem);
    // strm->state = new (mem) internal_state{};
    strm->state->mode = HEADER;
    strm->state->buff = 0UL;
    strm->state->bits = 0;
    strm->state->flags = 0;
    strm->state->blkfinal = 0;
    strm->state->blktype = 0;
    strm->state->block_number = 0;
    strm->state->lits = nullptr;
    strm->state->litlens = nullptr;
    strm->state->litmaxlen = 0;
    strm->state->dsts = nullptr;
    strm->state->dstlens = nullptr;
    strm->state->dstmaxlen = 0;
    strm->state->index = 0;
    strm->state->hlit = 0;
    strm->state->hdist = 0;
    strm->state->hclen = 0;
    strm->state->dynlits = nullptr;
    strm->state->dyndsts = nullptr;
    strm->state->wnd_mask = static_cast<uint32_t>(window_size - 1);
    strm->state->wnd_head = 0;
    strm->state->wnd_size = 0;
    return Z_OK;
}

static void windowAdd(internal_state *s, const Bytef *buf, uint32_t n) {
    s->wnd_size = min_u32(s->wnd_size + n, s->wnd_mask + 1);
    size_t n1 = min_u32(s->wnd_mask + 1 - s->wnd_head, n);
    size_t n2 = n - n1;
    Bytef *p = &s->wnd[s->wnd_head];
    while (n1-- > 0) {
        *p++ = *buf++;
    }
    p = &s->wnd[0];
    while (n2-- > 0) {
        *p++ = *buf++;
    }
    s->wnd_head = (s->wnd_head + n) & s->wnd_mask;
}

static bool checkDistance(const internal_state *s, size_t distance) {
    return distance <= s->wnd_size && distance <= s->wnd_mask;
}

int inflateEnd(z_streamp strm) {
    printf("pzlib::inflateEnd\n");
    if (strm->state) {
        if (strm->state->dynlits) {
            strm->zfree(strm->opaque, strm->state->dynlits);
            strm->state->dynlits = nullptr;
        }
        if (strm->state->dyndsts) {
            strm->zfree(strm->opaque, strm->state->dyndsts);
            strm->state->dyndsts = nullptr;
        }
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

#define panic0(rc, msg_)                        \
    do {                                        \
        snprintf(msgbuf, sizeof(msgbuf), msg_); \
        strm->msg = msgbuf;                     \
        ret = rc;                               \
        goto exit;                              \
    } while (0)

#define NEXTBYTE()                                 \
    do {                                           \
        if (avail_in == 0) goto exit;              \
        avail_in--;                                \
        buff += static_cast<uLong>(*in++) << bits; \
        read++;                                    \
        bits += 8;                                 \
    } while (0)

#define NEEDBITS(n)                     \
    do {                                \
        assert((n) < 8 * sizeof(buff)); \
        while (bits < (n)) NEXTBYTE();  \
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

static const unsigned char BitReverseTable256[256] = {
// clang-format off
#   define R2(n)     n,     n + 2*64,     n + 1*64,     n + 3*64
#   define R4(n) R2(n), R2(n + 2*16), R2(n + 1*16), R2(n + 3*16)
#   define R6(n) R4(n), R4(n + 2*4 ), R4(n + 1*4 ), R4(n + 3*4 )
    R6(0), R6(2), R6(1), R6(3)
#undef R2
#undef R4
#undef R6
    // clang-format on
};

uint16_t flip_u16(uint16_t v) noexcept {
    // clang-format off
    return static_cast<uint16_t>(
        (BitReverseTable256[(v >> 0) & 0xff] << 8) |
        (BitReverseTable256[(v >> 8) & 0xff] << 0)
    );
    // clang-format on
}

uint16_t flip_code(uint16_t code, size_t codelen) {
    assert(0 < codelen && codelen <= 16);
    return static_cast<uint16_t>(flip_u16(code) >> (16 - codelen));
}

static void init_huffman_tree(uint16_t *tree, const size_t maxlen, const uint16_t *codelens, size_t ncodes) {
    constexpr size_t MAX_HCODE_BIT_LENGTH = 16;
    constexpr size_t MAX_HUFFMAN_CODES = 512;
    size_t bl_count[MAX_HCODE_BIT_LENGTH];
    uint16_t next_code[MAX_HCODE_BIT_LENGTH];
    uint16_t codes[MAX_HUFFMAN_CODES];

    assert(ncodes < MAX_HUFFMAN_CODES);

    {
        // 1) Count the number of codes for each code length. Let bl_count[N] be the
        // number of codes of length N, N >= 1.
        memset(&bl_count[0], 0, sizeof(bl_count));
        for (size_t i = 0; i < ncodes; ++i) {
            assert(codelens[i] <= MAX_HCODE_BIT_LENGTH);
            assert(codelens[i] <= maxlen);
            ++bl_count[codelens[i]];
        }
        bl_count[0] = 0;
    }

    {
        // 2) Find the numerical value of the smallest code for each code length:
        memset(&next_code[0], 0, sizeof(next_code));
        uint16_t code = 0;
        for (size_t bits = 1; bits <= maxlen; ++bits) {
            code = static_cast<uint16_t>((code + bl_count[bits - 1]) << 1);
            next_code[bits] = code;
        }
    }

    {
        // 3) Assign numerical values to all codes, using consecutive values for all
        // codes of the same length with the base values determined at step 2. Codes
        // that are never used (which have a bit length of zero) must not be
        // assigned a value.
        memset(&codes[0], 0, sizeof(codes));
        for (size_t i = 0; i < ncodes; ++i) {
            if (codelens[i] != 0) {
                codes[i] = next_code[codelens[i]]++;
                assert((16 - __builtin_clz(codes[i])) <= codelens[i]);
            }
        }
    }

    {
        // 4) Generate dense table. This means that can read `max_bit_length` bits at a
        // time, and do a lookup immediately; should then use `code_lengths` to
        // determine how many of the peek'd bits should be removed.
        memset(tree, 0xff, (1u << maxlen) * sizeof(tree[0]));
        for (size_t i = 0; i < ncodes; ++i) {
            if (codelens[i] == 0) continue;
            uint16_t code = codes[i];
            uint16_t codelen = codelens[i];
            uint16_t value = static_cast<uint16_t>(i);
            size_t empty_bits = maxlen - codelen;
            code = static_cast<uint16_t>(code << empty_bits);
            uint16_t lowbits = static_cast<uint16_t>((1u << empty_bits) - 1);
            uint16_t maxcode = code | lowbits;
            while (code <= maxcode) {
                uint16_t flipped = flip_code(code, maxlen);
                assert(tree[flipped] == 0xffffu);
                tree[flipped] = value;
                ++code;
            }
        }
    }
}

#define CHECK_IO()                                    \
    do {                                              \
        assert(avail_in <= strm->avail_in);           \
        assert(avail_out <= strm->avail_out);         \
        assert(avail_in + read == strm->avail_in);    \
        assert(avail_out + wrote == strm->avail_out); \
    } while (0)

int PZ_inflate(z_streamp strm, int flush) {
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
    uint16_t value;
    uInt extra;

    if (in == Z_NULL || out == Z_NULL) {
        return Z_STREAM_ERROR;
    }

    switch (mode) {
    case HEADER:
        NEEDBITS(8 + 8 + 8 + 8);
        id1 = PEEKBITS(8);
        DROPBITS(8);
        id2 = PEEKBITS(8);
        DROPBITS(8);
        cm = PEEKBITS(8);
        DROPBITS(8);
        if (id1 != 0x1Fu || id2 != 0x8Bu) {
            panic(Z_STREAM_ERROR, "invalid gzip header bytes: 0x%02x 0x%02x", id1, id2);
        }
        if (cm != 8) {
            panic(Z_STREAM_ERROR, "invalid compression method: %u", cm);
        }
        state->flags = PEEKBITS(8);
        DROPBITS(8);
        DEBUG0("GZIP HEADER");
        DEBUG("\tID1   = %3u (0x%02x)", id1, id1);
        DEBUG("\tID2   = %3u (0x%02x)", id2, id2);
        DEBUG("\tCM    = %3u", cm);
        DEBUG("\tFLG   = %3u", state->flags);
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
        DROPBITS(32);
        DEBUG("\tMTIME = %u", mtime);
        if (state->head) {
            state->head->time = mtime;
        }
        mode = XFL;
        goto xfl;
        break;
    xfl:
    case XFL:
        NEEDBITS(8 + 8);
        DEBUG("\tXFL   = %3lu", PEEKBITS(8));
        if (state->head) {
            state->head->xflags = static_cast<int>(PEEKBITS(8));
        }
        DROPBITS(8);
        DEBUG("\tOS    = %3lu", PEEKBITS(8));
        if (state->head) {
            state->head->os = static_cast<int>(PEEKBITS(8));
        }
        DROPBITS(8);
        mode = FEXTRA;
        goto fextra;
        break;
    fextra:
    case FEXTRA:
        state->index = 0;
        if ((state->flags & (1u << 2)) != 0) {
            NEEDBITS(16);
            state->index |= ((buff >> 0) & 0xFFu) << 8;
            state->index |= ((buff >> 8) & 0xFFu) << 0;
            DROPBITS(16);
        }
        mode = FEXTRA_DATA;
        goto fextra_data;
        break;
    fextra_data:
    case FEXTRA_DATA:
        while (state->index > 0) {
            NEEDBITS(8);
            DROPBITS(8);
            state->index--;
        }
        state->len = 0;
        mode = FNAME;
        goto fname;
        break;
    fname:
    case FNAME:
        if ((state->flags & (1u << 3)) != 0) {
            for (;;) {
                NEEDBITS(8);
                auto c = static_cast<Bytef>(PEEKBITS(8));
                if (state->head && state->len < state->head->name_max) {
                    state->head->name[state->len++] = c;
                }
                DROPBITS(8);
                if (c == '\0') {
                    break;
                }
            }
            if (state->head) DEBUG("Original Filename: '%s'", state->head->name);
        }
        mode = FCOMMENT;
        goto fcomment;
        break;
    fcomment:
    case FCOMMENT:
        if ((state->flags & (1u << 4)) != 0) {
            Bytef c;
            do {
                NEEDBITS(8);
                c = static_cast<Bytef>(PEEKBITS(8));
                DROPBITS(8);
            } while (c != '\0');
        }
        mode = FHCRC;
        goto fhcrc;
        break;
    fhcrc:
    case FHCRC:
        if ((state->flags & (1u << 1)) != 0) {
            NEEDBITS(16);
            DEBUG("\tCRC = %lu", PEEKBITS(16));
            DROPBITS(16);
        }
        DEBUG0("Finished parsing GZIP header");
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
            mode = DYNAMIC_HUFFMAN;
            goto dynamic_huffman_block;
        } else {
            panic(Z_STREAM_ERROR, "invalid block type: %u", state->blktype);
        }
        UNREACHABLE();
        break;
    no_compression_block:
    case NO_COMPRESSION:
        DROPREMBYTE();
        NEEDBITS(32);
        if ((buff & 0xFFFFu) != ((buff >> 16) ^ 0xFFFFu)) {
            panic0(Z_STREAM_ERROR, "invalid stored block lengths");
        }
        state->len = buff & 0xFFFFu;
        DROPBITS(32);
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

            // Step 1. Flush remaining bytes in bit buffer to output
            assert(bits % 8 == 0);
            uInt bytes = bits / 8;
            uInt amount = min_u32(state->len, min_u32(bytes, avail_out));
            state->len -= amount;
            avail_out -= amount;
            read -= amount;
            wrote += amount;
            while (amount-- > 0) {
                *out++ = static_cast<Bytef>(PEEKBITS(8));
                DROPBITS(8);
            }
            assert(bits == 0);
            assert(buff == 0);

            // Step 2. Stream directly from input to output
            amount = min_u32(state->len, min_u32(avail_in, avail_out));
            state->len -= amount;
            avail_in -= amount;
            avail_out -= amount;
            read += amount;
            wrote += amount;
            while (amount-- > 0) {
                windowAdd(state, in, sizeof(*in));
                *out++ = *in++;
            }
            CHECK_IO();
            assert(state->len == 0 || (avail_in == 0 || avail_out == 0));
            if (state->len != 0) {
                goto exit;
            }
        }
        assert(state->len == 0);
        mode = END_BLOCK;
        goto end_block;
        break;
    fixed_huffman_block:
    case FIXED_HUFFMAN:
        state->lits = fixed_huffman_literals_codes;
        state->litlens = fixed_huffman_literals_codelens;
        state->litmaxlen = fixed_huffman_literals_maxlen;
        state->dsts = fixed_huffman_distance_codes;
        state->dstlens = fixed_huffman_distance_codelens;
        state->dstmaxlen = fixed_huffman_distance_maxlen;
        mode = HUFFMAN_READ;
        goto huffman_read;
        break;
    dynamic_huffman_block:
    case DYNAMIC_HUFFMAN:
        NEEDBITS(5 + 5 + 4);
        state->hlit = PEEKBITS(5) + 257;
        DROPBITS(5);
        state->hdist = PEEKBITS(5) + 1;
        DROPBITS(5);
        state->hclen = PEEKBITS(4) + 4;
        DROPBITS(4);
        state->index = 0;
        assert(state->hlit <= 286);
        assert(state->hdist <= 30);
        assert(state->hclen <= NUM_CODE_LENGTHS);
        memset(&state->hlengths, 0, sizeof(state->hlengths));
        mode = HEADER_TREE;
        goto header_tree;
        break;
    header_tree:
    case HEADER_TREE:
        while (state->index < state->hclen) {
            NEEDBITS(3);
            state->hlengths[order[state->index++]] = PEEKBITS(3);
            DROPBITS(3);
        }
        init_huffman_tree(state->htree, 7, state->hlengths, NUM_CODE_LENGTHS);
        memset(state->dynlens, 0, sizeof(state->dynlens));
        state->index = 0;
        mode = DYNAMIC_CODE_LENGTHS;
        goto dynamic_code_lengths;
    dynamic_code_lengths:
    case DYNAMIC_CODE_LENGTHS:
        while (state->index < state->hlit + state->hdist) {
            NEEDBITS(7);  // TODO: track actual max(hlengths)?
            value = state->htree[PEEKBITS(7)];
            if (value == 0xffffu) {
                panic(Z_STREAM_ERROR, "invalid bit sequence: 0x%x len=7", static_cast<Bytef>(PEEKBITS(7)));
            }
            DROPBITS(state->hlengths[value]);
            uInt nbits, offset;
            uint16_t rvalue;
            if (value <= 15) {
                state->dynlens[state->index++] = value;
            } else if (value <= 18) {
                switch (value) {
                case 16:
                    nbits = 2;
                    offset = 3;
                    if (state->index == 0) {
                        panic(Z_STREAM_ERROR, "invalid repeat code %u with no previous code lengths", value);
                    }
                    rvalue = state->dynlens[state->index - 1];
                    break;
                case 17:
                    nbits = 3;
                    offset = 3;
                    rvalue = 0;
                    break;
                case 18:
                    nbits = 7;
                    offset = 11;
                    rvalue = 0;
                    break;
                default:
                    UNREACHABLE();
                }
                NEEDBITS(nbits);
                uLong repeat = PEEKBITS(nbits);
                DROPBITS(nbits);
                repeat += offset;
                while (repeat-- > 0) {
                    state->dynlens[state->index++] = rvalue;
                }
            } else {
                panic(Z_STREAM_ERROR, "invalid dynamic code length: %u", value);
            }
        }
        if (state->index != state->hlit + state->hdist) {
            panic(Z_STREAM_ERROR, "too many code lengths: hlit=%zu hdist=%zu read=%zu", state->hlit, state->hdist,
                  state->index);
        }

        {
            // TODO(peter): do this during reading?
            state->litlens = &state->dynlens[0];
            state->dstlens = &state->dynlens[state->hlit];
            state->litmaxlen = max_length(&state->litlens[0], &state->litlens[state->hlit]);
            state->dstmaxlen = max_length(&state->dstlens[0], &state->dstlens[state->hdist]);
            if (state->dynlits) {
                assert(state->dynlits && state->dyndsts);
                strm->zfree(strm->opaque, state->dynlits);
                strm->zfree(strm->opaque, state->dyndsts);
            }
            uInt nlits = 1u << state->litmaxlen;
            uInt ndsts = 1u << state->dstmaxlen;
            state->dynlits = reinterpret_cast<uint16_t *>(strm->zalloc(strm->opaque, sizeof(uint16_t), nlits));
            state->dyndsts = reinterpret_cast<uint16_t *>(strm->zalloc(strm->opaque, sizeof(uint16_t), ndsts));
            if (!state->dynlits || !state->dyndsts) {
                panic0(Z_MEM_ERROR, "unable to allocate space for huffman tables");
            }
            init_huffman_tree(state->dynlits, state->litmaxlen, state->litlens, state->hlit);
            init_huffman_tree(state->dyndsts, state->dstmaxlen, state->dstlens, state->hdist);
            // TODO(peter): can save 2 pointers (at the cost of const safety) by just using `lits` and `dsts` directly
            state->lits = state->dynlits;
            state->dsts = state->dyndsts;
            mode = HUFFMAN_READ;
            goto huffman_read;
            break;
        }
    huffman_read:
    case HUFFMAN_READ:
        NEEDBITS(state->litmaxlen);
        value = state->lits[PEEKBITS(state->litmaxlen)];
        if (value == 0xffffu) {
            panic(Z_STREAM_ERROR, "invalid bit sequence: 0x%04lx length=%zu", PEEKBITS(state->litmaxlen),
                  state->litmaxlen);
        }
        if (value < 256) {
            if (avail_out == 0) {
                goto exit;
            }
            DROPBITS(state->litlens[value]);
            assert(avail_out > 0);
            Bytef c = static_cast<Bytef>(value);
            windowAdd(state, &c, sizeof(c));
            *out++ = c;
            avail_out--;
            wrote++;
            CHECK_IO();
            mode = HUFFMAN_READ;  // TEMP TEMP: unneeded
            goto huffman_read;
        } else if (value == 256) {
            DROPBITS(state->litlens[value]);
            DEBUG0("inflate: end of fixed huffman block found");
            if (state->lits != &fixed_huffman_literals_codes[0]) {
                assert(state->lits == state->dynlits);
                assert(state->dsts == state->dyndsts);
                strm->zfree(strm->opaque, state->dynlits);
                strm->zfree(strm->opaque, state->dyndsts);
                state->dynlits = nullptr;
                state->dyndsts = nullptr;
            }
            mode = END_BLOCK;
            goto end_block;
        } else if (value <= 285) {
            DROPBITS(state->litlens[value]);
            assert(257 <= value && value <= 285);
            state->len = static_cast<uInt>(value) - 257;
            assert(state->len < ARRSIZE(LENGTH_BASES));
            assert(state->len < ARRSIZE(LENGTH_EXTRA_BITS));
            mode = HUFFMAN_LENGTH_CODE;
            goto huffman_length_code;
        } else {
            panic(Z_STREAM_ERROR, "invalid huffman value: %u", value);
        }
        UNREACHABLE();
        break;
    huffman_length_code:
    case HUFFMAN_LENGTH_CODE:
        extra = LENGTH_EXTRA_BITS[state->len];
        NEEDBITS(extra);
        state->len = static_cast<uInt>(LENGTH_BASES[state->len] + PEEKBITS(extra));
        DROPBITS(extra);
        mode = READ_HUFFMAN_DISTANCE_CODE;
        goto read_huffman_distance_code;
        break;
    read_huffman_distance_code:
    case READ_HUFFMAN_DISTANCE_CODE:
        NEEDBITS(state->dstmaxlen);
        value = state->dsts[PEEKBITS(state->dstmaxlen)];
        if (value == 0xffffu) {
            panic(Z_STREAM_ERROR, "invalid bit sequence: 0x%04lx length=%zu", PEEKBITS(state->dstmaxlen),
                  state->dstmaxlen);
        }
        DROPBITS(state->dstlens[value]);
        if (value >= 32) {
            panic(Z_STREAM_ERROR, "invalid distance code: %u", value);
        }
        state->index = value;
        mode = HUFFMAN_DISTANCE_CODE;
        goto huffman_distance_code;
        break;
    huffman_distance_code:
    case HUFFMAN_DISTANCE_CODE: {
        extra = DISTANCE_EXTRA_BITS[state->index];
        NEEDBITS(extra);
        size_t distance = DISTANCE_BASES[state->index] + PEEKBITS(extra);
        DROPBITS(extra);
        if (!checkDistance(state, distance)) {
            panic(Z_STREAM_ERROR, "invalid distance %zu", distance);
        }
        state->index = (state->wnd_head + ((state->wnd_mask + 1) - distance));
        mode = WRITE_HUFFMAN_LEN_DIST;
        goto write_huffman_len_dist;
        break;
    }
    write_huffman_len_dist:
    case WRITE_HUFFMAN_LEN_DIST:
        while (state->len > 0) {
            if (avail_out == 0) {
                goto exit;
            }
            Bytef c = state->wnd[state->index & state->wnd_mask];
            *out++ = c;
            avail_out--;
            wrote++;
            windowAdd(state, &c, sizeof(c));
            CHECK_IO();
            state->index++;
            state->len--;
        }
        mode = HUFFMAN_READ;
        goto huffman_read;
        break;
    end_block:
    case END_BLOCK:
        if (state->blkfinal) {
            mode = CHECK_CRC32;
            goto check_crc32;
        } else {
            mode = BEGIN_BLOCK;
            goto begin_block;
        }
    check_crc32:
    case CHECK_CRC32: {
        DROPREMBYTE();
        NEEDBITS(32);
        strm->adler = calc_crc32(static_cast<uint32_t>(strm->adler), strm->next_out, wrote);
        strm->total_out += wrote;
        wrote = 0;
        strm->avail_out = avail_out;
        uint32_t crc = AS_U32(buff);
        DROPBITS(32);
        if (crc != AS_U32(strm->adler)) {
            panic(Z_STREAM_ERROR, "invalid crc: found=0x%04x expected=0x%04x", crc, AS_U32(strm->adler));
        }
        DEBUG("CRC32: 0x%08x MINE: 0x%08x", crc, AS_U32(strm->adler));
        mode = CHECK_ISIZE;
        goto check_isize;
        break;
    }
    check_isize:
    case CHECK_ISIZE: {
        NEEDBITS(32);
        uint32_t isize = AS_U32(buff);
        DROPBITS(32);
        DEBUG("Original input size: %u found=%u", isize, AS_U32(strm->total_out));
        assert(avail_in == 0);
        if (isize != AS_U32(strm->total_out)) {
            panic(Z_STREAM_ERROR, "original size does not match inflated size: orig=%u new=%u", isize,
                  AS_U32(strm->total_out));
        }
        ret = Z_STREAM_END;
        goto exit;
        break;
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
    strm->adler = calc_crc32(static_cast<uint32_t>(strm->adler), strm->next_out, wrote);
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
