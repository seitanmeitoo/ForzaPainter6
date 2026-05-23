#ifndef VINYL_UTIL_H
#define VINYL_UTIL_H

#include <stdint.h>

static inline int   imin(int a, int b)   { return a < b ? a : b; }
static inline int   imax(int a, int b)   { return a > b ? a : b; }
static inline int   iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float fclampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

#endif
