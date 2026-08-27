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

#define MAX_RATES     64
#define DEFAULT_CONFIG "configs/group1_week3_config.txt"

/* Every swept parameter lives here rather than in #defines, so that the
 * assigned configuration stays in configs/ and an unseen node count can be
 * run from the command line without a rebuild. */
typedef struct {
    int      node_count, mesh_rows, mesh_cols;
    unsigned seed;
    int      hot_nodes;
    double   hot_fraction;
    long     warmup, measure, drain_max;
    double   sat_efficiency;
    double   rates[MAX_RATES];
    int      nrates;
    int      do_ring, do_mesh;
    char     outdir[256];
} sweep_cfg_t;

static void cfg_defaults(sweep_cfg_t *c) {
    memset(c, 0, sizeof(*c));
    c->node_count = 16; c->mesh_rows = 4; c->mesh_cols = 4;
    c->seed = 1301u; c->hot_nodes = 2; c->hot_fraction = 0.50;
    c->warmup = 3000L; c->measure = 12000L; c->drain_max = 30000L;
    c->sat_efficiency = 0.95;
    c->do_ring = c->do_mesh = 1;
    snprintf(c->outdir, sizeof(c->outdir), "results");
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int parse_rates(const char *v, double *out, int cap) {
    int n = 0;
    const char *p = v;
    while (*p && n < cap) {
        char *end;
        double d = strtod(p, &end);
        if (end == p) { p++; continue; }
        if (d > 0.0) out[n++] = d;
        p = end;
    }
    return n;
}

/* key = value, '#' comments, trailing '\' continues onto the next line. */
static int cfg_load_file(sweep_cfg_t *c, const char *path, int required) {
    FILE *f = fopen(path, "r");
    if (!f) {
        if (required) fprintf(stderr, "FATAL: cannot open config %s\n", path);
        return required ? 0 : 1;
    }
    char line[1024], acc[4096];
    while (fgets(line, sizeof(line), f)) {
        acc[0] = 0;
        for (;;) {
            char *h = strchr(line, '#');
            if (h) *h = 0;
            char *t = trim(line);
            size_t len = strlen(t);
            int cont = (len > 0 && t[len - 1] == '\\');
            if (cont) t[len - 1] = 0;
            if (strlen(acc) + strlen(t) + 2 < sizeof(acc)) {
                if (acc[0]) strcat(acc, " ");
                strcat(acc, t);
            }
            if (!cont || !fgets(line, sizeof(line), f)) break;
        }
        char *eq = strchr(acc, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(acc), *v = trim(eq + 1);
        if (!*k || !*v) continue;

        if      (!strcmp(k, "node_count"))           c->node_count = atoi(v);
        else if (!strcmp(k, "ring_nodes"))           c->node_count = atoi(v);
        else if (!strcmp(k, "mesh_rows"))            c->mesh_rows = atoi(v);
        else if (!strcmp(k, "mesh_cols"))            c->mesh_cols = atoi(v);
        else if (!strcmp(k, "traffic_seed"))         c->seed = (unsigned)strtoul(v, NULL, 10);
        else if (!strcmp(k, "hot_node_count"))       c->hot_nodes = atoi(v);
        else if (!strcmp(k, "hot_traffic_fraction")) c->hot_fraction = atof(v);
        else if (!strcmp(k, "warmup_cycles"))        c->warmup = atol(v);
        else if (!strcmp(k, "measurement_cycles"))   c->measure = atol(v);
        else if (!strcmp(k, "drain_cap_cycles"))     c->drain_max = atol(v);
        else if (!strcmp(k, "saturation_efficiency")) c->sat_efficiency = atof(v);
        else if (!strcmp(k, "injection_rates"))      c->nrates = parse_rates(v, c->rates, MAX_RATES);
    }
    fclose(f);
    return 1;
}

static void usage(const char *prog) {
    printf("Usage: %s [options]\n"
           "  --config PATH    parameter file (default %s)\n"
           "  --nodes N        ring node count\n"
           "  --rows R         mesh rows\n"
           "  --cols C         mesh columns\n"
           "  --seed S         traffic seed\n"
           "  --topology LIST  comma-separated: ring,mesh (default both)\n"
           "  --rates LIST     injection rates, comma- or space-separated\n"
           "  --outdir DIR     results directory (default results)\n"
           "  --help\n", prog, DEFAULT_CONFIG);
}

static int cfg_parse_args(sweep_cfg_t *c, int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int has_val = (i + 1 < argc);
        const char *v = has_val ? argv[i + 1] : NULL;

        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "--config") && has_val)  { i++; }   /* handled earlier */
        else if (!strcmp(a, "--nodes")  && has_val)  { c->node_count = atoi(v); i++; }
        else if (!strcmp(a, "--rows")   && has_val)  { c->mesh_rows = atoi(v); i++; }
        else if (!strcmp(a, "--cols")   && has_val)  { c->mesh_cols = atoi(v); i++; }
        else if (!strcmp(a, "--seed")   && has_val)  { c->seed = (unsigned)strtoul(v, NULL, 10); i++; }
        else if (!strcmp(a, "--outdir") && has_val)  { snprintf(c->outdir, sizeof(c->outdir), "%s", v); i++; }
        else if (!strcmp(a, "--rates")  && has_val)  { c->nrates = parse_rates(v, c->rates, MAX_RATES); i++; }
        else if (!strcmp(a, "--topology") && has_val) {
            c->do_ring = (strstr(v, "ring") != NULL);
            c->do_mesh = (strstr(v, "mesh") != NULL);
            i++;
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", a);
            usage(argv[0]);
            return -1;
        }
    }
    return 1;
}

typedef struct {
    const char *topo_name;
    topology_type_t topo_type;
    traffic_type_t  traffic;
    vc_mode_t       mode;
} config_t;

int main(int argc, char **argv) {
    sweep_cfg_t cfg;
    cfg_defaults(&cfg);

    const char *cfg_path = DEFAULT_CONFIG;
    int cfg_explicit = 0;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--config")) { cfg_path = argv[i + 1]; cfg_explicit = 1; }
    if (!cfg_load_file(&cfg, cfg_path, cfg_explicit)) return 1;

    int pa = cfg_parse_args(&cfg, argc, argv);
    if (pa <= 0) return pa < 0 ? 1 : 0;

    if (cfg.nrates == 0) { fprintf(stderr, "FATAL: no injection rates\n"); return 1; }
    if (!cfg.do_ring && !cfg.do_mesh) { fprintf(stderr, "FATAL: no topology selected\n"); return 1; }

    topology_t *ring = topology_build_ring(cfg.node_count);
    topology_t *mesh = topology_build_mesh(cfg.mesh_rows, cfg.mesh_cols);
    if (!ring || !mesh) { fprintf(stderr, "FATAL: topology build failed\n"); return 1; }

    int ring_diam = topology_diameter(ring);
    int ring_bis  = topology_bisection_bandwidth(ring);
    int mesh_diam = topology_diameter(mesh);
    int mesh_bis  = topology_bisection_bandwidth(mesh);

    /* Closed-form expectations for the configured size. The gate is kept
     * from Week 2 but is now derived rather than hard-coded to 16 nodes,
     * so it still fires for an unseen node count. */
    int exp_ring_diam = cfg.node_count / 2;
    int exp_ring_bis  = (cfg.node_count < 3) ? cfg.node_count - 1 : 2;
    int exp_mesh_diam = (cfg.mesh_rows - 1) + (cfg.mesh_cols - 1);
    int exp_mesh_bis  = (cfg.mesh_rows < cfg.mesh_cols) ? cfg.mesh_rows : cfg.mesh_cols;

    printf("=== Project 13 / Group 1 -- Injection-Rate Sweep ===\n\n");
    printf("Config file   : %s\n", cfg_path);
    printf("Configuration : ring(%d) and mesh(%dx%d), seed %u\n",
           cfg.node_count, cfg.mesh_rows, cfg.mesh_cols, cfg.seed);
    printf("Packet size   : %d flits   VC buffer: %d flits   Eject BW: %d flits/cycle\n",
           PACKET_FLITS, VC_BUF_FLITS, EJECT_BW);
    printf("Warmup/measure: %ld / %ld cycles (drain cap %ld)\n\n",
           cfg.warmup, cfg.measure, cfg.drain_max);

    printf("-- Static metrics re-verified --\n");
    printf("  ring(%d) : diameter=%d  bisection=%d   (expected %d / %d)\n",
           cfg.node_count, ring_diam, ring_bis, exp_ring_diam, exp_ring_bis);
    printf("  mesh(%dx%d): diameter=%d  bisection=%d   (expected %d / %d)\n\n",
           cfg.mesh_rows, cfg.mesh_cols, mesh_diam, mesh_bis, exp_mesh_diam, exp_mesh_bis);
    if (ring_diam != exp_ring_diam || ring_bis != exp_ring_bis ||
        mesh_diam != exp_mesh_diam || mesh_bis != exp_mesh_bis) {
        fprintf(stderr, "FATAL: static metrics no longer verify -- aborting.\n");
        return 1;
    }

    {   /* Report the seed-derived hot-node set once. */
        traffic_t tmp;
        traffic_init(&tmp, TRAFFIC_HOTNODE, cfg.node_count, cfg.seed, cfg.hot_fraction, cfg.hot_nodes);
        printf("-- Hot-node (database) set derived from seed %u --\n", cfg.seed);
        printf("  hot nodes = {");
        for (int i = 0; i < tmp.num_hot; i++)
            printf("%d%s", tmp.hot[i], i + 1 < tmp.num_hot ? ", " : "");
        printf("}   share of all traffic = %.0f%%\n\n", cfg.hot_fraction * 100.0);
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

    char csv_path[320], sum_path[320];
    snprintf(csv_path, sizeof(csv_path), "%s/week3_sweep_results.csv", cfg.outdir);
    snprintf(sum_path, sizeof(sum_path), "%s/week3_saturation_summary.csv", cfg.outdir);
    FILE *csv = fopen(csv_path, "w");
    FILE *sum = fopen(sum_path, "w");
    if (!csv || !sum) { fprintf(stderr, "FATAL: cannot open %s for writing\n", cfg.outdir); return 1; }

    fprintf(csv, "topology,traffic,vc_mode,vcs,queues,diameter,bisection,offered_rate,"
                 "accepted_throughput,avg_latency,max_latency,avg_hops,"
                 "bg_latency,hot_latency,bg_throughput,hot_throughput,"
                 "m_generated,m_delivered,unretired,dropped_full,peak_queue,"
                 "conservation_ok,saturated\n");
    fprintf(sum, "topology,traffic,vc_mode,vcs,diameter,bisection,"
                 "zero_load_latency,saturation_rate,first_saturated_rate,"
                 "peak_accepted_throughput\n");

    int conservation_failures = 0;
    int runs = 0;

    for (int c = 0; c < ncfg; c++) {
        if (cfgs[c].topo_type == TOPO_RING && !cfg.do_ring) continue;
        if (cfgs[c].topo_type == TOPO_MESH && !cfg.do_mesh) continue;

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

        for (int r = 0; r < cfg.nrates; r++) {
            sim_t *s = sim_create(t, cfgs[c].traffic, cfg.seed, cfg.hot_fraction,
                                  cfg.hot_nodes, cfgs[c].mode);
            sim_run(s, cfg.rates[r], cfg.warmup, cfg.measure, cfg.drain_max);

            runs++;
            int ok = sim_check_conservation(s);
            if (!ok) conservation_failures++;

            double thr = sim_accepted_throughput(s);
            double lat = sim_avg_latency(s);
            double hop = sim_avg_hops(s);
            long unret = s->st.m_generated - s->st.m_dropped - s->st.m_delivered;

            int sat = (thr < cfg.sat_efficiency * cfg.rates[r]) || (s->st.dropped_full > 0);
            if (!sat && !saturated_seen) sat_rate = cfg.rates[r];
            if (sat && !saturated_seen) { saturated_seen = 1; first_sat = cfg.rates[r]; }
            if (thr > peak_thr) peak_thr = thr;
            if (zero_load < 0.0) zero_load = lat;

            double bg_lat  = sim_avg_latency_class(s, 0);
            double hot_lat  = sim_avg_latency_class(s, 1);
            double bg_thr   = sim_throughput_class(s, 0);
            double hot_thr  = sim_throughput_class(s, 1);

            printf("   %-8.2f %-11.4f %-10.1f %-11.1f %-11.1f %-8.2f %-8ld %s\n",
                   cfg.rates[r], thr, lat, bg_lat, hot_lat, hop,
                   s->st.dropped_full, sat ? "SATURATED" : "stable");

            fprintf(csv, "%s,%s,%s,%d,%d,%d,%d,%.4f,%.6f,%.4f,%ld,%.4f,"
                         "%.4f,%.4f,%.6f,%.6f,"
                         "%ld,%ld,%ld,%ld,%ld,%d,%d\n",
                    cfgs[c].topo_name, traffic_name(cfgs[c].traffic),
                    vc_mode_name(cfgs[c].mode), s->vcs, s->nqueues, diam, bis,
                    cfg.rates[r], thr, lat, s->st.m_latency_max, hop,
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
           conservation_failures, runs);
    printf("Results written to %s and %s\n", csv_path, sum_path);

    topology_free(ring);
    topology_free(mesh);
    return conservation_failures == 0 ? 0 : 1;
}
