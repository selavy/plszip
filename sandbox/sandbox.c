#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <zlib.h>


// #define CHUNK (1u << 14)
// #define CHUNK 16384
#define CHUNK 1

void printrc(int ret, int errnum)
{
#define XX(x) case x: desc = #x; break
    static char buf[1024];
    const char *desc = "unknown return code";
    switch (ret) {
        XX(Z_OK);
        XX(Z_STREAM_END);
        XX(Z_NEED_DICT);
        XX(Z_ERRNO);
        XX(Z_STREAM_ERROR);
        XX(Z_DATA_ERROR);
        XX(Z_MEM_ERROR);
        XX(Z_BUF_ERROR);
        XX(Z_VERSION_ERROR);
        default: break;
    }
    if (errnum != 0)
        snprintf(buf, sizeof(buf), "error: [%d] %s.", errno, strerror(errnum));
    else
        snprintf(buf, sizeof(buf), "no errors.");
    printf("Result: [%d] %s -- %s\n", ret, desc, buf);
#undef XX
}

int inf(FILE *src, FILE *dst)
{
    static uint8_t in[CHUNK];
    static uint8_t out[CHUNK];
    z_stream strm;
    int ret;
    size_t have;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, src);
        if (ferror(src)) {
            inflateEnd(&strm);
            return Z_ERRNO;
        }
        if (strm.avail_in == 0) {
            assert(feof(src));
            break;
        }
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);
            switch (ret) {
                case Z_NEED_DICT:
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    inflateEnd(&strm);
                    return ret;
                default:
                    break;
            }

            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dst) != have || ferror(dst)) {
                inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

int main(int argc, char **argv)
{
    int ret;
    FILE *src, *dst;
    const char *infile, *outfile = NULL;
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s [SRC] [DST]?\n", argv[0]);
        exit(0);
    }
    infile = argv[1];
    if (argc == 3) {
        outfile = argv[2];
    }
    src = fopen(infile, "rb");
    if (!src) {
        fprintf(stderr, "error: unable to open input file: '%s'\n", infile);
        fclose(src);
        fclose(dst);
        exit(1);
    }
    dst = outfile ? fopen(outfile, "wb") : stdout;
    if (!dst) {
        fprintf(stderr, "error: unable to open output file: '%s'\n", outfile);
        fclose(src);
        fclose(dst);
        exit(1);
    }

    ret = inf(src, dst);
    printrc(ret, errno);
    fclose(src);
    fclose(dst);
    return 0;
}