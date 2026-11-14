#ifndef RNG_H
#define RNG_H

#include <stdbool.h>
#include <stdint.h>

/* SplitMix64: tiny, fast, good enough for gameplay randomness. */
typedef struct { uint64_t s; } Rng;

static inline void rng_seed(Rng *r, uint64_t seed) { r->s = seed; }

static inline uint64_t rng_next(Rng *r) {
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* Uniform float in [0, 1). */
static inline float rng_float(Rng *r) { return (float)(rng_next(r) >> 40) / 16777216.0f; }
static inline float rng_range(Rng *r, float lo, float hi) { return lo + (hi - lo) * rng_float(r); }
/* Uniform int in [lo, hi] (inclusive). */
static inline int rng_int(Rng *r, int lo, int hi) {
    return lo + (int)(rng_next(r) % (uint64_t)(hi - lo + 1));
}
static inline bool rng_chance(Rng *r, float p) { return rng_float(r) < p; }

#endif
