#include "pzlib.h"

// TODO: faster huffman code reading using linked-listed LUT implementation
// TODO: can I remove the allocation for the dynamic trees?

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
#define DEBUG0(msg)
#define DEBUG(fmt, ...)
#else
#define DEBUG0(msg) fprintf(stderr, "DEBUG: " msg "\n");
#define DEBUG(fmt, ...) fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__);
#endif

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

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
static uint16_t hlengths[NUM_CODE_LENGTHS];
static constexpr size_t MAX_HTREE_BIT_LENGTH = 8;
static constexpr size_t HTREE_TABLE_SIZE = 1u << MAX_HTREE_BIT_LENGTH;
static constexpr size_t MAX_CODE_LENGTHS = 322;

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
    DYNAMIC_HUFFMAN,
    NO_COMPRESSION_READ,
    HUFFMAN_READ,
    WRITE_HUFFMAN_VALUE,
    HUFFMAN_DISTANCE_CODE,
    WRITE_HUFFMAN_LEN_DIST,
    HEADER_TREE,
    DYNAMIC_CODE_LENGTHS,
    END_BLOCK,
};
typedef enum inflate_mode inflate_mode;

struct vec {
    size_t len;
    const uint16_t *d;
};

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
    size_t code;
    size_t hlit;
    size_t hdist;
    size_t hclen;
    size_t ncodes;
    vec dynlits;
    vec dyndists;

    uint16_t htree[HTREE_TABLE_SIZE];
    uint16_t dyncodelens[MAX_CODE_LENGTHS];

    /* circular buffer window */
    uint32_t mask;
    uint32_t head;
    uint32_t size;
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

    size_t window_size = (1u << windowBits);
    size_t window_bytes = sizeof(Bytef) * window_size;
    size_t alloc_size = sizeof(internal_state) + window_bytes;
    void *mem = strm->zalloc(strm->opaque, 1, static_cast<uInt>(alloc_size));
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
    strm->state->code = 1;
    strm->state->hlit = 0;
    strm->state->hdist = 0;
    strm->state->hclen = 0;
    strm->state->ncodes = 0;
    strm->state->dynlits.d = NULL;
    strm->state->dynlits.len = 0;
    strm->state->dyndists.d = NULL;
    strm->state->dyndists.len = 0;
    strm->state->mask = static_cast<uint32_t>(window_size - 1);
    strm->state->head = 0;
    strm->state->size = 0;
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
        if (strm->state->dynlits.len != 0) {
            strm->zfree(strm->opaque, const_cast<uint16_t *>(strm->state->dynlits.d));
            strm->state->dynlits.d = Z_NULL;
            strm->state->dynlits.len = 0;
        }
        if (strm->state->dyndists.len != 0) {
            strm->zfree(strm->opaque, const_cast<uint16_t *>(strm->state->dyndists.d));
            strm->state->dyndists.d = Z_NULL;
            strm->state->dyndists.len = 0;
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


template <bool DynAllocate>
static bool init_huffman_tree(z_streamp s, vec *tree, const uint16_t *code_lengths, size_t n) {
    constexpr size_t MAX_HCODE_BIT_LENGTH = 16;
    constexpr size_t MAX_HUFFMAN_CODES = 512;
    static size_t bl_count[MAX_HCODE_BIT_LENGTH];
    static uint16_t next_code[MAX_HCODE_BIT_LENGTH];
    static uint16_t codes[MAX_HUFFMAN_CODES];

    if (!(n < MAX_HUFFMAN_CODES)) {
        assert(n < MAX_HUFFMAN_CODES);
        return false;
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the
    // number of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < n; ++i) {
        assert(code_lengths[i] <= MAX_HCODE_BIT_LENGTH);
        ++bl_count[code_lengths[i]];
        max_bit_length = code_lengths[i] > max_bit_length ? code_lengths[i] : max_bit_length;
    }
    bl_count[0] = 0;

    // 2) Find the numerical value of the smallest code for each code length:
    memset(&next_code[0], 0, sizeof(next_code));
    size_t code = 0;
    for (size_t bits = 1; bits <= max_bit_length; ++bits) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = static_cast<uint16_t>(code);
    }

    // 3) Assign numerical values to all codes, using consecutive values for all
    // codes of the same length with the base values determined at step 2. Codes
    // that are never used (which have a bit length of zero) must not be
    // assigned a value.
    memset(&codes[0], 0, sizeof(codes));
    for (size_t i = 0; i < n; ++i) {
        if (code_lengths[i] != 0) {
            codes[i] = next_code[code_lengths[i]]++;
            assert((16 - __builtin_clz(codes[i])) <= code_lengths[i]);
        }
    }

    // Table size is 2**(max_bit_length + 1)
    size_t table_size = 1u << (max_bit_length + 1);
    uint16_t *t;
    if (DynAllocate && table_size > tree->len) {
        if (tree->d) {
            s->zfree(s->opaque, const_cast<uint16_t *>(tree->d));
        }
        t = reinterpret_cast<uint16_t *>(s->zalloc(s->opaque, static_cast<uInt>(table_size), sizeof(*t)));
    } else {
        assert(table_size <= tree->len);
        t = const_cast<uint16_t *>(tree->d);
    }
    for (size_t j = 0; j < table_size; ++j) {
        t[j] = EMPTY_SENTINEL;
    }
    for (size_t value = 0; value < n; ++value) {
        int len = code_lengths[value];
        if (len == 0) {
            continue;
        }
        code = codes[value];
        size_t index = 1;
        for (int i = len - 1; i >= 0; --i) {
            index = (index << 1) | ((code >> i) & 0x1u);
        }
        assert(t[index] == EMPTY_SENTINEL);
        t[index] = static_cast<uint16_t>(value);
    }
    tree->d = t;
    tree->len = table_size;
    return true;
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
    uInt crc16;
    uint16_t value;
    vec htreevec;

    if (in == Z_NULL || out == Z_NULL) {
        return Z_STREAM_ERROR;
    }

    switch (mode) {
    case HEADER:
        NEEDBITS(8 + 8 + 8);
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
        DEBUG0("GZIP HEADER");
        DEBUG("\tID1   = %3u (0x%02x)", id1, id1);
        DEBUG("\tID2   = %3u (0x%02x)", id2, id2);
        DEBUG("\tCM    = %3u", cm);
        mode = FLAGS;
        goto flags;
        break;
    flags:
    case FLAGS:
        NEEDBITS(8);
        state->flag = PEEKBITS(8);
        DROPBITS(8);
        DEBUG("\tFLG   = %3u", state->flag);
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
        state->index = 0;
        if ((state->flag & (1u << 2)) != 0) {
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

        // TEMP TEMP TEMP
        state->temp = 0;
        state->code = state->len;

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
            uInt amount = std::min<uInt>(state->len, std::min<uInt>(bytes, avail_out));
            state->len -= amount;
            avail_out -= amount;
            read -= amount;
            wrote += amount;
            while (amount-- > 0) {
                *out++ = static_cast<Bytef>(PEEKBITS(8));
                DROPBITS(8);
                state->temp++;
            }
            assert(bits == 0);
            assert(buff == 0);

            // Step 2. Stream directly from input to output
            amount = std::min<uInt>(state->len, std::min<uInt>(avail_in, avail_out));
            state->len -= amount;
            avail_in -= amount;
            avail_out -= amount;
            read += amount;
            wrote += amount;
            while (amount-- > 0) {
                windowAdd(state, in, sizeof(*in));
                *out++ = *in++;
                state->temp++;
            }
            CHECK_IO();
            assert(state->len == 0 || (avail_in == 0 || avail_out == 0));
            if (state->len != 0) {
                goto exit;
            }
        }
        assert(state->temp == state->code);
        assert(state->len == 0);
        mode = END_BLOCK;
        goto end_block;
        break;
    fixed_huffman_block:
    case FIXED_HUFFMAN:
        state->lits = fixed_huffman_literals_tree;
        state->litlen = sizeof(fixed_huffman_literals_tree);
        state->dists = fixed_huffman_distance_tree;
        state->distlen = sizeof(fixed_huffman_distance_tree);
        mode = HUFFMAN_READ;
        state->code = 1;
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
        state->ncodes = state->hlit + state->hdist;
        state->index = 0;
        assert(state->hlit <= 286);
        assert(state->hdist <= 30);
        assert(state->hclen <= NUM_CODE_LENGTHS);
        memset(&hlengths, 0, sizeof(hlengths));
        mode = HEADER_TREE;
        goto header_tree;
        break;
    header_tree:
    case HEADER_TREE:
        while (state->index < state->hclen) {
            NEEDBITS(3);
            hlengths[order[state->index++]] = PEEKBITS(3);
            DROPBITS(3);
        }
        htreevec.len = HTREE_TABLE_SIZE;
        htreevec.d = state->htree;
        if (!init_huffman_tree<false>(strm, &htreevec, hlengths, NUM_CODE_LENGTHS))
            panic(Z_MEM_ERROR, "%s", "unable to initialize huffman header tree");
        memset(state->dyncodelens, 0, sizeof(state->dyncodelens));
        state->index = 0;
        state->code = 1;
        mode = DYNAMIC_CODE_LENGTHS;
        goto dynamic_code_lengths;
    dynamic_code_lengths:
    case DYNAMIC_CODE_LENGTHS:
        while (state->index < state->ncodes) {
            // REVISIT(peter): this is the very slow path
            while (state->htree[state->code] == EMPTY_SENTINEL) {
                NEEDBITS(1);
                state->code = (state->code << 1) | PEEKBITS(1);
                DROPBITS(1);
                assert(state->code < HTREE_TABLE_SIZE);
            }
            value = state->htree[state->code];
            uInt nbits, offset;
            uint16_t rvalue;
            if (value <= 15) {
                state->dyncodelens[state->index++] = value;
            } else if (value <= 18) {
                switch (value) {
                case 16:
                    nbits = 2;
                    offset = 3;
                    if (state->index == 0)
                        panic(Z_STREAM_ERROR, "invalid repeat code %u with no previous code lengths", value);
                    rvalue = state->dyncodelens[state->index - 1];
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
                    state->dyncodelens[state->index++] = rvalue;
                }
            } else {
                panic(Z_STREAM_ERROR, "invalid dynamic code length: %u", value);
            }
            state->code = 1;
        }
        if (!init_huffman_tree<true>(strm, &state->dynlits, &state->dyncodelens[0], state->hlit))
            panic0(Z_MEM_ERROR, "failed to initialize dynamic huffman literals tree");
        if (!init_huffman_tree<true>(strm, &state->dyndists, &state->dyncodelens[state->hlit], state->hdist))
            panic0(Z_MEM_ERROR, "failed to initialize dynamic huffman distances tree");

        state->lits = state->dynlits.d;
        state->litlen = state->dynlits.len;
        state->dists = state->dyndists.d;
        state->distlen = state->dyndists.len;
        state->code = 1;
        mode = HUFFMAN_READ;
        goto huffman_read;
        break;
    huffman_read:
    case HUFFMAN_READ:
        // REVISIT(peter): this is the very slow path
        while (state->lits[state->code] == EMPTY_SENTINEL) {
            NEEDBITS(1);
            state->code = (state->code << 1) | PEEKBITS(1);
            DROPBITS(1);
            assert(state->code < state->litlen);
        }
        value = state->lits[state->code];
        if (value < 256) {
            if (avail_out == 0) goto exit;
            Bytef c = static_cast<Bytef>(value);
            windowAdd(state, &c, sizeof(c));
            *out++ = c;
            avail_out--;
            wrote++;
            CHECK_IO();
            state->code = 1;
            mode = HUFFMAN_READ;  // TEMP TEMP: unneeded
            goto huffman_read;
        } else if (value == 256) {
            DEBUG0("inflate: end of fixed huffman block found");
            if (state->lits != &fixed_huffman_literals_tree[0]) {
                assert(state->lits == state->dynlits.d);
                assert(state->dists == state->dyndists.d);
                strm->zfree(strm->opaque, const_cast<uint16_t *>(state->dynlits.d));
                strm->zfree(strm->opaque, const_cast<uint16_t *>(state->dyndists.d));
                state->dynlits.d = NULL;
                state->dynlits.len = 0;
                state->dyndists.d = NULL;
                state->dyndists.len = 0;
            }
            state->code = 1;
            mode = END_BLOCK;
            goto end_block;
        } else if (value <= 285) {
            assert(257 <= value && value <= 285);
            value = static_cast<uint16_t>(value - 257);
            assert(value < ARRSIZE(LENGTH_BASES));
            uInt extra = LENGTH_EXTRA_BITS[value];
            NEEDBITS(extra);
            size_t length = LENGTH_BASES[value] + PEEKBITS(extra);
            DROPBITS(extra);
            state->length = length;
            state->code = 1;
            mode = HUFFMAN_DISTANCE_CODE;
            goto huffman_distance_code;
        } else {
            panic(Z_STREAM_ERROR, "invalid huffman value: %u", value);
        }
        UNREACHABLE();
        break;
    huffman_distance_code:
    case HUFFMAN_DISTANCE_CODE: {
        while (state->dists[state->code] == EMPTY_SENTINEL) {
            NEEDBITS(1);
            state->code = (state->code << 1) | PEEKBITS(1);
            DROPBITS(1);
            assert(state->code < state->distlen);
        }
        value = state->dists[state->code];
        assert(value < 32);  // invalid distance code
        uInt extra = DISTANCE_EXTRA_BITS[value];
        NEEDBITS(extra);
        size_t distance = DISTANCE_BASES[value] + PEEKBITS(extra);
        DROPBITS(extra);
        if (!checkDistance(state, distance)) {
            panic(Z_STREAM_ERROR, "invalid distance %zu", distance);
        }
        state->index = (state->head + ((state->mask + 1) - distance));
        mode = WRITE_HUFFMAN_LEN_DIST;
        goto write_huffman_len_dist;
        break;
    }
    write_huffman_len_dist:
    case WRITE_HUFFMAN_LEN_DIST:
        while (state->length > 0) {
            if (avail_out == 0) goto exit;
            Bytef c = state->wnd[state->index & state->mask];
            *out++ = c;
            avail_out--;
            wrote++;
            windowAdd(state, &c, sizeof(c));
            CHECK_IO();
            state->index++;
            state->length--;
        }
        state->code = 1;
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
