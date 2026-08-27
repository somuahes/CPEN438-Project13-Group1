/* =========================================================================
 * traffic.c -- see traffic.h for the module contract.
 * ========================================================================= */

#include "traffic.h"

/* ------------------------------------------------------------------- */
/* Deterministic xorshift64* generator (Marsaglia), seeded through      */
/* SplitMix64 so that a small seed such as 1301 still produces a        */
/* well-mixed initial state.                                            */
/*                                                                      */
/* An LCG was tried first but its low-order bits biased the Bernoulli   */
/* injection test at the small probabilities used here (p = rate/4),    */
/* which showed up as accepted throughput sitting a few percent above   */
/* the offered rate at light load. xorshift64* removes that bias while  */
/* remaining trivially reproducible in Python (see                      */
/* python/gen_datacenter_traffic.py, which implements the same three    */
/* shifts and multiply under a 64-bit mask).                            */
/* ------------------------------------------------------------------- */

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void rng_seed(rng_t *r, uint32_t seed) {
    uint64_t x = (uint64_t)seed;
    r->state = splitmix64(&x);
    if (r->state == 0) r->state = 0x9E3779B97F4A7C15ULL;
}

uint32_t rng_next_u32(rng_t *r) {
    uint64_t x = r->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->state = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

double rng_next_double(rng_t *r) {
    /* 24 significant bits, exactly representable in a double. */
    return (double)(rng_next_u32(r) >> 8) / 16777216.0;
}

int rng_next_int(rng_t *r, int n) {
    if (n <= 1) return 0;
    return (int)(rng_next_double(r) * (double)n);
}

/* ------------------------------------------------------------------- */
/* Traffic patterns                                                    */
/* ------------------------------------------------------------------- */

void traffic_init(traffic_t *t, traffic_type_t type, int num_nodes,
                  uint32_t seed, double hot_fraction, int num_hot) {
    t->type         = type;
    t->num_nodes    = num_nodes;
    t->seed         = seed;
    t->hot_fraction = hot_fraction;
    t->num_hot      = (num_hot > MAX_HOT_NODES) ? MAX_HOT_NODES : num_hot;

    /* Seed-derived hot-node selection (documented in traffic.h). */
    uint32_t s = seed;
    for (int i = 0; i < t->num_hot; i++) {
        int cand = (int)(s % (uint32_t)num_nodes);
        /* Resolve collisions deterministically by walking forward. */
        int clash = 1;
        while (clash) {
            clash = 0;
            for (int j = 0; j < i; j++) {
                if (t->hot[j] == cand) { cand = (cand + 1) % num_nodes; clash = 1; break; }
            }
        }
        t->hot[i] = cand;
        s /= (uint32_t)num_nodes;
    }

    rng_seed(&t->rng, seed);
}

int traffic_is_hot(const traffic_t *t, int node) {
    if (t->type != TRAFFIC_HOTNODE) return 0;
    for (int i = 0; i < t->num_hot; i++) {
        if (t->hot[i] == node) return 1;
    }
    return 0;
}

int traffic_dest(traffic_t *t, int src) {
    int n = t->num_nodes;

    if (t->type == TRAFFIC_HOTNODE) {
        double u = rng_next_double(&t->rng);
        if (u < t->hot_fraction) {
            int pick = t->hot[rng_next_int(&t->rng, t->num_hot)];
            if (pick != src) return pick;
            /* A hot node talking to itself falls through to the uniform
             * component rather than being discarded, so the offered load
             * per node stays exactly at the requested injection rate. */
        }
    }

    /* Uniform over all nodes except the source: draw in [0, n-1) and
     * shift past src. This is exactly uniform and never rejects. */
    int d = rng_next_int(&t->rng, n - 1);
    if (d >= src) d++;
    return d;
}

const char *traffic_name(traffic_type_t type) {
    return (type == TRAFFIC_HOTNODE) ? "hotnode" : "uniform";
}
