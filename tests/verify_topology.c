/* =========================================================================
 * Week 2 verification driver for Group 1, Project 13.
 *
 * Group 1 assigned configuration:
 *   - Node count: 16
 *   - Ring: 16 nodes
 *   - Mesh: 4 x 4
 *   - Ring routing: shortest-path routing
 *   - Mesh routing: dimension-order (XY) routing
 *   - Traffic seed: 1301
 *
 * Expected topology values:
 *   - 16-node ring: diameter = 8, bisection bandwidth = 2
 *   - 4x4 mesh:     diameter = 6, bisection bandwidth = 4
 *
 * The traffic seed and traffic-pattern generation are not exercised in
 * this Week 2 verification program; they are retained for later traffic
 * experiments.
 *
 * Usage:
 *   ./verify_topology.exe
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
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

static void demo_route(topology_t *t, const char *label, int src, int dst) {
    int path[64];
    int hops = topology_route(t, src, dst, path, 64);
    printf("  %s route %d -> %d (%d hops): ", label, src, dst, hops);
    for (int i = 0; i <= hops; i++) printf("%d%s", path[i], i < hops ? " -> " : "\n");
}

int main(void) {
    const int NODE_COUNT = 16;
    const int MESH_ROWS = 4;
    const int MESH_COLS = 4;
    const int TRAFFIC_SEED = 1301;

    printf("=== Project 13 / Group 1 -- Week 2 Topology Verification ===\n\n");

    printf("-- Assigned Group 1 Configuration --\n");
    printf("Node count      : %d\n", NODE_COUNT);
    printf("Ring nodes      : %d\n", NODE_COUNT);
    printf("Mesh dimensions : %d x %d\n", MESH_ROWS, MESH_COLS);
    printf("Ring routing    : Shortest-path routing\n");
    printf("Mesh routing    : Dimension-order (XY) routing\n");
    printf("Traffic seed    : %d\n\n", TRAFFIC_SEED);

    topology_t *ring = topology_build_ring(NODE_COUNT);
    topology_t *mesh = topology_build_mesh(MESH_ROWS, MESH_COLS);

    if (ring == NULL || mesh == NULL) {
        fprintf(stderr, "ERROR: failed to construct assigned topologies.\n");
        topology_free(ring);
        topology_free(mesh);
        return 1;
    }

    printf("-- Static Topology Verification --\n");
    report("ring(16)", ring);
    report("mesh(4x4)", mesh);

    printf("\nExpected values:\n");
    printf("  Ring 16 : diameter = 8, bisection bandwidth = 2\n");
    printf("  Mesh 4x4: diameter = 6, bisection bandwidth = 4\n\n");

    printf("-- Routing Verification --\n");
    demo_route(
        mesh,
        "XY",
        mesh_node_id(0, 0, MESH_COLS),
        mesh_node_id(3, 3, MESH_COLS)
    );

    demo_route(
        ring,
        "ring",
        1,
        12
    );

    topology_free(ring);
    topology_free(mesh);

    printf("\nWeek 2 topology verification completed successfully.\n");

    return 0;
}
