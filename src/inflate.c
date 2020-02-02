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

#define xassert(c, fmt, ...)                                     \
    do {                                                         \
        if (!(c)) {                                              \
            fprintf(stderr, "ASSERT: " fmt "\n", ##__VA_ARGS__); \
            assert(0);                                           \
        }                                                        \
    } while (0)

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

struct gzip_header
{
    uint8_t id1;
    uint8_t id2;
    uint8_t cm;
    uint8_t flg;
    uint32_t mtime;
    uint8_t xfl;
    uint8_t os;
} __attribute__((packed));
typedef struct gzip_header gzip_header;

struct stream
{
    const uint8_t *beg;
    const uint8_t *cur; /* beg <= cur <= end */
    const uint8_t *end;
    int            error; /* 0 = OK, otherwise returns result of ferror */
    int          (*refill)(struct stream* s);
    void          *udata;
};
typedef struct stream stream;

static const uint8_t _zeros[256] = { 0 };

int refill_zeros(stream *s)
{
    s->beg = &_zeros[0];
    s->cur = s->beg;
    s->end = s->beg + sizeof(_zeros);
    return s->error;
}

void init_zeros_stream(stream *s)
{
    s->refill = &refill_zeros;
    s->error = 0;
    s->udata = NULL;
    s->refill(s);
}

struct file_stream
{
    uint8_t buf[2048];
    FILE   *fp;
};

int refill_file(stream *s)
{
    struct file_stream *d = s->udata;
    size_t rem = s->end - s->cur;
    size_t read;
    assert(s->beg <= s->cur && s->cur <= s->end);
    memmove(&d->buf[0], s->cur, rem);
    read = fread(&d->buf[rem], 1, sizeof(d->buf) - rem, d->fp);
    if (read > 0) {
        s->beg = &d->buf[0];
        s->cur = &d->buf[rem];
        s->end = &d->buf[rem+read];
        // TODO(peter): check ferror if `read != sizeof(d->buf) - rem`?
        assert(s->beg <= s->cur && s->cur <= s->end);
        assert(s->end <= &d->buf[sizeof(d->buf)]);
        return 0;
    } else {
        init_zeros_stream(s);
        s->error = ferror(d->fp);
        return s->error;
    }
}

void init_file_stream(stream *s, FILE *fp)
{
    static struct file_stream data;
    data.fp = fp;
    s->beg = NULL;
    s->cur = NULL;
    s->end = NULL;
    s->error = 0;
    s->refill = &refill_file;
    s->udata = &data;
    s->refill(s);
}

int stream_read(stream* s, void* buf, size_t n)
{
    if (s->end - s->cur < n) {
        if (s->refill(s) != 0)
            return s->error;
        // TODO(peter): error for "not enough data"
        if (s->end - s->cur < n)
            panic("not enough data: desired %zu, available %zu", n, s->end - s->cur);
    }
    memcpy(buf, s->cur, n);
    s->cur += n;
    return 0;
}

int main(int argc, char** argv) {
    FILE* fp;
    FILE* out;
    char* input_filename = argv[1];
    char* output_filename;
    gzip_header hdr;
    stream strm;

    if (argc == 2) {
        output_filename = NULL;
    } else if (argc == 3) {
        output_filename = argv[2];
    } else {
        fprintf(stderr, "Usage: %s [FILE] [OUT]\n", argv[0]);
        exit(0);
    }

    fp = fopen(input_filename, "rb");
    if (!fp) {
        panic("failed to open input file: %s", input_filename);
    }

    out = output_filename ? fopen(output_filename, "wb") : stdout;
    if (!out) {
        panic("failed to open output file: %s", output_filename ?: "<stdout>");
    }

    init_file_stream(&strm, fp);
    if (stream_read(&strm, &hdr, sizeof(hdr)) != 0)
        panic("unable to read gzip header: %d", strm.error);
    // if (strm.end - strm.cur < sizeof(hdr)) {
    //     if (strm.refill(&strm) != 0) {
    //         panic("failed to refill file stream: %d", strm.error);
    //     }
    //     assert(strm.end - strm.cur >= sizeof(hdr));
    // }
    // memcpy(&hdr, strm.cur, sizeof(hdr));
    // strm.cur += sizeof(hdr);

    printf("GzipHeader:\n");
    printf("\tid1   = %u (0x%02x)\n", hdr.id1, hdr.id1);
    printf("\tid2   = %u (0x%02x)\n", hdr.id2, hdr.id2);
    printf("\tcm    = %u\n", hdr.cm);
    printf("\tflg   = %u\n", hdr.flg);
    printf("\tmtime = %u\n", hdr.mtime);
    printf("\txfl   = %u\n", hdr.xfl);
    printf("\tos    = %u\n", hdr.os);

    fclose(fp);
    fclose(out);
    return 0;
}
