#include "pzlib.h"

#ifndef USE_ZLIB

const char *zlibVersion()
{
    return "pzlib 0.0.1";
}

int inflateInit2_(z_streamp strm, int  windowBits, const char *version, int stream_size)
{
    return Z_DATA_ERROR;
}

#endif
