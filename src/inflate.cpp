#include <errno.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define USE_ZLIB

#ifdef USE_ZLIB
#include "zlib.h"
#else

#define ZEXTERN extern
#define ZEXPORT
typedef unsigned char Byte;
typedef unsigned int uInt;   /* 16 bits or more */
typedef unsigned long uLong; /* 32 bits or more */

typedef Byte byte_t;
typedef uInt uint_t;
typedef uLong ulong_t;

typedef void *(*alloc_func)(void *opaque, uint_t items, uint_t size);
typedef void *(*free_func)(void *opaque, void *address);

struct internal_state;

struct z_stream_s {
    const byte_t *next_in; /* next input byte */
    uint_t avail_in;       /* number of bytes available at next_in */
    ulong_t total_in;      /* total number of input bytes read so far */

    const Bytef *next_out; /* next output byte will go here */
    uint_t avail_out;      /* remaining free space at next_out */
    ulong_t total_out;     /* total number of bytes output so far */

    const char *msg; /* last error message, NULL if no error */
    struct internal_state *state;

    alloc_func zalloc; /* used to allocate the internal state */
    free_func zfree;   /* use to free the internal state */
    voidpf opaque;     /* private data object passed to zalloc and zfree */

    int data_type;    /* best guess about the data type: binary or text
                         for default, or the decoding state for inflate */
    ulong_t adler;    /* Adler-32 or CRC-32 value of the uncompressed data */
    ulong_t reserved; /* reserved for future use */
};
typedef struct z_stream_s z_stream;

/* constants */

#define Z_NO_FLUSH 0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH 2
#define Z_FULL_FLUSH 3
#define Z_FINISH 4
#define Z_BLOCK 5
#define Z_TREES 6
/* Allowed flush values; see deflate() and inflate() below for details */

#define Z_OK 0
#define Z_STREAM_END 1
#define Z_NEED_DICT 2
#define Z_ERRNO (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR (-3)
#define Z_MEM_ERROR (-4)
#define Z_BUF_ERROR (-5)
#define Z_VERSION_ERROR (-6)
/* Return codes for the compression/decompression functions. Negative values
 * are errors, positive values are used for special but normal events.
 */

#define Z_NO_COMPRESSION 0
#define Z_BEST_SPEED 1
#define Z_BEST_COMPRESSION 9
#define Z_DEFAULT_COMPRESSION (-1)
/* compression levels */

#define Z_FILTERED 1
#define Z_HUFFMAN_ONLY 2
#define Z_RLE 3
#define Z_FIXED 4
#define Z_DEFAULT_STRATEGY 0
/* compression strategy; see deflateInit2() below for details */

#define Z_BINARY 0
#define Z_TEXT 1
#define Z_ASCII Z_TEXT /* for compatibility with 1.2.2 and earlier */
#define Z_UNKNOWN 2
/* Possible values of the data_type field for deflate() */

#define Z_DEFLATED 8
/* The deflate compression method (the only one supported in this version) */

#define Z_NULL 0 /* for initializing zalloc, zfree, opaque */

#define ZEXTERN extern

ZEXTERN int inflateInit(z_stream *strm);
ZEXTERN int inflateInit2(z_stream *strm, int windowBits);
ZEXTERN int inflate(z_stream *strm, int flush);
ZEXTERN int inflateEnd(z_stream *strm);

#endif

/* -------------------------------------------------------------------------- */

#define SIZE 32768U
#define PARSE_GZIP 16

int main(int argc, char **argv) {
    static char ibuf[SIZE];
    static char obuf[SIZE];
    const char *inname, *outname;
    FILE *src, *dst;
    size_t have;
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
        fprintf(stderr, "error: failed to initialize inflate library\n");
        goto exit;
    }

    do {
        strm.avail_in = fread(ibuf, 1, SIZE, src);
        if (ferror(src)) {
            ret = errno;
            inflateEnd(&strm);
            fprintf(stderr, "error reading from input: %s\n", strerror(ret));
            goto exit;
        }
        if (strm.avail_in == 0) break;
        strm.next_in = (Bytef *)ibuf;
        do {
            strm.avail_out = SIZE;
            strm.next_out = (Bytef *)obuf;
            ret = inflate(&strm, Z_NO_FLUSH);
            switch (ret) {
                case Z_NEED_DICT:
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    inflateEnd(&strm);
                    fprintf(stderr, "inflate error: %s\n", strm.msg);
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
