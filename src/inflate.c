#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_huffman_trees.h"

#define panic(fmt, ...)                                   \
    do {                                                  \
        fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); \
        exit(1);                                          \
    } while (0)

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

#ifdef NDEBUG
#define xassert(c, fmt, ...)
#define DEBUG(fmt, ...)
#else
#define xassert(c, fmt, ...)                                     \
    do {                                                         \
        if (!(c)) {                                              \
            fprintf(stderr, "ASSERT: " fmt "\n", ##__VA_ARGS__); \
            assert(0);                                           \
        }                                                        \
    } while (0)
#define DEBUG(fmt, ...) fprintf(stderr, "DBG: " fmt "\n", ##__VA_ARGS__)
#endif

#define INFO(fmt, ...) fprintf(stdout, "INFO: " fmt "\n", ##__VA_ARGS__)

#ifndef static_assert
#define static_assert(x) _Static_assert(x, "")
#endif

#define FORCE_INLINE __attribute__((always_inline)) inline

#define MIN(x, y) (x) < (y) ? (x) : (y)
#define MAX(x, y) (x) > (y) ? (x) : (y)

/* Header Constants */
static const uint8_t ID1_GZIP = 31;
static const uint8_t ID2_GZIP = 139;
#define STRICT_WINDOW_SIZE_CHECK
#define MAX_HUFFMAN_CODES 512
#define MAX_HCODE_BIT_LENGTH 16
#define EMPTY_SENTINEL UINT16_MAX
#define READ_BUFFER_SIZE (1u << 16)
#define WRIT_BUFFER_SIZE (1u << 16)
#define WINDOW_SIZE      (1u << 16)

/* Header Flags */
static const uint8_t FTEXT = 1u << 0;
static const uint8_t FHCRC = 1u << 1;
static const uint8_t FEXTRA = 1u << 2;
static const uint8_t FNAME = 1u << 3;
static const uint8_t FCOMMENT = 1u << 4;
static const uint8_t RESERV1 = 1u << 5;
static const uint8_t RESERV2 = 1u << 6;
static const uint8_t RESERV3 = 1u << 7;

/* Block Types */
static const uint8_t NO_COMPRESSION = 0x0u;
static const uint8_t FIXED_HUFFMAN = 0x1u;
static const uint8_t DYNAMIC_HUFFMAN = 0x2u;
static const uint8_t RESERVED = 0x3u;

/* Global Static Data */
static const size_t LENGTH_BASE_CODE = 257;
static const size_t LENGTH_EXTRA_BITS[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};

static const size_t LENGTH_BASES[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23,  27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static_assert(ARRSIZE(LENGTH_EXTRA_BITS) == ARRSIZE(LENGTH_BASES));

static const size_t DISTANCE_EXTRA_BITS[32] = {
    0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 0, 0,
};

static const size_t DISTANCE_BASES[32] = {
    1,    2,    3,    4,    5,    7,     9,     13,    17,  25,   33,
    49,   65,   97,   129,  193,  257,   385,   513,   769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0,   0,
};

struct gzip_header {
    uint8_t id1;
    uint8_t id2;
    uint8_t cm;
    uint8_t flg;
    uint32_t mtime;
    uint8_t xfl;
    uint8_t os;
} __attribute__((packed));
typedef struct gzip_header gzip_header;

struct vec {
    size_t len;
    const uint16_t *d;
};
typedef struct vec vec;

struct stream {
    const uint8_t *next_in; /* next input byte */
    size_t avail_in;        /* number of bytes available at next_in */
    size_t total_in;        /* total number of bytes read so far */
    int (*refill)(struct stream *s);
    void *read_data;

    uint8_t *next_out; /* next output byte will go here */
    size_t avail_out;  /* remaining free space at next_out */
    size_t total_out;  /* total number of bytes output so far */
    int (*flush)(struct stream *s);
    void *write_data;

    void *(*zalloc)(void *data, size_t nmemb, size_t size);
    void (*zfree)(void *data, void *address);
    void *alloc_data;

    void *stream_data;

    int error;
};
typedef struct stream stream;

struct priv_stream_data {
    uint8_t buf;
    size_t  bit;

    /* circular buffer window */
    uint32_t mask;
    uint32_t head;
#ifdef STRICT_WINDOW_SIZE_CHECK
    uint32_t size;
#endif
    uint8_t wnd[1];
};

void *default_zalloc(void *data, size_t nmemb, size_t size) {
    return malloc(nmemb * size);
}

void default_zfree(void *data, void *address) { free(address); }

struct file_read_data {
    uint8_t buf[READ_BUFFER_SIZE];
    FILE *fp;
};
typedef struct file_read_data file_read_data;
struct file_write_data {
    uint8_t buf[WRIT_BUFFER_SIZE];
    FILE *fp;
};
typedef struct file_write_data file_write_data;

static void refill_or_panic(stream *s) {
    if (s->refill(s) != 0)
        panic("read error: %d", s->error);
}

int refill_file(stream *s) {
    file_read_data *d = s->read_data;
    size_t rem = s->avail_in;
    size_t read;
    memmove(&d->buf[0], s->next_in, rem);
    read = fread(&d->buf[rem], 1, sizeof(d->buf) - rem, d->fp);
    if (read > 0) {
        s->next_in = &d->buf[0];
        s->avail_in += read;
        s->error = read == sizeof(d->buf) - rem ? 0 : ferror(d->fp);
        return s->error;
    } else {
        s->error = ferror(d->fp);
        return s->error;
    }
}

int flush_file(stream *s) {
    file_write_data *d = s->write_data;
    uint32_t avail = s->next_out - &d->buf[0];
    size_t out = fwrite(&d->buf[0], 1, avail, d->fp);
    s->next_out = &d->buf[0];
    s->avail_out = sizeof(d->buf);
    s->total_out += out;
    s->error = out != avail ? ferror(d->fp) : 0;
    return s->error;
}

static int init_priv_stream_data(stream *s, size_t size) {
    struct priv_stream_data *data;
    xassert((size & (size - 1)) == 0, "window size must be a power of 2");
    data = s->zalloc(s->alloc_data, 1,
                     sizeof(*data) + sizeof(data->wnd[0]) * (size - 1));
    if (!data) return -1;  // TODO(peter): improve error codes
    data->mask = size - 1;
    data->head = 0;
#ifdef STRICT_WINDOW_SIZE_CHECK
    data->size = 0;
#endif
    // TEMP TEMP
    memset(&data->wnd[0], 0, sizeof(data->wnd[0]) * size);
    data->buf = 0u;
    data->bit = 0;
    s->stream_data = data;
    return 0;
}

static void init_file_stream(stream *s, file_read_data *read_data,
                      file_write_data *write_data) {
    s->next_in = &read_data->buf[0];
    s->avail_in = 0;
    s->total_in = 0;
    s->refill = &refill_file;
    s->read_data = read_data;

    s->next_out = &write_data->buf[0];
    s->avail_out = sizeof(write_data->buf);
    s->total_out = 0;
    s->flush = &flush_file;
    s->write_data = write_data;

    s->error = 0;

    s->zalloc = &default_zalloc;
    s->zfree = &default_zfree;

    if (init_priv_stream_data(s, WINDOW_SIZE) != 0)
        panic("failed to initialize private stream data");
}

FORCE_INLINE static void close_file_stream(stream *s) {
    file_read_data *read_data = s->read_data;
    file_write_data *write_data = s->write_data;
    fclose(read_data->fp);
    fclose(write_data->fp);
}

// TODO(peter): force inline?
FORCE_INLINE static void stream_read_consume(stream *s, size_t n) {
    assert(s->avail_in >= n);
    s->next_in += n;
    s->avail_in -= n;
    s->total_in += n;
    // TODO(peter): add CRC32 calculatiion
}

// TODO(peter): force inline?
FORCE_INLINE static void stream_write_consume(stream *s, size_t n) {
    assert(s->avail_out >= n);
    s->next_out += n;
    s->avail_out -= n;
    s->total_out += n;
}

static int stream_read(stream *s, void *buf, size_t n) {
    /* fast path: plenty of data available */
    if (s->avail_in >= n) {
        memcpy(buf, s->next_in, n);
        stream_read_consume(s, n);
        return 0;
    }

    uint8_t *p = buf;
    do {
        if (s->avail_in < n)
            refill_or_panic(s);
        xassert(s->avail_in > 0 || s->avail_in == n,
                "refill did not add any bytes!");
        size_t avail = MIN(n, s->avail_in);
        memcpy(p, s->next_in, avail);
        stream_read_consume(s, avail);
        p += avail;
        n -= avail;
    } while (n > 0);
    return 0;
}

FORCE_INLINE static size_t wndsize(const stream *s) {
    struct priv_stream_data *data = s->stream_data;
#ifdef STRICT_WINDOW_SIZE_CHECK
    return data->size;
#else
    return data->mask + 1;
#endif
}

FORCE_INLINE static size_t wndcap(const stream *s) {
    struct priv_stream_data *data = s->stream_data;
    return data->mask + 1;
}

static void window_add(stream *s, const uint8_t *buf, size_t n) {
    struct priv_stream_data *data = s->stream_data;
#ifdef STRICT_WINDOW_SIZE_CHECK
    data->size = MIN(data->size + n, data->mask + 1);
#endif
    // while (n-- > 0) {
    //     data->wnd[data->head] = *buf++;
    //     data->head = (data->head + 1) & data->mask;
    // }
    size_t n1 = MIN(data->mask + 1 - data->head, n);
    size_t n2 = n - n1;
    uint8_t *p = &data->wnd[data->head];
    while (n1-- > 0) {
        *p++ = *buf++;
    }
    p = &data->wnd[0];
    while (n2-- > 0) {
        *p++ = *buf++;
    }
    data->head = (data->head + n) & data->mask;
}

static int check_distance(stream *s, size_t distance) {
    struct priv_stream_data *wnd = s->stream_data;
#ifdef STRICT_WINDOW_SIZE_CHECK
    if (distance > wnd->size) return 1;
#endif
    if (distance > wnd->mask) return 1;
    return 0;
}

static int stream_write(stream *s, const void *buf, size_t n) {
    if (s->avail_out >= n) {
        memcpy(s->next_out, buf, n);
        stream_write_consume(s, n);
        return 0;
    }

    const uint8_t *p = buf;
    do {
        if (s->avail_out < n) {
            if (s->flush(s) != 0) return s->error;
        }
        assert(s->avail_out > 0);
        size_t avail = MIN(n, s->avail_out);
        memcpy(s->next_out, p, avail);
        stream_write_consume(s, avail);
        p += avail;
        n -= avail;
    } while (n > 0);

    return 0;
}

static int stream_window(stream *s, size_t dist, size_t len) {
    struct priv_stream_data *w = s->stream_data;
    size_t mask = w->mask;
    size_t bsize = w->mask + 1;
    size_t head = w->head;
    size_t start = (head + (bsize - dist)) & mask;
    xassert(dist < bsize, "distance >= bsize: %zu >= %zu", dist, bsize);

    // size_t hh = head, ss = start;
    // for (size_t i = 0; i < len; ++i) {
    //     w->wnd[hh] = w->wnd[ss];
    //     hh = (hh + 1) & mask;
    //     ss = (ss + 1) & mask;
    // }

    size_t ll1 = MIN(bsize - head, len);
    size_t ll2 = len - ll1;
    for (size_t i = 0; i < ll1; ++i) {
        w->wnd[head+i] = w->wnd[(start+i) & mask];
    }
    for (size_t i = 0; i < ll2; ++i) {
        w->wnd[   0+i] = w->wnd[(start+ll1+i) & mask];
    }

    // flush window -- may require 2 writes in case wrapped
    // ---------------------------------
    // | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | X |
    // ---------------------------------
    //           ^           ^
    //           head        start
    //           len = 5
    // [      ]            [           ]
    // [WRITE2]            [WRITE1     ]
    //
    // len1 = [head, X) ==> buffer_size - head
    // len2 = [0   , end)
    size_t len1 = MIN(bsize - start, len);
    size_t len2 = len - len1;
    if (s->avail_out < len)
        if (s->flush(s) != 0)
            return s->error;
    memcpy(s->next_out +    0, &w->wnd[start], len1);
    memcpy(s->next_out + len1, &w->wnd[    0], len2);
    stream_write_consume(s, len);
    // w->head = hh;
    w->head = (head + len) & mask;
#ifdef STRICT_WINDOW_SIZE_CHECK
    w->size = MIN(w->size + len, bsize);
#endif
    return 0;
}

static char *read_null_terminated_string(stream *s) {
    // TODO(peter): remove usage of realloc, and use s->z_alloc instead
    size_t pos;
    size_t len = 0;
    char *str = NULL;
    for (;;) {
        const size_t n = s->avail_in;
        const uint8_t *p = memchr(s->next_in, '\0', n);
        if (p) {
            pos = p - s->next_in + 1;
            str = realloc(str, len + pos);
            if (!str) return NULL;
            memcpy(&str[len], s->next_in, pos);
            assert(str[len + pos - 1] == '\0');
            stream_read_consume(s, pos);
            return str;
        } else {
            str = realloc(str, len + n);
            if (!str) return NULL;
            memcpy(&str[len], s->next_in, n);
            assert(n == s->avail_in);
            s->next_in += n;
            s->avail_in = 0;
            s->total_in += n;
            if (s->refill(s) != 0) {
                free(str);
                return NULL;
            }
            len += n;
        }
    }
}

static uint16_t readbits(stream *s, size_t *bit, size_t nbits) {
    xassert(0 <= nbits && nbits <= 16, "invalid nbits: %zu", nbits);
    uint16_t result = 0;
    for (size_t i = 0; i < nbits; ++i) {
        if (*bit == 8) {
            ++s->next_in;
            if (--s->avail_in == 0)
                refill_or_panic(s);
            *bit = 0;
        }
        result |= ((s->next_in[0] >> *bit) & 0x1u) << i;
        ++*bit;
    }
    return result;
}

static int init_huffman_tree(stream *s, vec *tree, const uint16_t *code_lengths,
                      size_t n) {
    static size_t bl_count[MAX_HCODE_BIT_LENGTH];
    static uint16_t next_code[MAX_HCODE_BIT_LENGTH];
    static uint16_t codes[MAX_HUFFMAN_CODES];

    if (!(n < MAX_HUFFMAN_CODES)) {
        xassert(n < MAX_HUFFMAN_CODES, "code lengths too long");
        return 1;  // TODO: improve error codes
    }

    // 1) Count the number of codes for each code length. Let bl_count[N] be the
    // number of codes of length N, N >= 1.
    memset(&bl_count[0], 0, sizeof(bl_count));
    size_t max_bit_length = 0;
    for (size_t i = 0; i < n; ++i) {
        xassert(code_lengths[i] <= MAX_HCODE_BIT_LENGTH,
                "Unsupported bit length");
        ++bl_count[code_lengths[i]];
        max_bit_length =
            code_lengths[i] > max_bit_length ? code_lengths[i] : max_bit_length;
    }
    bl_count[0] = 0;

    // 2) Find the numerical value of the smallest code for each code length:
    memset(&next_code[0], 0, sizeof(next_code));
    uint32_t code = 0;
    for (size_t bits = 1; bits <= max_bit_length; ++bits) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    // 3) Assign numerical values to all codes, using consecutive values for all
    // codes of the same length with the base values determined at step 2. Codes
    // that are never used (which have a bit length of zero) must not be
    // assigned a value.
    memset(&codes[0], 0, sizeof(codes));
    for (size_t i = 0; i < n; ++i) {
        if (code_lengths[i] != 0) {
            codes[i] = next_code[code_lengths[i]]++;
            xassert((16 - __builtin_clz(codes[i])) <= code_lengths[i],
                    "overflowed code length");
        }
    }

    // Table size is 2**(max_bit_length + 1)
    size_t table_size = 1u << (max_bit_length + 1);
    uint16_t *t = s->zalloc(s->alloc_data, table_size, sizeof(*t));
    for (int j = 0; j < table_size; ++j) {
        t[j] = EMPTY_SENTINEL;
    }
    for (size_t value = 0; value < n; ++value) {
        size_t len = code_lengths[value];
        if (len == 0) {
            continue;
        }
        uint16_t code = codes[value];
        size_t index = 1;
        for (int i = len - 1; i >= 0; --i) {
            // size_t isset = ((code & (1u << i)) != 0) ? 1 : 0;
            // index = 2 * index + isset;
            index = (index << 1) | ((code >> i) & 0x1u);
        }
        xassert(t[index] == EMPTY_SENTINEL,
                "Assigned multiple values to same index");
        t[index] = value;
    }
    tree->d = t;
    tree->len = table_size;

    return 0;
}

static uint16_t read_huffman_value(stream *s, size_t *bitp, vec tree) {
    // TODO(peter): what is the maximum number of bits in a huffman code?
    //    fixed  : 9
    //    dymamic: ?
    size_t index = 1;
    size_t bit = *bitp;
    do {
        if (bit == 8) {
            ++s->next_in;
            if (--s->avail_in == 0)
                refill_or_panic(s);
            bit = 0;
        }
        index <<= 1;
        index |= (s->next_in[0] >> bit++) & 0x1u;
        xassert(index < tree.len, "invalid index");
    } while (tree.d[index] == EMPTY_SENTINEL);
    *bitp = bit;
    return tree.d[index];
}

static int read_dynamic_trees(stream *s, size_t *bit, vec *lits, vec *dists) {
    size_t hlit = readbits(s, bit, 5) + 257;
    size_t hdist = readbits(s, bit, 5) + 1;
    size_t hclen = readbits(s, bit, 4) + 4;
    size_t ncodes = hlit + hdist;
#define NUM_CODE_LENGTHS 19
    const static uint16_t order[NUM_CODE_LENGTHS] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    static uint16_t lengths[NUM_CODE_LENGTHS];
    memset(&lengths, 0, sizeof(lengths));
    for (size_t i = 0; i < hclen; ++i) {
        lengths[order[i]] = readbits(s, bit, 3);
    }
    vec htree;

    // TODO(peter): statically allocate `htree`? each length is max 3 bits => 8
    if (init_huffman_tree(s, &htree, &lengths[0], NUM_CODE_LENGTHS) != 0)
        panic("failed to initialized dynamic huffman tree header");

// max(HLIT)  => max(5 bits + 257) => max(32 + 257) => max 289
// max(HDIST) => max(5 bits +   1) => max(32 +   1) => max 33
// max(NCODES) => max(HLIT + HDIST) => max(289 + 33) => max(322)
#define MAX_CODE_LENGTHS 322
    static uint16_t dynamic_code_lengths[MAX_CODE_LENGTHS];
    memset(&dynamic_code_lengths, 0, sizeof(dynamic_code_lengths));
    xassert(ncodes <= MAX_CODE_LENGTHS, "too many dynamic code lengths");
    size_t idx = 0;
    while (idx < ncodes) {
        uint16_t value = read_huffman_value(s, bit, htree);
        if (value <= 15) {
            dynamic_code_lengths[idx++] = value;
        } else if (value <= 18) {
            size_t nbits, offset;
            uint16_t rvalue;
            if (value == 16) {
                nbits = 2;
                offset = 3;
                if (idx == 0)
                    panic(
                        "received repeat code 16 with no code lengths to "
                        "repeat");
                rvalue = dynamic_code_lengths[idx - 1];
            } else if (value == 17) {
                nbits = offset = 3;
                rvalue = 0;
            } else if (value == 18) {
                nbits = 7;
                offset = 11;
                rvalue = 0;
            }
            xassert(16 <= value && value <= 18,
                    "didn't cover all cases for value");
            int rtimes = readbits(s, bit, nbits) + offset;
            xassert(rtimes > 0, "invalid repeat value");
            while (rtimes-- > 0) {
                dynamic_code_lengths[idx++] = rvalue;
            }
        } else {
            panic("invalid dynamic code length: %u", value);
        }
    }
    xassert(idx == ncodes, "invalid number of dynamic length codes: %zu", idx);
    xassert(idx > 256 && dynamic_code_lengths[256] != 0,
            "invalid dynamic length codes");

    if (init_huffman_tree(s, lits, &dynamic_code_lengths[0], hlit) != 0)
        panic("failed to initialize dynamic huffman literals tree");
    if (init_huffman_tree(s, dists, &dynamic_code_lengths[hlit], hdist) != 0)
        panic("failed to initialize dynamic huffman distances tree");

    s->zfree(s->alloc_data, (void *)htree.d);
#ifndef NDEBUG
    htree.d = NULL;
    htree.len = 0;
#endif
    return 0;
}

int main(int argc, char **argv) {
    char *input_filename = argv[1];
    char *output_filename;
    gzip_header hdr;
    file_read_data file_read_data;
    file_write_data file_write_data;
    stream strm;

    if (argc == 2) {
        output_filename = NULL;
    } else if (argc == 3) {
        output_filename = argv[2];
    } else {
        fprintf(stderr, "Usage: %s [FILE] [OUT]\n", argv[0]);
        exit(0);
    }

    if (!(file_read_data.fp = fopen(input_filename, "rb")))
        panic("failed to open input file: %s", input_filename);
    if (!(file_write_data.fp =
              output_filename ? fopen(output_filename, "wb") : stdout))
        panic("failed to open output file: %s", output_filename ?: "<stdout>");
    init_file_stream(&strm, &file_read_data, &file_write_data);

    /**************************************************************************
     * Read header and metadata
     *************************************************************************/

    if (stream_read(&strm, &hdr, sizeof(hdr)) != 0)
        panic("unable to read gzip header: %d", strm.error);

    INFO("GzipHeader:");
    INFO("\tid1   = %u (0x%02x)", hdr.id1, hdr.id1);
    INFO("\tid2   = %u (0x%02x)", hdr.id2, hdr.id2);
    INFO("\tcm    = %u", hdr.cm);
    INFO("\tflg   = %u", hdr.flg);
    INFO("\tmtime = %u", hdr.mtime);
    INFO("\txfl   = %u", hdr.xfl);
    INFO("\tos    = %u", hdr.os);

    if (hdr.id1 != ID1_GZIP) panic("Unsupported identifier #1: %u.", hdr.id1);
    if (hdr.id2 != ID2_GZIP) panic("Unsupported identifier #2: %u.", hdr.id2);
    if ((hdr.flg & FEXTRA) != 0) {
        // +---+---+=================================+
        // | XLEN  |...XLEN bytes of "extra field"...| (more-->)
        // +---+---+=================================+
        panic("FEXTRA flag not supported.");
    }
    if ((hdr.flg & FNAME) != 0) {
        // +=========================================+
        // |...original file name, zero-terminated...| (more-->)
        // +=========================================+
        char *fname = read_null_terminated_string(&strm);
        if (!fname) panic("unable to read original filename: %d", strm.error);
        INFO("Original Filename: '%s'", fname);
        free(fname);
    }
    if ((hdr.flg & FCOMMENT) != 0) {
        // +===================================+
        // |...file comment, zero-terminated...| (more-->)
        // +===================================+
        DEBUG("File contains comment");
        char *comment = read_null_terminated_string(&strm);
        if (!comment) panic("unable to read file comment: %d", strm.error);
        INFO("File comment: '%s'", comment);
        free(comment);
    }
    if ((hdr.flg & FHCRC) != 0) {
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
        if (stream_read(&strm, &crc16, sizeof(crc16)) != 0)
            panic("unable to read crc16: %d", strm.error);
        INFO("CRC16: %u (0x%04X)", crc16, crc16);
    }
    if ((hdr.flg & (RESERV1 | RESERV2 | RESERV3)) != 0)
        panic("reserve bits are not 0");

    /**************************************************************************
     * Read compressed data
     *************************************************************************/

    /* know at this point that are on a byte boundary as all previous fields
     * have been byte sized */
    size_t bit = 0;
    size_t nbits;
    uint8_t bfinal;
    uint8_t blktyp;
    vec lit_tree;
    vec dst_tree;
    vec dynlits;
    vec dyndists;
    do {
        bfinal = readbits(&strm, &bit, 1);
        blktyp = readbits(&strm, &bit, 2);
        if (blktyp == NO_COMPRESSION) {
            DEBUG("No Compression Block%s", bfinal ? " -- Final Block" : "");
            // flush bit buffer to be on byte boundary
            if (bit != 0) stream_read_consume(&strm, 1);
            bit = 0;
            uint16_t len, nlen;
            if (strm.avail_in < 4)
                refill_or_panic(&strm);
            // 2-byte little endian read
            len = (strm.next_in[1] << 8) | strm.next_in[0];
            // 2-byte little endian read
            nlen = (strm.next_in[3] << 8) | strm.next_in[2];
            stream_read_consume(&strm, 4);
            if ((len & 0xffffu) != (nlen ^ 0xffffu))
                panic("invalid stored block lengths: %u %u", len, nlen);
            DEBUG("\tlen = %u, nlen = %u", len, nlen);
            do {
                if (strm.avail_in < len)
                    // XXX(peter): could hoist error check to end, would write
                    // zeros for section. do we guarantee to detect errors
                    // asap?
                    refill_or_panic(&strm);
                size_t avail = MIN(len, strm.avail_in);
                assert(avail <= len);
                if (stream_write(&strm, strm.next_in, avail))
                    panic("failed to write output: %d", strm.error);
                window_add(&strm, strm.next_in, avail);
                stream_read_consume(&strm, avail);
                len -= avail;
            } while (len > 0);
        } else if (blktyp == FIXED_HUFFMAN || blktyp == DYNAMIC_HUFFMAN) {
            // TODO(peter): max output from a literal or length+distance code is
            // 258 bytes so just require that output buffer has that must space
            // at beginning of loop?
            if (blktyp == FIXED_HUFFMAN) {
                DEBUG("Fixed Huffman Block%s", bfinal ? " -- Final Block" : "");
                lit_tree.d = fixed_huffman_literals_tree;
                lit_tree.len = sizeof(fixed_huffman_literals_tree);
                dst_tree.d = fixed_huffman_distance_tree;
                dst_tree.len = sizeof(fixed_huffman_distance_tree);
            } else {
                DEBUG("Dynamic Huffman Block%s",
                      bfinal ? " -- Final Block" : "");
                if (read_dynamic_trees(&strm, &bit, &dynlits, &dyndists) != 0)
                    panic("failed to read dynamic huffman trees");
                lit_tree = dynlits;
                dst_tree = dyndists;
            }

            for (;;) {
                uint16_t value = read_huffman_value(&strm, &bit, lit_tree);
                if (value < 256) {
                    uint8_t c = (uint8_t)value;
                    if (stream_write(&strm, &c, sizeof(c)) != 0)
                        panic("failed to write value in fixed huffman section");
                    window_add(&strm, &c, sizeof(c));
                } else if (value == 256) {
                    DEBUG("inflate: end of %s huffman block found", "fixed");
                    break;
                } else if (value <= 285) {
                    assert(257 <= value <= 285);
                    // NOTE: 257 <= value <= 285
                    // => 0 <= (value - LENGTH_BASE_CODE) <= 28
                    // => value - LENGTH_BASE_CODE < asize(LENGTH_EXTRA_BITS)
                    value -= LENGTH_BASE_CODE;
                    size_t base_length = LENGTH_BASES[value];
                    size_t extra_length =
                        readbits(&strm, &bit, LENGTH_EXTRA_BITS[value]);
                    size_t length = base_length + extra_length;
                    xassert(length <= 258, "invalid length");
                    size_t distance_code =
                        read_huffman_value(&strm, &bit, dst_tree);
                    xassert(distance_code < 32, "invalid distance code");
                    size_t base_distance = DISTANCE_BASES[distance_code];
                    size_t extra_distance = readbits(
                        &strm, &bit, DISTANCE_EXTRA_BITS[distance_code]);
                    size_t distance = base_distance + extra_distance;
                    if (check_distance(&strm, distance) != 0)
                        panic("invalid distance: dist=%zu wnd=%zu cap=%zu",
                              distance, wndsize(&strm), wndcap(&strm));
                    if (stream_window(&strm, distance, length) != 0)
                        panic("failed to write distance/length code");
                } else {
                    panic("invalid %s huffman value: %u", "huffman", value);
                }
            }

            // TODO(peter): revisit
            if (blktyp == DYNAMIC_HUFFMAN) {
                strm.zfree(strm.alloc_data, (void *)dynlits.d);
                dynlits.d = NULL;
                dynlits.len = 0;
                strm.zfree(strm.alloc_data, (void *)dyndists.d);
                dyndists.d = NULL;
                dyndists.len = 0;
            }
        } else {
            panic("Invalid block type: %u", blktyp);
        }
    } while (bfinal == 0);

    if (strm.flush(&strm) != 0) panic("write failed: %d", strm.error);

    close_file_stream(&strm);
    return 0;
}
