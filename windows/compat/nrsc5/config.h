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
#elif defined(HIDEF_CONV_DEC)
/* 32-bit Windows malloc() only returns 8-byte aligned blocks, and the SSE decoder's
 * aligned loads (_mm_load_si128) fault on those. The 32-bit build compiles conv_dec.c
 * (only) with HIDEF_CONV_DEC, so all its allocations come from MinGW's aligned heap and
 * go back to it. Nothing it allocates leaves conv_dec.c. */
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
static inline void *hidef_aligned_calloc(size_t n, size_t size) {
    void *p = __mingw_aligned_malloc(n * size, 16);
    if (p) memset(p, 0, n * size);
    return p;
}
#define memalign(align, size) __mingw_aligned_malloc((size), (align))
#define malloc(size) __mingw_aligned_malloc((size), 16)
#define calloc(n, size) hidef_aligned_calloc((n), (size))
#define free(p) __mingw_aligned_free(p)
#endif
