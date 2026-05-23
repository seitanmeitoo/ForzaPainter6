#include "rng.h"

#include <math.h>

static uint64_t splitmix64(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void rng_seed(Rng *r, uint64_t seed)
{
    uint64_t sm = seed ? seed : 0xDEADBEEFCAFEBABEull;
    r->s0 = splitmix64(&sm);
    r->s1 = splitmix64(&sm);
    if (r->s0 == 0 && r->s1 == 0) r->s1 = 1;
}

uint64_t rng_u64(Rng *r)
{
    uint64_t x = r->s0;
    const uint64_t y = r->s1;
    r->s0 = y;
    x ^= x << 23;
    r->s1 = x ^ y ^ (x >> 17) ^ (y >> 26);
    return r->s1 + y;
}

uint32_t rng_u32(Rng *r)
{
    return (uint32_t)(rng_u64(r) >> 32);
}

uint32_t rng_u32_range(Rng *r, uint32_t hi)
{
    /* Lemire's debiased bounded random : evite le modulo bias. */
    uint64_t m = (uint64_t)rng_u32(r) * (uint64_t)hi;
    uint32_t l = (uint32_t)m;
    if (l < hi) {
        uint32_t t = (uint32_t)(-(int32_t)hi) % hi;
        while (l < t) {
            m = (uint64_t)rng_u32(r) * (uint64_t)hi;
            l = (uint32_t)m;
        }
    }
    return (uint32_t)(m >> 32);
}

int32_t rng_i32_range(Rng *r, int32_t lo, int32_t hi)
{
    if (hi < lo) { int32_t t = lo; lo = hi; hi = t; }
    uint32_t span = (uint32_t)(hi - lo) + 1u;
    return lo + (int32_t)rng_u32_range(r, span);
}

float rng_f01(Rng *r)
{
    /* 24 bits de mantisse pour float. */
    return (float)(rng_u32(r) >> 8) * (1.0f / 16777216.0f);
}

float rng_f_range(Rng *r, float lo, float hi)
{
    return lo + (hi - lo) * rng_f01(r);
}

float rng_gauss(Rng *r, float sigma)
{
    /* Box-Muller : tire deux uniformes, en ressort un gaussien (on jette le second). */
    float u1 = rng_f01(r);
    float u2 = rng_f01(r);
    if (u1 < 1e-7f) u1 = 1e-7f;
    float mag = sqrtf(-2.0f * logf(u1));
    return sigma * mag * cosf(2.0f * 3.14159265358979f * u2);
}
