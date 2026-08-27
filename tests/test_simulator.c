/* =========================================================================
 * test_simulator.c
 *
 * Project 13 / Group 1 -- WEEK 3 UNIT TESTS
 *
 * Week 2 tested topology construction, static metrics and routing. This
 * suite tests everything Week 3 added: the traffic generators, the
 * flit-level simulation core, the packet-loss accounting demanded by
 * Project 13 Section M, and the class-isolation innovation.
 *
 * The tests deliberately include the project's own central hypothesis
 * (the mesh must sustain a higher injection rate than the ring) as an
 * assertion rather than as a claim made only in prose.
 *
 * Build (from repository root):
 *   gcc -O2 -Wall -Wextra -std=c11 -Istudent_implementation \
 *       -o test_simulator student_implementation/network_topology.c \
 *                         student_implementation/traffic.c \
 *                         student_implementation/network_sim.c \
 *                         tests/test_simulator.c -lm
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "network_sim.h"

#define SEED         1301u
#define N            16
#define HOT_FRACTION 0.50
#define HOT_NODES    2

static int tests_run = 0;
#define CHECK(cond) do {                                             \
    tests_run++;                                                     \
    if (!(cond)) {                                                   \
        fprintf(stderr, "FAILED: %s (line %d)\n", #cond, __LINE__);  \
        return 1;                                                    \
    }                                                                \
} while (0)

/* Short runs keep the suite fast; the long-run behaviour is exercised by
 * tests/run_sweep.c. */
#define W  1000L
#define M  4000L
#define D 20000L

static sim_t *run_case(topology_t *t, traffic_type_t tr, vc_mode_t mode, double rate) {
    sim_t *s = sim_create(t, tr, SEED, HOT_FRACTION, HOT_NODES, mode);
    sim_run(s, rate, W, M, D);
    return s;
}

/* ---- 1. Traffic generator ------------------------------------------- */

static int test_traffic_generator(void) {
    traffic_t u, h;
    traffic_init(&u, TRAFFIC_UNIFORM, N, SEED, HOT_FRACTION, HOT_NODES);
    traffic_init(&h, TRAFFIC_HOTNODE, N, SEED, HOT_FRACTION, HOT_NODES);

    /* The hot set is derived from the seed, not hard-coded. */
    CHECK(h.num_hot == 2);
    CHECK(h.hot[0] == (int)(SEED % N));            /* 1301 mod 16 = 5      */
    CHECK(h.hot[1] == (int)((SEED / N) % N));      /*   81 mod 16 = 1      */
    CHECK(h.hot[0] != h.hot[1]);
    CHECK(traffic_is_hot(&h, h.hot[0]) && traffic_is_hot(&h, h.hot[1]));
    CHECK(!traffic_is_hot(&u, h.hot[0]));          /* uniform has no hot set */

    /* A packet never addresses its own source node. */
    int counts[N];
    memset(counts, 0, sizeof(counts));
    const int TRIALS = 200000;
    int self_addressed = 0, out_of_range = 0;
    for (int i = 0; i < TRIALS; i++) {
        int src = i % N;
        int d = traffic_dest(&u, src);
        if (d < 0 || d >= N) out_of_range++;
        if (d == src)        self_addressed++;
        counts[d]++;
    }
    CHECK(out_of_range == 0);
    CHECK(self_addressed == 0);
    /* Uniform pattern: every node should receive within +/-8% of 1/N of
     * the traffic. A generator that quietly concentrates load would
     * defeat the mesh's bisection advantage (Project 13 Instructor Notes). */
    double expect = (double)TRIALS / (double)N;
    for (int i = 0; i < N; i++) {
        CHECK(fabs(counts[i] - expect) / expect < 0.08);
    }

    /* Hot-node pattern: the two database nodes must attract close to
     * HOT_FRACTION of all packets, plus their uniform share. */
    memset(counts, 0, sizeof(counts));
    for (int i = 0; i < TRIALS; i++) counts[traffic_dest(&h, i % N)]++;
    double hot_share = (double)(counts[h.hot[0]] + counts[h.hot[1]]) / (double)TRIALS;
    /* Expected share is HOT_FRACTION, plus the hot nodes' share of the
     * remaining uniform component, minus a small correction because a hot
     * node that draws itself falls through to the uniform component
     * instead of self-addressing. That puts the true value near 0.535. */
    CHECK(hot_share > 0.50 && hot_share < 0.60);
    /* ... and each hot node must far exceed a non-hot node. */
    CHECK(counts[h.hot[0]] > 3 * counts[(h.hot[0] + 3) % N]);
    return 0;
}

/* ---- 2. Reproducibility ---------------------------------------------- */

static int test_reproducibility(void) {
    topology_t *m = topology_build_mesh(4, 4);
    sim_t *a = run_case(m, TRAFFIC_HOTNODE, VC_MODE_BASELINE, 0.20);
    sim_t *b = run_case(m, TRAFFIC_HOTNODE, VC_MODE_BASELINE, 0.20);

    CHECK(a->st.generated == b->st.generated);
    CHECK(a->st.delivered == b->st.delivered);
    CHECK(a->st.m_latency_sum == b->st.m_latency_sum);
    CHECK(a->st.dropped_full == b->st.dropped_full);

    sim_free(a); sim_free(b); topology_free(m);
    return 0;
}

/* ---- 3. Packet conservation and explicit loss accounting ------------- */

static int test_packet_conservation(void) {
    topology_t *r = topology_build_ring(N);
    topology_t *m = topology_build_mesh(4, 4);
    double rates[] = { 0.05, 0.20, 0.40, 0.80 };

    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 2; k++) {
            topology_t *t = k ? m : r;
            traffic_type_t tr = k ? TRAFFIC_HOTNODE : TRAFFIC_UNIFORM;
            sim_t *s = run_case(t, tr, VC_MODE_BASELINE, rates[i]);

            /* generated == delivered + dropped + in flight + queued */
            CHECK(sim_check_conservation(s));
            /* Nothing may be lost anywhere except an explicitly counted
             * full source queue. */
            CHECK(s->st.delivered >= 0 && s->st.in_network >= 0);
            CHECK(s->st.generated > 0);
            sim_free(s);
        }
    }
    topology_free(r); topology_free(m);
    return 0;
}

static int test_zero_loss_below_saturation(void) {
    topology_t *r = topology_build_ring(N);
    topology_t *m = topology_build_mesh(4, 4);

    /* Well below either topology's saturation point there must be exactly
     * zero packet loss -- the "Excellent" bar in the Project 13 rubric. */
    sim_t *a = run_case(r, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.10);
    sim_t *b = run_case(m, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.10);
    sim_t *c = run_case(r, TRAFFIC_HOTNODE, VC_MODE_BASELINE, 0.08);
    sim_t *d = run_case(m, TRAFFIC_HOTNODE, VC_MODE_BASELINE, 0.08);

    CHECK(a->st.dropped_full == 0);
    CHECK(b->st.dropped_full == 0);
    CHECK(c->st.dropped_full == 0);
    CHECK(d->st.dropped_full == 0);
    /* Every measurement packet must also actually retire. */
    CHECK(a->st.m_delivered == a->st.m_generated);
    CHECK(b->st.m_delivered == b->st.m_generated);
    CHECK(c->st.m_delivered == c->st.m_generated);
    CHECK(d->st.m_delivered == d->st.m_generated);

    sim_free(a); sim_free(b); sim_free(c); sim_free(d);
    topology_free(r); topology_free(m);
    return 0;
}

/* ---- 4. Zero-load latency agrees with the Week 2 hop counts ---------- */

static int test_zero_load_latency(void) {
    topology_t *r = topology_build_ring(N);
    topology_t *m = topology_build_mesh(4, 4);

    sim_t *a = run_case(r, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.02);
    sim_t *b = run_case(m, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.02);

    /* Mean hop count must match the analytical averages: N/4 = 4.0 on a
     * 16-node ring and 2(k+1)/3 = 2.67 on a 4x4 mesh. */
    CHECK(fabs(sim_avg_hops(a) - 4.00) < 0.40);
    CHECK(fabs(sim_avg_hops(b) - 2.67) < 0.40);

    /* Latency can never be below serialisation + one cycle per hop. */
    CHECK(sim_avg_latency(a) >= sim_avg_hops(a) + PACKET_FLITS - 1);
    CHECK(sim_avg_latency(b) >= sim_avg_hops(b) + PACKET_FLITS - 1);

    /* Fewer hops must translate into lower unloaded latency. */
    CHECK(sim_avg_latency(b) < sim_avg_latency(a));

    sim_free(a); sim_free(b); topology_free(r); topology_free(m);
    return 0;
}

/* ---- 5. Accepted throughput tracks offered load below saturation ----- */

static int test_throughput_tracks_offered(void) {
    topology_t *m = topology_build_mesh(4, 4);
    double rates[] = { 0.05, 0.10, 0.20, 0.30 };
    for (int i = 0; i < 4; i++) {
        sim_t *s = run_case(m, TRAFFIC_UNIFORM, VC_MODE_BASELINE, rates[i]);
        double thr = sim_accepted_throughput(s);
        CHECK(fabs(thr - rates[i]) / rates[i] < 0.05);
        sim_free(s);
    }
    topology_free(m);
    return 0;
}

/* ---- 6. Latency rises monotonically with offered load ---------------- */

static int test_latency_monotonic(void) {
    topology_t *m = topology_build_mesh(4, 4);
    double rates[] = { 0.05, 0.20, 0.40, 0.60 };
    double prev = 0.0;
    for (int i = 0; i < 4; i++) {
        sim_t *s = run_case(m, TRAFFIC_UNIFORM, VC_MODE_BASELINE, rates[i]);
        double lat = sim_avg_latency(s);
        CHECK(lat >= prev);
        prev = lat;
        sim_free(s);
    }
    topology_free(m);
    return 0;
}

/* ---- 7. Ring deadlock freedom (dateline rule) ------------------------ */

static int test_ring_makes_progress_at_overload(void) {
    topology_t *r = topology_build_ring(N);
    /* A bidirectional ring with shortest-direction routing has a cyclic
     * channel-dependency graph. If the dateline VC rule were removed the
     * network would deadlock at overload and accepted throughput would
     * collapse to zero. It must instead plateau at a positive value. */
    sim_t *s = run_case(r, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.80);
    CHECK(sim_accepted_throughput(s) > 0.15);
    CHECK(s->st.delivered > 0);
    CHECK(sim_check_conservation(s));
    sim_free(s);
    topology_free(r);
    return 0;
}

/* ---- 8. The project's central hypothesis ----------------------------- */

static int test_mesh_outperforms_ring(void) {
    topology_t *r = topology_build_ring(N);
    topology_t *m = topology_build_mesh(4, 4);

    /* Under uniform-random traffic at a load the ring cannot sustain, the
     * mesh -- with the larger bisection bandwidth (4 vs 2) and smaller
     * diameter (6 vs 8) -- must still be running comfortably. */
    sim_t *a = run_case(r, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.40);
    sim_t *b = run_case(m, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.40);
    CHECK(sim_accepted_throughput(b) > sim_accepted_throughput(a));
    CHECK(sim_avg_latency(b) < sim_avg_latency(a));
    CHECK(fabs(sim_accepted_throughput(b) - 0.40) / 0.40 < 0.05); /* mesh stable */
    CHECK(sim_accepted_throughput(a) < 0.95 * 0.40);              /* ring saturated */
    sim_free(a); sim_free(b);

    /* Hot-node traffic must hurt BOTH topologies relative to uniform. */
    sim_t *c = run_case(r, TRAFFIC_HOTNODE, VC_MODE_BASELINE, 0.24);
    sim_t *d = run_case(r, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.24);
    CHECK(sim_avg_latency(c) > sim_avg_latency(d));
    sim_free(c); sim_free(d);

    sim_t *e = run_case(m, TRAFFIC_HOTNODE, VC_MODE_BASELINE, 0.24);
    sim_t *f = run_case(m, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.24);
    CHECK(sim_avg_latency(e) > sim_avg_latency(f));
    sim_free(e); sim_free(f);

    topology_free(r); topology_free(m);
    return 0;
}

/* ---- 9. VC mode configuration ---------------------------------------- */

static int test_vc_mode_configuration(void) {
    CHECK(vc_mode_vcs(VC_MODE_BASELINE) == 2 && vc_mode_queues(VC_MODE_BASELINE) == 1);
    CHECK(vc_mode_vcs(VC_MODE_MORE)     == 4 && vc_mode_queues(VC_MODE_MORE)     == 1);
    CHECK(vc_mode_vcs(VC_MODE_QCLASS)   == 2 && vc_mode_queues(VC_MODE_QCLASS)   == 2);
    CHECK(vc_mode_vcs(VC_MODE_CLASS)    == 4 && vc_mode_queues(VC_MODE_CLASS)    == 2);

    /* Total source-queue buffering is held constant across modes so the
     * innovation cannot win simply by being given more memory. */
    topology_t *m = topology_build_mesh(4, 4);
    sim_t *a = sim_create(m, TRAFFIC_HOTNODE, SEED, HOT_FRACTION, HOT_NODES, VC_MODE_BASELINE);
    sim_t *b = sim_create(m, TRAFFIC_HOTNODE, SEED, HOT_FRACTION, HOT_NODES, VC_MODE_CLASS);
    CHECK(a->q_cap * a->nqueues == b->q_cap * b->nqueues);
    sim_free(a); sim_free(b); topology_free(m);
    return 0;
}

/* ---- 10. The innovation ---------------------------------------------- */

static int test_class_isolation_removes_hol_blocking(void) {
    topology_t *m = topology_build_mesh(4, 4);
    topology_t *r = topology_build_ring(N);
    const double LOAD = 0.24;   /* past the hot-node saturation knee */

    sim_t *base_m = run_case(m, TRAFFIC_HOTNODE, VC_MODE_BASELINE, LOAD);
    sim_t *cls_m  = run_case(m, TRAFFIC_HOTNODE, VC_MODE_CLASS,    LOAD);
    sim_t *base_r = run_case(r, TRAFFIC_HOTNODE, VC_MODE_BASELINE, LOAD);
    sim_t *cls_r  = run_case(r, TRAFFIC_HOTNODE, VC_MODE_CLASS,    LOAD);

    /* Background traffic (packets NOT addressed to a database node) must
     * be dramatically faster once it is isolated from the congested hot
     * flow -- that is exactly the head-of-line blocking being removed. */
    CHECK(sim_avg_latency_class(cls_m, 0) < 0.2 * sim_avg_latency_class(base_m, 0));
    CHECK(sim_avg_latency_class(cls_r, 0) < 0.2 * sim_avg_latency_class(base_r, 0));

    /* The hot flow itself is bounded by the hot nodes' own link bandwidth,
     * so isolation must NOT be claimed to speed it up; it must simply not
     * make it worse in throughput terms. */
    CHECK(sim_throughput_class(cls_m, 1) >= 0.9 * sim_throughput_class(base_m, 1));

    /* Total accepted throughput must not regress. */
    CHECK(sim_accepted_throughput(cls_m) >= sim_accepted_throughput(base_m) * 0.98);
    CHECK(sim_check_conservation(cls_m) && sim_check_conservation(cls_r));

    sim_free(base_m); sim_free(cls_m); sim_free(base_r); sim_free(cls_r);
    topology_free(m); topology_free(r);
    return 0;
}

/* ---- 11. Isolation is a no-op when there is only one traffic class --- */

static int test_class_isolation_neutral_under_uniform(void) {
    topology_t *m = topology_build_mesh(4, 4);
    sim_t *a = run_case(m, TRAFFIC_UNIFORM, VC_MODE_BASELINE, 0.30);
    sim_t *b = run_case(m, TRAFFIC_UNIFORM, VC_MODE_CLASS,    0.30);

    /* Under uniform traffic every packet is background class, so the
     * partition can neither help nor hurt appreciably. */
    CHECK(fabs(sim_avg_latency(a) - sim_avg_latency(b)) < 0.15 * sim_avg_latency(a));
    CHECK(sim_throughput_class(a, 1) == 0.0);   /* no hot class exists */
    CHECK(sim_throughput_class(b, 1) == 0.0);

    sim_free(a); sim_free(b); topology_free(m);
    return 0;
}

/* ---- Main ------------------------------------------------------------ */

int main(void) {
    int failures = 0;
    failures += test_traffic_generator();
    failures += test_reproducibility();
    failures += test_packet_conservation();
    failures += test_zero_loss_below_saturation();
    failures += test_zero_load_latency();
    failures += test_throughput_tracks_offered();
    failures += test_latency_monotonic();
    failures += test_ring_makes_progress_at_overload();
    failures += test_mesh_outperforms_ring();
    failures += test_vc_mode_configuration();
    failures += test_class_isolation_removes_hol_blocking();
    failures += test_class_isolation_neutral_under_uniform();

    if (failures == 0) {
        printf("ALL TESTS PASSED (%d checks across 12 test functions)\n", tests_run);
        return 0;
    }
    printf("%d test function(s) FAILED\n", failures);
    return 1;
}
