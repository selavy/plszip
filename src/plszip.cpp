#include "plszip.h"

// TODO: faster huffman code reading using linked-listed LUT implementation
// TODO: can I remove the allocation for the dynamic trees?

#ifndef USE_ZLIB

// #define CALC_AND_CHECK_CRC

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

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

#ifdef NDEBUG
#define panic(rc, msg1, fmt, ...) \
    do {                          \
        strm->msg = msg1;         \
        ret = rc;                 \
        goto exit;                \
    } while (0)
#else
#define panic(rc, msg1, fmt, ...)                                                 \
    do {                                                                          \
        snprintf(msgbuf_, sizeof(msgbuf_), "[%d] " fmt, __LINE__, ##__VA_ARGS__); \
        strm->msg = msgbuf_;                                                      \
        ret = rc;                                                                 \
        goto exit;                                                                \
    } while (0)
#endif

#define panic0(rc, msg_)  \
    do {                  \
        strm->msg = msg_; \
        ret = rc;         \
        goto exit;        \
    } while (0)

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))
#define AS_U32(x) static_cast<uint32_t>((x)&0xFFFFFFFFu)
#define AS_U16(x) static_cast<uint16_t>((x)&0xFFFFu)
#define AS_U8(x) static_cast<uint16_t>((x)&0xFFu)

/* Global Static Data */
static constexpr uInt LengthExtraBits[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};

static constexpr size_t LengthBases[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static_assert(ARRSIZE(LengthExtraBits) == ARRSIZE(LengthBases));

static constexpr uInt DistanceExtraBits[32] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 0, 0,
};

static constexpr size_t DISTANCE_BASES[32] = {
    1,   2,   3,   4,   5,    7,    9,    13,   17,   25,   33,   49,    65,    97,    129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0,   0,
};

static constexpr size_t NumHeaderCodeLengths = 19;
static constexpr size_t order[NumHeaderCodeLengths] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                       11, 4,  12, 3, 13, 2, 14, 1, 15};
static constexpr size_t HeaderTreeMaxSize = 1u << 7;
static constexpr size_t MaxDynamicCodeLengths = 322;
static constexpr size_t MaxCodeBits = 16;

/* Internal Types */
enum inflate_mode {
    HEADER, /* ID1 | ID2 | CM */
    MTIME,  /* MTIME */
    XFL,    /* XFL */
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

static_assert(sizeof(uLong) >= 4, "Only support architectures with unsigned long >= 4 bytes");

struct internal_state {
    inflate_mode mode;
    uInt bits;   // # of bits in bit accumulator
    uLong buff;  // bit accumator
    gz_header *head;
    const uint8_t *litlens;
    const uint16_t *litcodes;
    const uint8_t *dstlens;
    const uint16_t *dstcodes;
    union {
        uint16_t length;
        uint16_t lencode;
    };
    union {
        uint16_t index;
        uint16_t dstcode;
        uint16_t n_codes;
    };
    uint16_t hlit;
    uint16_t hdist;
    uint16_t hclen;
    uint16_t *dynlits;
    uint16_t *dyndsts;
    uint16_t wnd_mask;
    uint16_t wnd_head;
    uint16_t wnd_size;
    uint8_t litmaxbits;
    uint8_t dstmaxbits;
    Byte flags;
    Byte blkfinal;

#ifndef NDEBUG
    int block_number;
#endif

    uint16_t htree[HeaderTreeMaxSize];
    uint8_t dynlens[MaxDynamicCodeLengths];
    uint8_t hlengths[NumHeaderCodeLengths];

    /* circular buffer window */
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
uint8_t max_u8(uint8_t x, uint8_t y) noexcept { return x > y ? x : y; }
uint32_t min_u32(uint32_t x, uint32_t y) noexcept { return x < y ? x : y; }
int32_t min_s32(int32_t x, int32_t y) noexcept { return x < y ? x : y; }

uint8_t max_length(const uint8_t *first, const uint8_t *last) noexcept {
    uint8_t result = 0;
    while (first != last) {
        result = max_u8(*first++, result);
    }
    return result;
}

int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size) {
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

    strm->total_in = 0;
    strm->total_out = 0;

    if (windowBits != 15 + 16) {
        strm->msg = "invalid windowBits parameter -- 31 only supported value";
        return Z_STREAM_ERROR;
    }
    windowBits -= 16;
    assert(8 <= windowBits && windowBits <= 15);

    strm->adler = 0;

    size_t window_size = (1u << windowBits);
    size_t window_bytes = sizeof(Bytef) * window_size;
    size_t alloc_size = sizeof(internal_state) + window_bytes;
    void *mem = strm->zalloc(strm->opaque, 1, static_cast<uInt>(alloc_size));
    if (!mem) {
        strm->msg = "failed to allocate memory for internal state";
        return Z_MEM_ERROR;
    }
    strm->state = new (mem) internal_state{};
    strm->state->mode = HEADER;
    strm->state->buff = 0UL;
    strm->state->bits = 0;
    strm->state->flags = 0;
    strm->state->blkfinal = 0;
#ifndef NDEBUG
    strm->state->block_number = 0;
#endif
    strm->state->litlens = nullptr;
    strm->state->litcodes = nullptr;
    strm->state->litmaxbits = 0;
    strm->state->dstlens = nullptr;
    strm->state->dstcodes = nullptr;
    strm->state->dstmaxbits = 0;
    strm->state->index = 0;
    strm->state->hlit = 0;
    strm->state->hdist = 0;
    strm->state->hclen = 0;
    strm->state->dynlits = nullptr;
    strm->state->dyndsts = nullptr;
    assert(window_size <= 0xFFFFu);
    strm->state->wnd_mask = static_cast<uint16_t>(window_size - 1);
    strm->state->wnd_head = 0;
    strm->state->wnd_size = 0;
    return Z_OK;
}

static void windowAddByte(internal_state *s, Bytef x) {
    if (s->wnd_size <= s->wnd_mask) {
        s->wnd_size++;
    }
    s->wnd[s->wnd_head] = x;
    s->wnd_head = static_cast<uint16_t>((s->wnd_head + 1) & s->wnd_mask);
}

static void windowAdd(internal_state *s, const Bytef *buf, int n) {
    assert(n < UINT16_MAX);
    int wnd_mask = s->wnd_mask;
    int wnd_capacity = wnd_mask + 1;
    int wnd_head = s->wnd_head;
    int wnd_size = s->wnd_size;
    int n1 = min_s32(wnd_capacity - wnd_head, n);
    int n2 = n - n1;
    Bytef *p1 = &s->wnd[wnd_head];
    Bytef *p2 = &s->wnd[0];
    while (n1-- > 0) {
        *p1++ = *buf++;
    }
    while (n2-- > 0) {
        *p2++ = *buf++;
    }
    wnd_head = (wnd_head + n) & wnd_mask;
    wnd_size = min_s32(wnd_size + n, wnd_capacity);
    assert(wnd_head < UINT16_MAX);
    assert(wnd_size < UINT16_MAX);
    s->wnd_head = static_cast<uint16_t>(wnd_head);
    s->wnd_size = static_cast<uint16_t>(wnd_size);
}

static bool checkDistance(const internal_state *s, size_t distance) {
    return distance <= s->wnd_size && distance <= s->wnd_mask;
}

int inflateEnd(z_streamp strm) {
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

#ifndef NDEBUG
#define CHECK_IO()                                    \
    do {                                              \
        assert(avail_in <= strm->avail_in);           \
        assert(avail_out <= strm->avail_out);         \
        assert(avail_in + read == strm->avail_in);    \
        assert(avail_out + wrote == strm->avail_out); \
    } while (0)
#else
#define CHECK_IO()
#endif

#ifndef NDEBUG
#define NEXTBYTE()                                 \
    do {                                           \
        if (avail_in == 0) goto exit;              \
        avail_in--;                                \
        buff += static_cast<uLong>(*in++) << bits; \
        read++;                                    \
        bits += 8;                                 \
    } while (0)
#else
#define NEXTBYTE()                                 \
    do {                                           \
        if (avail_in == 0) goto exit;              \
        avail_in--;                                \
        buff += static_cast<uLong>(*in++) << bits; \
        bits += 8;                                 \
    } while (0)
#endif

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

#ifndef NDEBUG
constexpr int calc_min_code_len(uint16_t code) noexcept {
    return code != 0 ? 16 - __builtin_clz(code) : 0;
}
#endif

static void init_huffman_tree(uint16_t *tree, const size_t maxlen, const uint8_t *codelens, size_t ncodes) {
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
            if (i == 256) {
                DEBUG("here");
            }
            if (codelens[i] != 0) {
                codes[i] = next_code[codelens[i]]++;
                assert(calc_min_code_len(codes[i]) <= codelens[i]);
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

int inflate(z_streamp strm, int flush) { return PLS_inflate(strm, flush); }

int PLS_inflate(z_streamp strm, int flush) {
    (void)flush;

#ifndef NDEBUG
    static char msgbuf_[1024];
#endif

    int ret = Z_OK;
    struct internal_state *state = strm->state;
    inflate_mode mode = state->mode;
    z_const Bytef *in = strm->next_in;
    uInt avail_in = strm->avail_in;
    Bytef *out = strm->next_out;
    uInt avail_out = strm->avail_out;
    uInt bits = state->bits;
    uLong buff = state->buff;

#ifndef NDEBUG
    uLong read = 0;
    uLong wrote = 0;
#endif

    uint8_t id1, id2, cm, blktype;
    uint32_t mtime;
    uint16_t value;
    uInt extra;

    if (in == Z_NULL || out == Z_NULL) {
        return Z_STREAM_ERROR;
    }

    switch (mode) {
    case HEADER:
        NEEDBITS(8 + 8 + 8 + 8);
        id1 = static_cast<uint8_t>((buff >> 0) & 0xFFu);
        id2 = static_cast<uint8_t>((buff >> 8) & 0xFFu);
        cm = static_cast<uint8_t>((buff >> 16) & 0xFFu);
        state->flags = static_cast<uint8_t>((buff >> 24) & 0xFFu);
        DROPBITS(32);
        if (id1 != 0x1Fu || id2 != 0x8Bu) {
            panic(Z_STREAM_ERROR, "invalid gzip header", "invalid gzip header bytes: 0x%02x 0x%02x", id1, id2);
        }
        if (cm != 8) {
            panic(Z_STREAM_ERROR, "invalid compression method", "invalid compression method: %u", cm);
        }
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
            state->index = static_cast<uint16_t>((((buff >> 0) & 0xFFu) << 8) | (((buff >> 8) & 0xFFu) << 0));
            DROPBITS(16);
            mode = FEXTRA_DATA;
            goto fextra_data;
        } else {
            state->index = 0;
            mode = FNAME;
            goto fname;
        }
    fextra_data:
    case FEXTRA_DATA:
        while (state->index > 0) {
            NEEDBITS(8);
            DROPBITS(8);
            state->index--;
        }
        mode = FNAME;
        goto fname;
        break;
    fname:
    case FNAME:
        if ((state->flags & (1u << 3)) != 0) {
            for (;;) {
                NEEDBITS(8);
                auto c = static_cast<Bytef>(PEEKBITS(8));
                if (state->head && state->index < state->head->name_max) {
                    state->head->name[state->index++] = c;
                }
                DROPBITS(8);
                if (c == '\0') {
                    break;
                }
            }
            if (state->head) {
                DEBUG("Original Filename: '%s'", state->head->name);
            }
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
        // state->blkfinal = buff & 0x1;
        // blktype = (buff >> 1) & 0x3;
        state->blkfinal = PEEKBITS(1);
        DROPBITS(1);
        blktype = PEEKBITS(2);
        DROPBITS(2);
        // DROPBITS(3);
        if (blktype == 0x0u) {
#ifndef NDEBUG
            DEBUG("Block #%d Encoding: No Compression%s", state->block_number, state->blkfinal ? " -- final" : "");
            state->block_number++;
#endif
            mode = NO_COMPRESSION;
            goto no_compression_block;
        } else if (blktype == 0x1u) {
#ifndef NDEBUG
            DEBUG("Block #%d Encoding: Fixed Huffman%s", state->block_number, state->blkfinal ? " -- final" : "");
            state->block_number++;
#endif
            mode = FIXED_HUFFMAN;
            goto fixed_huffman_block;
        } else if (blktype == 0x2u) {
#ifndef NDEBUG
            DEBUG("Block #%d Encoding: Dynamic Huffman%s", state->block_number, state->blkfinal ? " -- final" : "");
            state->block_number++;
#endif
            mode = DYNAMIC_HUFFMAN;
            goto dynamic_huffman_block;
        } else {
            panic(Z_STREAM_ERROR, "invalid block type", "invalid block type: %u", blktype);
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
        state->length = AS_U16(buff);
        DROPBITS(32);
        mode = NO_COMPRESSION_READ;
        goto no_compression_read;
        break;
    no_compression_read:
    case NO_COMPRESSION_READ: {
        // NOTE: switching to byte-oriented reading/writing
        // should be on a byte boundary at this point after flush to
        // byte boundary followed by 2 x 2B reads

        // REVISIT(peter): are there ever bytes
        // remaining in the buffer?

        // Step 1. Flush remaining bytes in bit buffer to output
        assert(bits % 8 == 0);
        uInt length = state->length;
        uInt bytes = bits / 8;
        uInt amount = min_u32(length, min_u32(bytes, avail_out));
        length -= amount;
        avail_out -= amount;
#ifndef NDEBUG
        read -= amount;
        wrote += amount;
#endif
        while (amount-- > 0) {
            assert(bits >= 8);
            Bytef c = static_cast<Bytef>(PEEKBITS(8));
            *out++ = c;
            windowAddByte(state, c);
            DROPBITS(8);
        }
        assert(bits == 0);
        assert(buff == 0);

        // Step 2. Stream directly from input to output
        amount = min_u32(length, min_u32(avail_in, avail_out));
        memcpy(out, in, amount);
        windowAdd(state, in, static_cast<int>(amount));
        in += amount;
        out += amount;
        length -= amount;
        avail_in -= amount;
        avail_out -= amount;
#ifndef NDEBUG
        read += amount;
        wrote += amount;
#endif
        CHECK_IO();
        assert(length == 0 || (avail_in == 0 || avail_out == 0));
        if (length != 0) {
            state->length = static_cast<uint16_t>(length);
            goto exit;
        }
        mode = END_BLOCK;
        goto end_block;
        break;
    }
    fixed_huffman_block:
    case FIXED_HUFFMAN:
        state->litlens = fixed_huffman_literals_lens;
        state->litcodes = fixed_huffman_literals_codes;
        state->litmaxbits = fixed_huffman_literals_maxbits;
        state->dstlens = fixed_huffman_distance_lens;
        state->dstcodes = fixed_huffman_distance_codes;
        state->dstmaxbits = fixed_huffman_distance_maxbits;
        assert(state->litmaxbits <= MaxCodeBits);
        assert(state->dstmaxbits <= MaxCodeBits);
        mode = HUFFMAN_READ;
        goto huffman_read;
        break;
    dynamic_huffman_block:
    case DYNAMIC_HUFFMAN:
        NEEDBITS(5 + 5 + 4);
        state->hlit = static_cast<uint16_t>(PEEKBITS(5) + 257);
        DROPBITS(5);
        state->hdist = static_cast<uint16_t>(PEEKBITS(5) + 1);
        DROPBITS(5);
        state->hclen = static_cast<uint16_t>(PEEKBITS(4) + 4);
        DROPBITS(4);
        state->n_codes = 0;
        if (!(state->hlit <= 286)) {
            panic0(Z_STREAM_ERROR, "invalid HLIT");
        }
        if (!(state->hdist <= 30)) {
            panic0(Z_STREAM_ERROR, "invalid HDIST");
        }
        if (!(state->hclen <= NumHeaderCodeLengths)) {
            panic0(Z_STREAM_ERROR, "invalid HCLEN");
        }
        memset(&state->hlengths, 0, sizeof(state->hlengths));
        mode = HEADER_TREE;
        goto header_tree;
        break;
    header_tree:
    case HEADER_TREE:
        while (state->n_codes < state->hclen) {
            NEEDBITS(3);
            state->hlengths[order[state->n_codes++]] = static_cast<uint8_t>(PEEKBITS(3));
            DROPBITS(3);
        }
        init_huffman_tree(state->htree, 7, state->hlengths, NumHeaderCodeLengths);
        memset(state->dynlens, 0, sizeof(state->dynlens));
        state->n_codes = 0;
        mode = DYNAMIC_CODE_LENGTHS;
        goto dynamic_code_lengths;
    dynamic_code_lengths:
    case DYNAMIC_CODE_LENGTHS:
        while (static_cast<int>(state->n_codes) < state->hlit + state->hdist) {
            NEEDBITS(7 + MaxCodeBits);
            value = state->htree[PEEKBITS(7)];
            if (value == 0xffffu) {
                panic(Z_STREAM_ERROR, "invalid bit sequence in header tree", "invalid bit sequence: 0x%x len=7",
                      static_cast<Bytef>(PEEKBITS(7)));
            }
            DROPBITS(state->hlengths[value]);
            uInt nbits, offset;
            uint8_t rvalue;
            if (value <= 15) {
                state->dynlens[state->n_codes++] = static_cast<uint8_t>(value);
            } else if (value <= 18) {
                switch (value) {
                case 16:
                    nbits = 2;
                    offset = 3;
                    if (state->n_codes == 0) {
                        panic(Z_STREAM_ERROR, "invalid repeat code",
                              "invalid repeat code %u with no previous code lengths", value);
                    }
                    rvalue = state->dynlens[state->n_codes - 1];
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
                assert(bits >= nbits);
                // NEEDBITS(nbits);
                uLong repeat = PEEKBITS(nbits);
                DROPBITS(nbits);
                repeat += offset;
                while (repeat-- > 0) {
                    state->dynlens[state->n_codes++] = rvalue;
                }
            } else {
                panic(Z_STREAM_ERROR, "invalid dynamic code length", "invalid dynamic code length: %u", value);
            }
        }
        if (static_cast<int>(state->n_codes) != state->hlit + state->hdist) {
            panic(Z_STREAM_ERROR, "too many code lengths", "too many code lengths: hlit=%u hdist=%u read=%u",
                  state->hlit, state->hdist, state->n_codes);
        }

        {
            // TODO(peter): do this during reading?
            state->litlens = &state->dynlens[0];
            state->dstlens = &state->dynlens[state->hlit];
            state->litmaxbits = max_length(&state->litlens[0], &state->litlens[state->hlit]);
            state->dstmaxbits = max_length(&state->dstlens[0], &state->dstlens[state->hdist]);
            assert(state->litmaxbits <= MaxCodeBits);
            assert(state->dstmaxbits <= MaxCodeBits);
            if (state->dynlits) {
                assert(state->dynlits && state->dyndsts);
                strm->zfree(strm->opaque, state->dynlits);
                strm->zfree(strm->opaque, state->dyndsts);
            }
            uInt nlits = 1u << state->litmaxbits;
            uInt ndsts = 1u << state->dstmaxbits;
            state->dynlits = reinterpret_cast<uint16_t *>(strm->zalloc(strm->opaque, sizeof(uint16_t), nlits));
            state->dyndsts = reinterpret_cast<uint16_t *>(strm->zalloc(strm->opaque, sizeof(uint16_t), ndsts));
            if (!state->dynlits || !state->dyndsts) {
                panic0(Z_MEM_ERROR, "unable to allocate space for huffman tables");
            }
            init_huffman_tree(state->dynlits, state->litmaxbits, state->litlens, state->hlit);
            init_huffman_tree(state->dyndsts, state->dstmaxbits, state->dstlens, state->hdist);
            // TODO(peter): can save 2 pointers (at the cost of const safety) by just using `lits` and `dsts` directly
            state->litcodes = state->dynlits;
            state->dstcodes = state->dyndsts;
            mode = HUFFMAN_READ;
            goto huffman_read;
            break;
        }
    huffman_read:
    case HUFFMAN_READ:
        NEEDBITS(state->litmaxbits);
        value = state->litcodes[PEEKBITS(state->litmaxbits)];
        if (value == 0xffffu) {
            panic(Z_STREAM_ERROR, "invalid huffman code", "invalid bit sequence: 0x%04lx length=%u",
                  PEEKBITS(state->litmaxbits), state->litmaxbits);
        }
        if (value < 256) {
            if (avail_out == 0) {
                goto exit;
            }
            DROPBITS(state->litlens[value]);
            assert(avail_out > 0);
            Bytef c = static_cast<Bytef>(value);
            windowAddByte(state, c);
            *out++ = c;
            avail_out--;
#ifndef NDEBUG
            wrote++;
#endif
            CHECK_IO();
            assert(mode == HUFFMAN_READ);
            goto huffman_read;
        } else if (value == 256) {
            DROPBITS(state->litlens[value]);
            DEBUG0("inflate: end of fixed huffman block found");
            if (state->litcodes != &fixed_huffman_literals_codes[0]) {
                assert(state->litcodes == state->dynlits);
                assert(state->dstcodes == state->dyndsts);
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
            state->lencode = static_cast<uint16_t>(value - 257);
            assert(state->lencode < ARRSIZE(LengthBases));
            assert(state->lencode < ARRSIZE(LengthExtraBits));
            mode = HUFFMAN_LENGTH_CODE;
            goto huffman_length_code;
        } else {
            panic(Z_STREAM_ERROR, "invalid huffman code", "invalid huffman value: %u", value);
        }
        UNREACHABLE();
        break;
    huffman_length_code:
    case HUFFMAN_LENGTH_CODE:
        extra = LengthExtraBits[state->lencode];
        NEEDBITS(extra);
        state->length = static_cast<uint16_t>(LengthBases[state->lencode] + PEEKBITS(extra));
        DROPBITS(extra);
        mode = READ_HUFFMAN_DISTANCE_CODE;
        goto read_huffman_distance_code;
        break;
    read_huffman_distance_code:
    case READ_HUFFMAN_DISTANCE_CODE:
        NEEDBITS(state->dstmaxbits);
        value = state->dstcodes[PEEKBITS(state->dstmaxbits)];
        if (value == 0xffffu) {
            panic(Z_STREAM_ERROR, "invalid huffman code", "invalid bit sequence: 0x%04lx length=%u",
                  PEEKBITS(state->dstmaxbits), state->dstmaxbits);
        }
        DROPBITS(state->dstlens[value]);
        if (value >= 32) {
            panic(Z_STREAM_ERROR, "invalid distance code", "invalid distance code: %u", value);
        }
        state->dstcode = value;
        mode = HUFFMAN_DISTANCE_CODE;
        goto huffman_distance_code;
        break;
    huffman_distance_code:
    case HUFFMAN_DISTANCE_CODE: {
        extra = DistanceExtraBits[state->dstcode];
        NEEDBITS(extra);
        size_t distance = DISTANCE_BASES[state->dstcode] + PEEKBITS(extra);
        DROPBITS(extra);
        if (!checkDistance(state, distance)) {
            panic(Z_STREAM_ERROR, "invalid distance", "invalid distance %zu", distance);
        }
        size_t wnd_buff_size = static_cast<size_t>(state->wnd_mask + 1);
        assert(distance < UINT16_MAX);
        assert(distance < wnd_buff_size);
        state->index = static_cast<uint16_t>(state->wnd_head + (wnd_buff_size - distance));
        mode = WRITE_HUFFMAN_LEN_DIST;
        goto write_huffman_len_dist;
        break;
    }
    write_huffman_len_dist:
    case WRITE_HUFFMAN_LEN_DIST:
        while (state->length > 0) {
            if (avail_out == 0) {
                goto exit;
            }
            Bytef c = state->wnd[state->index & state->wnd_mask];
            *out++ = c;
            avail_out--;
#ifndef NDEBUG
            wrote++;
#endif
            windowAddByte(state, c);
            CHECK_IO();
            state->index++;
            state->length--;
        }
        mode = HUFFMAN_READ;
        goto huffman_read;
        break;
    end_block:
    case END_BLOCK:
        CHECK_IO();
        if (state->blkfinal) {
            mode = CHECK_CRC32;
            goto check_crc32;
        } else {
            mode = BEGIN_BLOCK;
            goto begin_block;
        }
    check_crc32:
    case CHECK_CRC32: {
        CHECK_IO();
        DROPREMBYTE();
        NEEDBITS(32);
#ifndef NDEBUG
        assert(wrote == strm->avail_out - avail_out);
#endif

#ifdef CALC_AND_CHECK_CRC
        strm->adler = calc_crc32(static_cast<uint32_t>(strm->adler), strm->next_out, strm->avail_out - avail_out);
#endif
        strm->total_out += strm->avail_out - avail_out;
        strm->avail_out = avail_out;
#ifndef NDEBUG
        wrote = 0;
#endif

#ifdef CALC_AND_CHECK_CRC
        uint32_t crc = AS_U32(buff);
#endif
        DROPBITS(32);
#ifdef CALC_AND_CHECK_CRC
        if (crc != AS_U32(strm->adler)) {
            panic(Z_STREAM_ERROR, "crc check failed", "invalid crc: found=0x%04x expected=0x%04x", crc,
                  AS_U32(strm->adler));
        }
        DEBUG("CRC32: 0x%08x MINE: 0x%08x", crc, AS_U32(strm->adler));
#endif
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
            fprintf(stderr, "%u != %u\n", isize, AS_U32(strm->total_out));
            panic(Z_STREAM_ERROR, "original size does not match inflated size",
                  "original size does not match inflated size: orig=%u new=%u", isize, AS_U32(strm->total_out));
        }
        ret = Z_STREAM_END;
        goto exit;
        break;
    }
    }

exit:
    assert(avail_in <= strm->avail_in);
    assert(avail_out <= strm->avail_out);
#ifndef NDEBUG
    assert(read == strm->avail_in - avail_in);
    assert(wrote == strm->avail_out - avail_out);
#endif
#ifdef CALC_AND_CHECK_CRC
    strm->adler = calc_crc32(static_cast<uint32_t>(strm->adler), strm->next_out, strm->avail_out - avail_out);
#endif
    strm->total_in += strm->avail_in - avail_in;
    strm->total_out += strm->avail_out - avail_out;
    strm->next_in = in;
    strm->next_out = out;
    strm->avail_in = avail_in;
    strm->avail_out = avail_out;
    state->bits = bits;
    state->buff = buff;
    state->mode = mode;
    return ret;
}

#endif
