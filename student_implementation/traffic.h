/* =========================================================================
 * traffic.h
 *
 * Project 13 -- Wiring the Data Centre
 * WEEK 3 MODULE: seeded warehouse-scale traffic generation.
 *
 * Provides the two traffic patterns required by the Project 13 brief
 * (Section G / Section H):
 *
 *   TRAFFIC_UNIFORM  -- uniform-random: every node is equally likely to
 *                       be the destination of any packet (self excluded).
 *   TRAFFIC_HOTNODE  -- hot-node-skewed: a small subset of "database"
 *                       nodes receives a disproportionate share of all
 *                       traffic, modelling a real data-centre access skew.
 *
 * Reproducibility: the generator is driven by an explicit xorshift64*
 * generator (Marsaglia), seeded through SplitMix64, rather than by
 * rand(), so that
 *   (a) results are identical on every machine and every libc, and
 *   (b) the Python reference generator
 *       python/gen_datacenter_traffic.py reproduces the *same* stream
 *       bit-for-bit from the same seed.
 *
 * Group 1 seed: 1301.
 * ========================================================================= */

#ifndef TRAFFIC_H
#define TRAFFIC_H

#include <stdint.h>

#define MAX_HOT_NODES 4

/* ---- Deterministic RNG ------------------------------------------------ */
typedef struct {
    uint64_t state;
} rng_t;

void     rng_seed(rng_t *r, uint32_t seed);
uint32_t rng_next_u32(rng_t *r);
double   rng_next_double(rng_t *r);      /* uniform in [0,1)              */
int      rng_next_int(rng_t *r, int n);  /* uniform integer in [0,n)      */

/* ---- Traffic patterns ------------------------------------------------- */
typedef enum {
    TRAFFIC_UNIFORM = 0,
    TRAFFIC_HOTNODE = 1
} traffic_type_t;

typedef struct {
    traffic_type_t type;
    int    num_nodes;
    int    num_hot;                 /* number of hot ("database") nodes   */
    int    hot[MAX_HOT_NODES];      /* their node ids                     */
    double hot_fraction;            /* share of all packets aimed at them */
    uint32_t seed;
    rng_t  rng;
} traffic_t;

/* Hot-node selection is derived from the seed so that it is reproducible
 * and team-specific rather than hard-coded:
 *     hot[0] = seed mod N
 *     hot[1] = (seed div N) mod N   (nudged if it collides with hot[0])
 * For Group 1 (seed 1301, N = 16) this yields hot nodes {5, 1}. */
void traffic_init(traffic_t *t, traffic_type_t type, int num_nodes,
                  uint32_t seed, double hot_fraction, int num_hot);

/* Draw a destination for a packet originating at `src`. Never returns
 * `src`, so every generated packet performs at least one hop. */
int  traffic_dest(traffic_t *t, int src);

/* 1 if `node` is one of the hot database nodes. */
int  traffic_is_hot(const traffic_t *t, int node);

const char *traffic_name(traffic_type_t type);

#endif /* TRAFFIC_H */
