/* =========================================================================
 * Week 2 verification driver for Project 13.
 *
 * Verifies the simulator's own topology-construction code against the
 * closed-form diameter and bisection-bandwidth values for the configured
 * size. Project 13 Section G makes this a hard gate: the traffic
 * experiments are not trustworthy until it passes.
 *
 * The configuration is read from configs/ rather than hard-coded, so the
 * same driver verifies an unseen node count at the defence.
 *
 * Usage:
 *   ./verify_topology [--config PATH] [--nodes N] [--rows R] [--cols C]
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_topology.h"
#include "sim_config.h"

static int failures = 0;

static void check(const char *what, int got, int expected) {
    int ok = (got == expected);
    if (!ok) failures++;
    printf("  %-34s %-4d (expected %-4d)  %s\n",
           what, got, expected, ok ? "OK" : "MISMATCH");
}

static void demo_route(topology_t *t, const char *label, int src, int dst) {
    int max_len = t->num_nodes + 2;
    int *path = (int *)malloc((size_t)max_len * sizeof(int));
    int hops = topology_route(t, src, dst, path, max_len);
    printf("  %s route %d -> %d (%d hops): ", label, src, dst, hops);
    for (int i = 0; i <= hops; i++) printf("%d%s", path[i], i < hops ? " -> " : "\n");
    free(path);
}

static void usage(const char *prog) {
    printf("Usage: %s [options]\n"
           "  --config PATH  parameter file (default %s)\n"
           "  --nodes N      ring node count\n"
           "  --rows R       mesh rows\n"
           "  --cols C       mesh columns\n"
           "  --help\n", prog, SIM_CONFIG_DEFAULT);
}

int main(int argc, char **argv) {
    sim_config_t cfg;
    sim_config_defaults(&cfg);

    const char *cfg_path = SIM_CONFIG_DEFAULT;
    int cfg_explicit = 0;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--config")) { cfg_path = argv[i + 1]; cfg_explicit = 1; }
    if (!sim_config_load(&cfg, cfg_path, cfg_explicit)) return 1;

    /* Unknown or valueless options are rejected rather than skipped: a
     * mistyped node count must not report a confident pass on the
     * default configuration. */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int has_val = (i + 1 < argc);
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "--config") && has_val) i++;
        else if (!strcmp(a, "--nodes")  && has_val) cfg.node_count = atoi(argv[++i]);
        else if (!strcmp(a, "--rows")   && has_val) cfg.mesh_rows  = atoi(argv[++i]);
        else if (!strcmp(a, "--cols")   && has_val) cfg.mesh_cols  = atoi(argv[++i]);
        else {
            fprintf(stderr, "unknown or incomplete option: %s\n", a);
            usage(argv[0]);
            return 1;
        }
    }

    if (!sim_config_validate(&cfg)) return 1;

    printf("=== Project 13 / Group 1 -- Topology Verification ===\n\n");
    printf("-- Configuration (%s) --\n", cfg_path);
    printf("Ring nodes      : %d\n", cfg.node_count);
    printf("Mesh dimensions : %d x %d\n", cfg.mesh_rows, cfg.mesh_cols);
    printf("Ring routing    : Shortest-path routing\n");
    printf("Mesh routing    : Dimension-order (XY) routing\n");
    printf("Traffic seed    : %u\n\n", cfg.seed);

    topology_t *ring = topology_build_ring(cfg.node_count);
    topology_t *mesh = topology_build_mesh(cfg.mesh_rows, cfg.mesh_cols);
    if (ring == NULL || mesh == NULL) {
        fprintf(stderr, "ERROR: failed to construct the configured topologies.\n");
        topology_free(ring);
        topology_free(mesh);
        return 1;
    }

    printf("-- Static metrics vs closed form --\n");
    check("ring node count",      ring->num_nodes, cfg.node_count);
    check("ring diameter",        topology_diameter(ring),
                                  sim_config_expect_ring_diameter(&cfg));
    check("ring bisection width", topology_bisection_bandwidth(ring),
                                  sim_config_expect_ring_bisection(&cfg));
    check("mesh node count",      mesh->num_nodes, cfg.mesh_rows * cfg.mesh_cols);
    check("mesh diameter",        topology_diameter(mesh),
                                  sim_config_expect_mesh_diameter(&cfg));
    check("mesh bisection width", topology_bisection_bandwidth(mesh),
                                  sim_config_expect_mesh_bisection(&cfg));

    printf("\n-- Routing demonstration --\n");
    demo_route(mesh, "XY",
               mesh_node_id(0, 0, cfg.mesh_cols),
               mesh_node_id(cfg.mesh_rows - 1, cfg.mesh_cols - 1, cfg.mesh_cols));
    demo_route(ring, "ring", 1, cfg.node_count * 3 / 4);

    topology_free(ring);
    topology_free(mesh);

    if (failures) {
        printf("\nFAILED: %d topology check(s) did not match the closed form.\n", failures);
        return 1;
    }
    printf("\nTopology verification passed (6 checks).\n");
    return 0;
}
