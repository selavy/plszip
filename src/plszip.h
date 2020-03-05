#pragma once

// #define USE_ZLIB

#define ZLIB_CONST
#include "zlib.h"

#ifndef USE_ZLIB
int PLS_inflate(z_streamp strm, int flush);
#endif
