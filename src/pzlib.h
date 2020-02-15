#pragma once

#define ZLIB_CONST
#include "zlib.h"


#ifndef USE_ZLIB
int PZ_inflate(z_streamp strm, int flush);
#endif

// #define USE_ZLIB
