#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include "zlib.h"

#define panic(fmt, ...)                                     \
    do {                                                    \
        fprintf(stderr, "error: " fmt "\n", ##__VA_ARGS__); \
        ret = -128;                                         \
        goto exit;                                          \
    } while (0)

#define SIZE 32768U
// #define SIZE 1U /* TEMP: for testing */
#define FNAME (1U << 3)

static char ibuf[SIZE];
static char obuf[SIZE];
size_t have = 0;
uint8_t last = -1;
uint8_t *in = ibuf;
FILE *src = NULL;
FILE *dst = NULL;

uint8_t next() {
    if (have == 0) {
        in = ibuf;
        if ((have = fread(in, 1, SIZE, src)) != SIZE) {
            if (ferror(src)) have = 0;
        }
    }
    if (have) {
        --have;
        last = *in++;
    } else {
        last = -1;
    }
    return last;
}

const char *xlate(int r)
{
    switch  (r) {
        case Z_OK: return "ok";
        case Z_STREAM_END: return "stream end";
        case Z_NEED_DICT: return "need dict";
        case Z_ERRNO: return "errno";
        case Z_STREAM_ERROR: return "stream error";
        case Z_DATA_ERROR: return "data error";
        case Z_MEM_ERROR: return "memory error";
        case Z_BUF_ERROR: return "buffer error";
        case Z_VERSION_ERROR: return "version error";
        default: return "unknown";
    }
}

int main(int argc, char **argv) {
    uint8_t id1, id2, cm, flg, xfl, os;
    uint32_t mtime;
    const char *inname, *outname;
    int ret = 0;
    z_stream strm;

    if (argc == 2) {
        inname = argv[1];
        outname = NULL;
    } else if (argc == 3) {
        inname = argv[1];
        outname = argv[2];
    } else {
        fprintf(stderr, "usage: %s [IN] [OUT]?\n", argv[0]);
        return 0;
    }

    src = fopen(inname, "rb");
    dst = outname ? fopen(outname, "wb") : stdout;
    if (!src || !dst) panic("failed to open i/o");

#if 1
    id1 = next();
    id2 = next();
    if (id1 != 31 && id2 != 139) panic("invalid gzip header: %u %u", id1, id2);

    cm = next();
    if (cm != 8) panic("unsupported compression method: %u", last);
    flg = next(); /* flg */
    mtime = 0;
    mtime |= (next() << 0); /* mtime -- 4 bytes */
    mtime |= (next() << 8);
    mtime |= (next() << 16);
    mtime |= (next() << 24);
    xfl = next(); /* xfl */
    os = next();  /* os */

    if ((flg & ~FNAME) != 0) panic("other flag options not supported");
    if ((flg & FNAME) != 0) { /* fname */
        while (next() != 0 && last != -1)
            ;
    }

    printf("GZIP\n");
    printf("\tid1   = %u (0x%02x)\n", id1, id1);
    printf("\tid2   = %u (0x%02x)\n", id2, id2);
    printf("\tcm    = %u\n", cm);
    printf("\tflg   = %u\n", flg);
    printf("\tmtime = %u\n", mtime);
    printf("\txfl   = %u\n", xfl);
    printf("\tos    = %u\n", os);
    printf("Great success! -- %zu\n", have);
#endif

    /* allocate default state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = have;
    strm.next_in = in;
    ret = inflateInit2(&strm, -15);
    printf("result of inflateInit: %s\n", xlate(ret));
    if (ret != Z_OK) panic("deflateInit failed: %d", ret);

    do {
        if (strm.avail_in == 0) {
            strm.avail_in = fread(ibuf, 1, SIZE, src);
            if (ferror(src)) {
                inflateEnd(&strm);
                printf("result of inflateEnd: %s\n", xlate(ret));
                panic("fread failed: %d", errno);
            }
            if (strm.avail_in == 0) break;
            strm.next_in = ibuf;
        }

        do {
            strm.avail_out = SIZE;
            strm.next_out = obuf;
            ret = inflate(&strm, Z_NO_FLUSH);
            printf("result of inflate: %s\n", xlate(ret));
            assert(ret != Z_STREAM_ERROR);
            switch (ret) {
                case Z_NEED_DICT:
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    inflateEnd(&strm);
                    printf("result of inflateEnd: %s %s\n", xlate(ret), strm.msg);
                    goto exit;
                default:
                    break;
            }
            have = SIZE - strm.avail_out;
            if (fwrite(obuf, 1, have, dst) != have || ferror(dst)) {
                inflateEnd(&strm);
                printf("result of inflateEnd: %s\n", xlate(ret));
                ret = Z_ERRNO;
                goto exit;
            }
        } while (strm.avail_out == 0);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    printf("result of inflateEnd: %s\n", xlate(ret));
    ret = ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;

exit:
    fclose(src);
    fclose(dst);

    const char *msg = "unknown";
    switch (ret) {
        case Z_OK:
            msg = "OK";
            break;
        case Z_STREAM_END:
            msg = "STREAM_END";
            break;
        case Z_NEED_DICT:
            msg = "NEED_DICT";
            break;
        case Z_ERRNO:
            msg = "ERRNO";
            break;
        case Z_STREAM_ERROR:
            msg = "STREAM_ERROR";
            break;
        case Z_DATA_ERROR:
            msg = "DATA_ERROR";
            break;
        case Z_MEM_ERROR:
            msg = "MEM_ERROR";
            break;
        case Z_BUF_ERROR:
            msg = "BUF_ERROR";
            break;
        case Z_VERSION_ERROR:
            msg = "VERSION_ERROR";
            break;
        default:
            msg = "INTERNAL ERROR";
            break;
    }
    printf("result: %s\n", msg);

    return ret;
}
