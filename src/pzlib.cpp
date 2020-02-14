#include "pzlib.h"

#ifndef USE_ZLIB

#include <cstdio>
#include <cstdint>
#include <cstring>

const char *zlibVersion()
{
    return "pzlib 0.0.1";
}

int inflateInit2_(z_streamp strm, int  windowBits, const char *version, int stream_size)
{
    printf("pzlib::inflateInit2_\n");
    if (strcmp(version, ZLIB_VERSION) != 0) {
        return Z_VERSION_ERROR;
    }
    if (stream_size != sizeof(z_stream)) {
        return Z_VERSION_ERROR;
    }
    return Z_OK;
}

int inflateEnd(z_streamp strm)
{
    printf("pzlib::inflateEnd\n");
    return Z_OK;
}

int inflate(z_streamp strm, int flush)
{
    printf("pzlib::inflate\n");
    strm->msg = (char*)"Not yet implemented";
    return Z_DATA_ERROR;
}

#endif
