/* =========================================================================
 * run_sweep.c
 *
 * Project 13 / Group 1 -- WEEK 3 EXPERIMENT DRIVER
 *
 * Runs the full injection-rate sweep required by the Week 3 schedule:
 *
 *   topologies      : 16-node ring, 4 x 4 mesh
 *   traffic         : uniform-random, hot-node-skewed  (seed 1301)
 *   VC configuration: baseline_vc2 (2 VCs, 1 shared source queue)
 *                     vc4_plain    (4 VCs, 1 shared queue -- buffer control)
 *                     vc2_qclass   (2 VCs, 2 class queues -- queue control)
 *                     vc4_class    (4 VCs, 2 class queues -- INNOVATION)
 *   injection rate  : swept from 0.02 to 0.80 flits/node/cycle
 *
 * For every point it records offered load, accepted throughput, average
 * and maximum packet latency, average hop count, and a full packet
 * census (generated / delivered / still in flight / dropped), and it
 * asserts the conservation identity
 *     generated == delivered + dropped + in_network + queued.
 *
 * Output: results/week3_sweep_results.csv
 *         results/week3_saturation_summary.csv
 *
 * Build (from repository root):
 *   gcc -O2 -Wall -Wextra -std=c11 -Istudent_implementation \
 *       -o run_sweep student_implementation/network_topology.c \
 *                    student_implementation/traffic.c \
 *                    student_implementation/network_sim.c \
 *                    tests/run_sweep.c -lm
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_sim.h"

#define SEED          1301u
#define NODE_COUNT    16
#define MESH_ROWS     4
#define MESH_COLS     4
#define HOT_NODES     2
#define HOT_FRACTION  0.50

#define WARMUP        3000L
#define MEASURE       12000L
#define DRAIN_MAX     30000L

/* Saturation criterion: the network is considered saturated at the first
 * offered rate whose accepted throughput falls below SAT_EFFICIENCY of the
 * offered rate, or at which any packet is dropped from a source queue. */
#define SAT_EFFICIENCY 0.95

static const double RATES[] = {
    0.02, 0.05, 0.08, 0.11, 0.14, 0.17, 0.20, 0.24, 0.28,
    0.32, 0.36, 0.40, 0.45, 0.50, 0.55, 0.60, 0.70, 0.80
};
#define NRATES ((int)(sizeof(RATES) / sizeof(RATES[0])))

typedef struct {
    const char *topo_name;
    topology_type_t topo_type;
    traffic_type_t  traffic;
    vc_mode_t       mode;
} config_t;

int main(void) {
    topology_t *ring = topology_build_ring(NODE_COUNT);
    topology_t *mesh = topology_build_mesh(MESH_ROWS, MESH_COLS);
    if (!ring || !mesh) { fprintf(stderr, "FATAL: topology build failed\n"); return 1; }

    /* Week 2 static metrics are re-verified here so that Week 3 results
     * are never reported on top of an unverified topology. */
    int ring_diam = topology_diameter(ring);
    int ring_bis  = topology_bisection_bandwidth(ring);
    int mesh_diam = topology_diameter(mesh);
    int mesh_bis  = topology_bisection_bandwidth(mesh);

    printf("=== Project 13 / Group 1 -- Week 3 Injection-Rate Sweep ===\n\n");
    printf("Configuration : %d nodes, ring(%d) and mesh(%dx%d), seed %u\n",
           NODE_COUNT, NODE_COUNT, MESH_ROWS, MESH_COLS, SEED);
    printf("Packet size   : %d flits   VC buffer: %d flits   Eject BW: %d flits/cycle\n",
           PACKET_FLITS, VC_BUF_FLITS, EJECT_BW);
    printf("Warmup/measure: %ld / %ld cycles (drain cap %ld)\n\n",
           WARMUP, MEASURE, DRAIN_MAX);

    printf("-- Week 2 static metrics re-verified --\n");
    printf("  ring(16) : diameter=%d  bisection=%d   (expected 8 / 2)\n", ring_diam, ring_bis);
    printf("  mesh(4x4): diameter=%d  bisection=%d   (expected 6 / 4)\n\n", mesh_diam, mesh_bis);
    if (ring_diam != 8 || ring_bis != 2 || mesh_diam != 6 || mesh_bis != 4) {
        fprintf(stderr, "FATAL: Week 2 metrics no longer verify -- aborting.\n");
        return 1;
    }

    {   /* Report the seed-derived hot-node set once. */
        traffic_t tmp;
        traffic_init(&tmp, TRAFFIC_HOTNODE, NODE_COUNT, SEED, HOT_FRACTION, HOT_NODES);
        printf("-- Hot-node (database) set derived from seed %u --\n", SEED);
        printf("  hot nodes = {");
        for (int i = 0; i < tmp.num_hot; i++)
            printf("%d%s", tmp.hot[i], i + 1 < tmp.num_hot ? ", " : "");
        printf("}   share of all traffic = %.0f%%\n\n", HOT_FRACTION * 100.0);
    }

    config_t cfgs[] = {
        /* Part A -- the required ring-vs-mesh comparison (baseline flow control) */
        { "ring", TOPO_RING, TRAFFIC_UNIFORM, VC_MODE_BASELINE },
        { "mesh", TOPO_MESH, TRAFFIC_UNIFORM, VC_MODE_BASELINE },
        { "ring", TOPO_RING, TRAFFIC_HOTNODE, VC_MODE_BASELINE },
        { "mesh", TOPO_MESH, TRAFFIC_HOTNODE, VC_MODE_BASELINE },
        /* Part B -- innovation and its two controls, under hot-node traffic */
        { "ring", TOPO_RING, TRAFFIC_HOTNODE, VC_MODE_MORE     },
        { "mesh", TOPO_MESH, TRAFFIC_HOTNODE, VC_MODE_MORE     },
        { "ring", TOPO_RING, TRAFFIC_HOTNODE, VC_MODE_QCLASS   },
        { "mesh", TOPO_MESH, TRAFFIC_HOTNODE, VC_MODE_QCLASS   },
        { "ring", TOPO_RING, TRAFFIC_HOTNODE, VC_MODE_CLASS    },
        { "mesh", TOPO_MESH, TRAFFIC_HOTNODE, VC_MODE_CLASS    },
        /* Part C -- innovation under uniform traffic (must be a no-op) */
        { "ring", TOPO_RING, TRAFFIC_UNIFORM, VC_MODE_CLASS    },
        { "mesh", TOPO_MESH, TRAFFIC_UNIFORM, VC_MODE_CLASS    }
    };
    int ncfg = (int)(sizeof(cfgs) / sizeof(cfgs[0]));

    FILE *csv = fopen("results/week3_sweep_results.csv", "w");
    FILE *sum = fopen("results/week3_saturation_summary.csv", "w");
    if (!csv || !sum) { fprintf(stderr, "FATAL: cannot open results/ for writing\n"); return 1; }

    fprintf(csv, "topology,traffic,vc_mode,vcs,queues,diameter,bisection,offered_rate,"
                 "accepted_throughput,avg_latency,max_latency,avg_hops,"
                 "bg_latency,hot_latency,bg_throughput,hot_throughput,"
                 "m_generated,m_delivered,unretired,dropped_full,peak_queue,"
                 "conservation_ok,saturated\n");
    fprintf(sum, "topology,traffic,vc_mode,vcs,diameter,bisection,"
                 "zero_load_latency,saturation_rate,first_saturated_rate,"
                 "peak_accepted_throughput\n");

    int conservation_failures = 0;

    for (int c = 0; c < ncfg; c++) {
        const topology_t *t = (cfgs[c].topo_type == TOPO_RING) ? ring : mesh;
        int diam = (cfgs[c].topo_type == TOPO_RING) ? ring_diam : mesh_diam;
        int bis  = (cfgs[c].topo_type == TOPO_RING) ? ring_bis  : mesh_bis;

        printf("-- %s / %s / %s --\n", cfgs[c].topo_name,
               traffic_name(cfgs[c].traffic), vc_mode_name(cfgs[c].mode));
        printf("   %-8s %-11s %-10s %-11s %-11s %-8s %-8s %s\n",
               "offered", "accepted", "avg_lat", "bg_lat", "hot_lat",
               "avg_hop", "dropped", "state");

        double zero_load = -1.0, sat_rate = 0.0, first_sat = -1.0, peak_thr = 0.0;
        int saturated_seen = 0;

        for (int r = 0; r < NRATES; r++) {
            sim_t *s = sim_create(t, cfgs[c].traffic, SEED, HOT_FRACTION,
                                  HOT_NODES, cfgs[c].mode);
            sim_run(s, RATES[r], WARMUP, MEASURE, DRAIN_MAX);

            int ok = sim_check_conservation(s);
            if (!ok) conservation_failures++;

            double thr = sim_accepted_throughput(s);
            double lat = sim_avg_latency(s);
            double hop = sim_avg_hops(s);
            long unret = s->st.m_generated - s->st.m_dropped - s->st.m_delivered;

            int sat = (thr < SAT_EFFICIENCY * RATES[r]) || (s->st.dropped_full > 0);
            if (!sat && !saturated_seen) sat_rate = RATES[r];
            if (sat && !saturated_seen) { saturated_seen = 1; first_sat = RATES[r]; }
            if (thr > peak_thr) peak_thr = thr;
            if (zero_load < 0.0) zero_load = lat;

            double bg_lat  = sim_avg_latency_class(s, 0);
            double hot_lat  = sim_avg_latency_class(s, 1);
            double bg_thr   = sim_throughput_class(s, 0);
            double hot_thr  = sim_throughput_class(s, 1);

            printf("   %-8.2f %-11.4f %-10.1f %-11.1f %-11.1f %-8.2f %-8ld %s\n",
                   RATES[r], thr, lat, bg_lat, hot_lat, hop,
                   s->st.dropped_full, sat ? "SATURATED" : "stable");

            fprintf(csv, "%s,%s,%s,%d,%d,%d,%d,%.4f,%.6f,%.4f,%ld,%.4f,"
                         "%.4f,%.4f,%.6f,%.6f,"
                         "%ld,%ld,%ld,%ld,%ld,%d,%d\n",
                    cfgs[c].topo_name, traffic_name(cfgs[c].traffic),
                    vc_mode_name(cfgs[c].mode), s->vcs, s->nqueues, diam, bis,
                    RATES[r], thr, lat, s->st.m_latency_max, hop,
                    bg_lat, hot_lat, bg_thr, hot_thr,
                    s->st.m_generated, s->st.m_delivered, unret,
                    s->st.dropped_full, s->st.peak_queue, ok, sat);

            sim_free(s);
        }

        fprintf(sum, "%s,%s,%s,%d,%d,%d,%.4f,%.4f,%.4f,%.6f\n",
                cfgs[c].topo_name, traffic_name(cfgs[c].traffic),
                vc_mode_name(cfgs[c].mode), vc_mode_vcs(cfgs[c].mode),
                diam, bis, zero_load, sat_rate, first_sat, peak_thr);

        printf("   => zero-load latency %.2f cycles; highest loss-free rate %.2f; "
               "first saturated rate %.2f; peak accepted %.4f flits/node/cycle\n\n",
               zero_load, sat_rate, first_sat, peak_thr);
    }

    fclose(csv);
    fclose(sum);

    printf("Packet-conservation check: %s (%d violation(s) across %d runs)\n",
           conservation_failures == 0 ? "PASSED" : "FAILED",
           conservation_failures, ncfg * NRATES);
    printf("Results written to results/week3_sweep_results.csv and "
           "results/week3_saturation_summary.csv\n");

    topology_free(ring);
    topology_free(mesh);
    return conservation_failures == 0 ? 0 : 1;
}
