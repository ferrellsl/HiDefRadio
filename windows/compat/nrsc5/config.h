/* nrsc5 config.h for the Windows build (MinGW-w64 gcc). */
#pragma once
#define USE_FAAD2
#include <complex.h>
#ifndef CMPLXF
#define CMPLXF(x, y) __builtin_complex((float)(x), (float)(y))
#endif
#define LIBRARY_DEBUG_LEVEL 5
#define GIT_COMMIT_HASH "v3.2.0"

/* conv_dec.c wants 16-byte aligned memory for its SSE Viterbi decoder.
 * Windows has no memalign(), but 64-bit Windows malloc() always returns
 * 16-byte aligned blocks, and the memory is released with free(). */
#ifdef _WIN64
#include <stdlib.h>
#define memalign(align, size) malloc(size)
#endif
