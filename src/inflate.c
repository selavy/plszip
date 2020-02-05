#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define panic(fmt, ...)                                   \
    do {                                                  \
        fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); \
        exit(1);                                          \
    } while (0)

#ifdef NDEBUG
#define xassert(c, fmt, ...)
#else
#define xassert(c, fmt, ...)                                     \
    do {                                                         \
        if (!(c)) {                                              \
            fprintf(stderr, "ASSERT: " fmt "\n", ##__VA_ARGS__); \
            assert(0);                                           \
        }                                                        \
    } while (0)
#endif

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

#ifdef NDEBUG
#define DEBUG(fmt, ...)
#else
#define DEBUG(fmt, ...) fprintf(stderr, "DBG: " fmt "\n", ##__VA_ARGS__)
#endif

#define MIN(x, y) (x) < (y) ? (x) : (y)

/* Header Constants */
static const uint8_t ID1_GZIP = 31;
static const uint8_t ID2_GZIP = 139;

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
static const uint8_t _zeros[256] = {0};

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

struct stream {
    const uint8_t *next_in;
    size_t avail_in;
    size_t total_in;
    int (*refill)(struct stream *s);
    void *read_data;

    uint8_t *next_out;
    size_t avail_out;
    size_t total_out;
    int (*flush)(struct stream *s);
    void *write_data;

    int error;
};
typedef struct stream stream;

struct file_read_data {
    // uint8_t buf[2048]; // TODO(peter): resize to max size of zlib window
    uint8_t buf[24];  // TEMP TEMP -- for testing only
    FILE *fp;
};
typedef struct file_read_data file_read_data;
struct file_write_data {
    // uint8_t buf[2048];  // TODO(peter): resize to max size of zlib window
    uint8_t buf[24];  // TEMP TEMP -- for testing only
    FILE *fp;
};
typedef struct file_write_data file_write_data;

int refill_file(stream *s)
{
    file_read_data *d = s->read_data;
    size_t rem = s->avail_in;
    size_t read;
    memmove(&d->buf[0], s->next_in, rem);
    DEBUG("rrefill_file: avail_in=%zu, readsize=%zu", s->avail_in,
          sizeof(d->buf) - rem);
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
    s->error = out != avail ? ferror(d->fp) : 0;
    return s->error;
}

void init_file_stream(stream *s, file_read_data *read_data, file_write_data *write_data)
{
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
}

void close_file_stream(stream *s)
{
    file_read_data *read_data = s->read_data;
    file_write_data *write_data = s->write_data;
    fclose(read_data->fp);
    fclose(write_data->fp);
}

int stream_read(stream *s, void *buf, size_t n) {
    DEBUG("stream_read: %zu", n);
    /* fast path: plenty of data available */
    if (s->avail_in >= n) {
        memcpy(buf, s->next_in, n);
        s->next_in += n;
        s->avail_in -= n;
        s->total_in += n;
        return 0;
    }

    uint8_t *p = buf;
    do {
        DEBUG("s->avail_in = %zu", s->avail_in);
        if (s->avail_in < n) {
            if (s->refill(s) != 0) return s->error;
        }
        xassert(s->avail_in > 0 || s->avail_in == n,
                "refill did not add any bytes!");
        size_t avail = MIN(n, s->avail_in);
        memcpy(p, s->next_in, avail);
        s->next_in += avail;
        s->avail_in -= avail;
        s->total_in += avail;
        p += avail;
        n -= avail;
    } while (n > 0);
    return 0;
}

int stream_write(stream *s, const void *buf, size_t n) {
    DEBUG("stream_write: %zu", n);
    if (s->avail_out >= n) {
        memcpy(s->next_out, buf, n);
        s->next_out += n;
        s->avail_out -= n;
        s->total_out += n;
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
        s->next_out += avail;
        s->avail_out -= avail;
        s->total_out += avail;
        p += avail;
        n -= avail;
    } while (n > 0);
    return 0;
}

char *read_null_terminated_string(stream *s) {
    DEBUG("read_null_terminated_string");
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
            s->next_in += pos;
            s->avail_in -= pos;
            s->total_in += pos;
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

uint8_t readbits(stream *s, size_t *bitpos, size_t nbits) {
    assert(0 <= nbits && nbits <= 8);
    uint8_t result = 0;
    for (size_t i = 0; i < nbits; ++i) {
        if (*bitpos == 8) {
            if (--s->avail_in == 0)
                if (s->refill(s) != 0) panic("read error: %d", s->error);
            *bitpos = 0;
        }
        result |= ((s->next_in[0] >> *bitpos) & 0x1u) << i;
        ++*bitpos;
    }
    return result;
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

    DEBUG("GzipHeader:");
    DEBUG("\tid1   = %u (0x%02x)", hdr.id1, hdr.id1);
    DEBUG("\tid2   = %u (0x%02x)", hdr.id2, hdr.id2);
    DEBUG("\tcm    = %u", hdr.cm);
    DEBUG("\tflg   = %u", hdr.flg);
    DEBUG("\tmtime = %u", hdr.mtime);
    DEBUG("\txfl   = %u", hdr.xfl);
    DEBUG("\tos    = %u", hdr.os);

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
        char *orig_filename = read_null_terminated_string(&strm);
        if (!orig_filename)
            panic("unable to read original filename: %d", strm.error);
        DEBUG("File contains original filename!: '%s'", orig_filename);
        free(orig_filename);
    }
    if ((hdr.flg & FCOMMENT) != 0) {
        // +===================================+
        // |...file comment, zero-terminated...| (more-->)
        // +===================================+
        DEBUG("File contains comment");
        char *comment = read_null_terminated_string(&strm);
        if (!comment) panic("unable to read file comment: %d", strm.error);
        DEBUG("File comment: '%s'", comment);
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
        DEBUG("CRC16: %u (0x%04X)", crc16, crc16);
    }
    if ((hdr.flg & (RESERV1 | RESERV2 | RESERV3)) != 0)
        panic("reserve bits are not 0");

    /**************************************************************************
     * Read compressed data
     *************************************************************************/

    /* know at this point that are on a byte boundary as all previous fields
     * have been byte sized */
    size_t bitpos = 0;
    size_t nbits;
    uint8_t bfinal;
    uint8_t blktyp;
    do {
        bfinal = readbits(&strm, &bitpos, 1);
        blktyp = readbits(&strm, &bitpos, 2);
        if (blktyp == NO_COMPRESSION) {
            DEBUG("No Compression Block%s", bfinal ? " -- Final Block" : "");
            // flush bit buffer to be on byte boundary
            if (bitpos != 0) {
                ++strm.next_in;
                --strm.avail_in;
                ++strm.total_in;
            }
            bitpos = 0;
            uint16_t len, nlen;
            if (strm.avail_in < 4)
                if (strm.refill(&strm) != 0)
                    panic("refill failed: %d", strm.error);
            // 2-byte little endian read
            len = (strm.next_in[1] << 8) | strm.next_in[0];
            // 2-byte little endian read
            nlen = (strm.next_in[3] << 8) | strm.next_in[2];
            strm.next_in += 4;
            strm.avail_in -= 4;
            strm.total_in += 4;
            if ((len & 0xffffu) != (nlen ^ 0xffffu))
                panic("invalid stored block lengths: %u %u", len, nlen);
            DEBUG("\tlen = %u, nlen = %u", len, nlen);
            while (len > 0) {
                if (strm.avail_in < len)
                    // XXX(peter): could hoist error check to end, would write
                    // zeros for section. do we guarantee to detect errors
                    // asap?
                    if (strm.refill(&strm) != 0)
                        panic("refill failed: %d", strm.error);
                size_t avail = MIN(len, strm.avail_in);
                assert(avail <= len);
                // DEBUG("XFER len=%u, avail=%zu => %zu", len, strm.avail_in,
                // avail);
                // TODO: add write buffer
                if (stream_write(&strm, strm.next_in, avail))
                    panic("failed to write output: %d", strm.error);
                strm.next_in += avail;
                strm.avail_in -= avail;
                strm.total_in += avail;
                len -= avail;
            }
        } else if (blktyp == FIXED_HUFFMAN) {
            DEBUG("Fixed Huffman Block%s", bfinal ? " -- Final Block" : "");
            panic("not implemented yet");
        } else if (blktyp == DYNAMIC_HUFFMAN) {
            DEBUG("Dynamic Huffman Block%s", bfinal ? " -- Final Block" : "");
            panic("not implemented yet");
        } else {
            panic("Invalid block type: %u", blktyp);
        }
    } while (bfinal == 0);

    if (strm.flush(&strm) != 0) panic("write failed: %d", strm.error);

    close_file_stream(&strm);
    return 0;
}
