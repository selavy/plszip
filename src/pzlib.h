#ifndef PZLIB__H_
#define PZLIB__H_

#include <stdint.h>

#define PZ_EXTERN extern

typedef unsigned int uint_t;
typedef unsigned long ulong_t;
typedef uint8_t byte_t;

/*
 * ----------------------------------------------------------------------------
 * TODO: move all above into separate pzconf.h
 * ----------------------------------------------------------------------------
 */

struct internal_state;

typedef void* (*pz_alloc_func)(void* data, uint_t items, uint_t size);
typedef void* (*pz_free_func)(void* data, void* address);

struct pz_stream_s {
    const byte_t* next_in; /* next input byte */
    uint avail_in;         /* number of bytes available at next_in */
    ulong total_in;        /* total number of input bytes read so far */

    const byte_t* next_out; /* next output byte will go here */
    uint_t avail_out;       /* remaining free space at next_out */
    ulong_t total_out;      /* total number of bytes output so far */

    const char* msg; /* last error message, NULL if no error */
    struct internal_state* state;

    pz_alloc_func zalloc; /* used to allocate the internal state */
    pz_free_func zfree;   /* use to free the internal state */
    void* opaque;         /* private data object passed to zalloc and zfree */

    int data_type;    /* best guess about the data type: binary or text
                         for default, or the decoding state for inflate */
    ulong_t adler;    /* Adler-32 or CRC-32 value of the uncompressed data */
    ulong_t reserved; /* reserved for future use */
};
typedef struct pz_stream_s pz_stream;

#define PZ_NO_FLUSH 0
#define PZ_PARTIAL_FLUSH 1
#define PZ_SYNC_FLUSH 2
#define PZ_FULL_FLUSH 3
#define PZ_FINISH 4
#define PZ_BLOCK 5
#define PZ_TREES 6

#define PZ_OK 0
#define PZ_STREAM_END 1
#define PZ_NEED_DICT 2
#define PZ_ERRNO (-1)
#define PZ_STREAM_ERROR (-2)
#define PZ_DATA_ERROR (-3)
#define PZ_MEM_ERROR (-4)
#define PZ_BUF_ERROR (-5)
#define PZ_VERSION_ERROR (-6)

#define PZ_BINARY 0
#define PZ_TEXT 1
#define PZ_ASCII Z_TEXT
#define PZ_UNKNOWN 2

#define PZ_NULL NULL

PZ_EXTERN const char* pzlibVersion();
PZ_EXTERN int inflateInit(pz_stream* strm);
/*
PZ_EXTERN int inflateInit2(pz_stream* strm, int windowBits, const char* version,
                           int stream_size);
*/
PZ_EXTERN int inflateReset(pz_stream* strm);
PZ_EXTERN int inflateReset2(pz_stream* strm, int windowBits);
PZ_EXTERN int inflate(pz_stream* strm, int flush);

#endif  // PZLIB__H_
