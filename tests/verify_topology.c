/* =========================================================================
 * verify_topology.c
 *
 * Week 2 verification driver: builds ring and mesh topologies and prints
 * diameter / bisection bandwidth so they can be checked line-by-line
 * against the hand computation in the Week 2 design spec (Section 4).
 *
 * Reproduces the Project 13 brief's own worked example as a sanity check
 * before this is run against the team's actual assigned node count:
 *   - 16-node ring   -> diameter 8,  bisection bandwidth 2
 *   - 4x4 mesh       -> diameter 6,  bisection bandwidth 4
 *
 * Usage: ./verify_topology [assigned_node_count]
 *   With no argument, runs the worked-example sizes above.
 *   With an argument N, also builds a ring(N) and the "squarest" mesh
 *   factorisation of N, so the team can drop in their real assigned
 *   node count the moment it is confirmed from the Week 1 proposal.
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "network_topology.h"

static void report(const char *label, topology_t *t) {
    int diam = topology_diameter(t);
    int bisect = topology_bisection_bandwidth(t);
    if (t->type == TOPO_MESH) {
        printf("%-28s nodes=%-4d (%dx%d)  diameter=%-3d  bisection_bw=%d\n",
               label, t->num_nodes, t->rows, t->cols, diam, bisect);
    } else {
        printf("%-28s nodes=%-4d           diameter=%-3d  bisection_bw=%d\n",
               label, t->num_nodes, diam, bisect);
    }
}

/* Pick the factor pair (rows, cols) closest to square for a given N, so
 * an arbitrary assigned node count still gets a sensible mesh shape. */
static void squarest_factors(int n, int *rows, int *cols) {
    int r = (int)floor(sqrt((double)n));
    while (r > 1 && n % r != 0) r--;
    *rows = r;
    *cols = n / r;
}

static void demo_route(topology_t *t, const char *label, int src, int dst) {
    int path[64];
    int hops = topology_route(t, src, dst, path, 64);
    printf("  %s route %d -> %d (%d hops): ", label, src, dst, hops);
    for (int i = 0; i <= hops; i++) printf("%d%s", path[i], i < hops ? " -> " : "\n");
}

int main(int argc, char **argv) {
    printf("=== Project 13 / Group 1 -- Week 2 topology verification ===\n\n");

    printf("-- Worked example from the project brief --\n");
    topology_t *ring16 = topology_build_ring(16);
    report("ring(16)", ring16);
    topology_t *mesh4x4 = topology_build_mesh(4, 4);
    report("mesh(4x4)", mesh4x4);
    printf("  Expected: ring diameter=8 bisection=2 | mesh diameter=6 bisection=4\n\n");

    demo_route(mesh4x4, "XY", mesh_node_id(0, 0, 4), mesh_node_id(3, 3, 4));
    demo_route(ring16, "ring", 1, 12);
    printf("\n");

    topology_free(ring16);
    topology_free(mesh4x4);

    if (argc > 1) {
        int n = atoi(argv[1]);
        printf("-- Team-assigned node count (from Week 1 proposal) --\n");
        int rows, cols;
        squarest_factors(n, &rows, &cols);
        topology_t *ring_n = topology_build_ring(n);
        report("ring(N)", ring_n);
        topology_t *mesh_n = topology_build_mesh(rows, cols);
        report("mesh(N)", mesh_n);
        topology_free(ring_n);
        topology_free(mesh_n);
    } else {
        printf("(No node count passed -- re-run as `./verify_topology <N>`\n"
               " once the assigned seed/node count from the Week 1 proposal\n"
               " is confirmed, to verify the real assigned instance.)\n");
    }

    return 0;
}
