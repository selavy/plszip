#include <errno.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pzlib.h"

/* -------------------------------------------------------------------------- */

const char *xlaterc(int rc) {
    switch (rc) {
        case Z_OK: return "OK";
        case Z_STREAM_END: return "StreamEnd";
        case Z_NEED_DICT: return "NeedDictionary";
        case Z_ERRNO: return "Errno";
        case Z_STREAM_ERROR: return "StreamError";
        case Z_DATA_ERROR: return "DataError";
        case Z_MEM_ERROR: return "MemoryError";
        case Z_BUF_ERROR: return "BufferError";
        case Z_VERSION_ERROR: return "VersionError";
        default: return "Unknown";
    }
}

#define SIZE 32768U
// #define SIZE 256U
// #define SIZE 1U
#define PARSE_GZIP 16

int main(int argc, char **argv) {
    static char ibuf[SIZE];
    static char obuf[SIZE];
    const char *inname, *outname;
    FILE *src, *dst;
    size_t have;
    int ret = 0;
    z_stream strm;

    printf("inflate: %s\n", zlibVersion());

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
    if (!src || !dst) {
        fprintf(stderr, "error: unable to open %s file: %s\n",
                !src ? "input" : "output",
                !src ? inname : (outname ? outname : "stdout"));
        return 1;
    }

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = inflateInit2(&strm, 15 + 16);
    if (ret != Z_OK) {
        fprintf(stderr, "error: failed to initialize inflate library: %s\n", strm.msg);
        goto exit;
    }

    do {
        strm.avail_in = static_cast<uInt>(fread(ibuf, 1, SIZE, src));
        if (ferror(src)) {
            ret = errno;
            inflateEnd(&strm);
            fprintf(stderr, "error reading from input: %s\n", strerror(ret));
            goto exit;
        }
        if (strm.avail_in == 0) break;
        strm.next_in = reinterpret_cast<Bytef *>(ibuf);
        do {
            strm.avail_out = SIZE;
            strm.next_out = reinterpret_cast<Bytef *>(obuf);
// TEMP TEMP: use different name to not confuse gdb
#ifdef USE_ZLIB
            ret = inflate(&strm, Z_NO_FLUSH);
#else
            ret = PZ_inflate(&strm, Z_NO_FLUSH);
#endif
            // NOTE(peter): Z_BUF_ERROR is NOT fatal. It will be called if:
            // "no progress was possible or if there was not enough room in the output
            // buffer when Z_FINISH is used. Note that Z_BUF_ERROR is not fatal, and
            // inflate() can be called again with more input and more output space to
            // continue decompressing."

            // printf("\nRET = [%d] %s\n", ret, xlaterc(ret));
            // if (ret < Z_OK) {
            //     inflateEnd(&strm);
            //     fprintf(stderr, "inflate error[%d]: %s\n", ret, strm.msg);
            //     goto exit;
            // }
            switch (ret) {
                case Z_STREAM_ERROR:
                case Z_NEED_DICT:
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    inflateEnd(&strm);
                    fprintf(stderr, "inflate error[%d]: %s\n", ret, strm.msg);
                    goto exit;
                default:
                    break;
            }
            have = SIZE - strm.avail_out;
            if (fwrite(obuf, 1, have, dst) != have || ferror(dst)) {
                ret = errno;
                inflateEnd(&strm);
                fprintf(stderr, "write error: %s\n", strerror(ret));
            }
        } while (strm.avail_out == 0);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    ret = 0;

exit:
    fclose(src);
    fclose(dst);
    return ret;
}
