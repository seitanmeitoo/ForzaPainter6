#ifndef VINYL_RNG_H
#define VINYL_RNG_H

#include <stdint.h>

/* xorshift128+ : rapide, qualite statistique correcte pour Monte Carlo.
 * Etat seed via splitmix64 pour eviter les zero-states. */
typedef struct Rng {
    uint64_t s0, s1;
} Rng;

void     rng_seed(Rng *r, uint64_t seed);
uint64_t rng_u64(Rng *r);
uint32_t rng_u32(Rng *r);
uint32_t rng_u32_range(Rng *r, uint32_t hi);   /* [0..hi)  ; UB si hi==0 */
int32_t  rng_i32_range(Rng *r, int32_t lo, int32_t hi);  /* [lo..hi] inclusif */
float    rng_f01(Rng *r);                       /* [0..1) */
float    rng_f_range(Rng *r, float lo, float hi);  /* [lo..hi) */
float    rng_gauss(Rng *r, float sigma);        /* N(0, sigma^2) via Box-Muller */

#endif
