/* =========================================================================
 * test_topology.c
 *
 * Week 2 unit tests (Part I Sec.4 "unit tests" deliverable; Testing,
 * Validation & Documentation Lead owns this file per Part I Sec.9).
 *
 * Covers: ring/mesh diameter, ring/mesh bisection bandwidth, XY routing
 * correctness (shortest path + dimension order), ring shortest-direction
 * routing, and edge cases (src==dst, 1xN line mesh).
 *
 * Build: gcc -Wall -Wextra -o test_topology network_topology.c test_topology.c -lm
 * ========================================================================= */

#include <assert.h>
#include <stdio.h>
#include "network_topology.h"

static int tests_run = 0;
#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAILED: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while (0)

/* ---- Diameter ---------------------------------------------------- */

static int test_ring_diameter(void) {
    topology_t *r16 = topology_build_ring(16);
    CHECK(topology_diameter(r16) == 8);               /* matches worked example */
    topology_free(r16);

    topology_t *r8 = topology_build_ring(8);
    CHECK(topology_diameter(r8) == 4);                 /* floor(8/2) */
    topology_free(r8);

    topology_t *r7 = topology_build_ring(7);
    CHECK(topology_diameter(r7) == 3);                 /* floor(7/2) */
    topology_free(r7);
    return 0;
}

static int test_mesh_diameter(void) {
    topology_t *m4x4 = topology_build_mesh(4, 4);
    CHECK(topology_diameter(m4x4) == 6);               /* matches worked example */
    topology_free(m4x4);

    topology_t *m2x8 = topology_build_mesh(2, 8);
    CHECK(topology_diameter(m2x8) == 1 + 7);           /* (rows-1)+(cols-1) */
    topology_free(m2x8);

    topology_t *line = topology_build_mesh(1, 5);       /* degenerate: 1D line */
    CHECK(topology_diameter(line) == 4);
    topology_free(line);
    return 0;
}

/* ---- Bisection bandwidth ------------------------------------------ */

static int test_ring_bisection(void) {
    topology_t *r16 = topology_build_ring(16);
    CHECK(topology_bisection_bandwidth(r16) == 2);
    topology_free(r16);

    topology_t *r5 = topology_build_ring(5);
    CHECK(topology_bisection_bandwidth(r5) == 2);       /* independent of N */
    topology_free(r5);
    return 0;
}

static int test_mesh_bisection(void) {
    topology_t *m4x4 = topology_build_mesh(4, 4);
    CHECK(topology_bisection_bandwidth(m4x4) == 4);
    topology_free(m4x4);

    topology_t *m4x8 = topology_build_mesh(4, 8);
    CHECK(topology_bisection_bandwidth(m4x8) == 4);      /* min(4,8) */
    topology_free(m4x8);

    topology_t *m1x8 = topology_build_mesh(1, 8);
    CHECK(topology_bisection_bandwidth(m1x8) == 1);      /* line: min(1,8) */
    topology_free(m1x8);
    return 0;
}

/* ---- Routing -------------------------------------------------------- */

static int test_mesh_xy_routing_shortest_and_order(void) {
    topology_t *m = topology_build_mesh(4, 4);
    int path[32];

    /* Corner to corner: hop count must equal Manhattan distance (3+3=6),
     * which must also equal the diameter computed above. */
    int src = mesh_node_id(0, 0, 4);
    int dst = mesh_node_id(3, 3, 4);
    int hops = mesh_route_xy(m, src, dst, path, 32);
    CHECK(hops == 6);
    CHECK(path[0] == src);
    CHECK(path[hops] == dst);

    /* Dimension order: the first 3 hops must all be column moves (row
     * unchanged), then the last 3 hops must all be row moves (column
     * unchanged) -- i.e. X before Y, no interleaving. */
    int r, c, prev_r, prev_c;
    mesh_node_coords(path[0], 4, &prev_r, &prev_c);
    int seen_row_move = 0;
    for (int i = 1; i <= hops; i++) {
        mesh_node_coords(path[i], 4, &r, &c);
        int row_changed = (r != prev_r);
        int col_changed = (c != prev_c);
        CHECK(row_changed != col_changed);      /* exactly one axis per hop */
        if (row_changed) seen_row_move = 1;
        if (col_changed) CHECK(!seen_row_move); /* no column move after a row move */
        prev_r = r; prev_c = c;
    }

    /* src == dst -> zero hops, path is just the source node. */
    int hops2 = mesh_route_xy(m, src, src, path, 32);
    CHECK(hops2 == 0);
    CHECK(path[0] == src);

    topology_free(m);
    return 0;
}

static int test_ring_shortest_direction_routing(void) {
    topology_t *r = topology_build_ring(16);
    int path[32];

    /* 1 -> 12: clockwise = 11 hops, counter-clockwise = 5 hops -> must
     * take the counter-clockwise (shorter) direction. */
    int hops = ring_route_shortest(r, 1, 12, path, 32);
    CHECK(hops == 5);
    CHECK(path[0] == 1);
    CHECK(path[hops] == 12);

    /* Every step must be an actual ring edge (neighbours differ by 1
     * mod n), confirming no illegal "teleport" hops. */
    for (int i = 1; i <= hops; i++) {
        int diff = (path[i] - path[i - 1] + 16) % 16;
        CHECK(diff == 1 || diff == 15);
    }

    topology_free(r);
    return 0;
}

static int test_topology_route_dispatch(void) {
    topology_t *ring = topology_build_ring(8);
    topology_t *mesh = topology_build_mesh(2, 4);
    int path[32];
    CHECK(topology_route(ring, 0, 4, path, 32) == 4);   /* ring dispatch */
    CHECK(topology_route(mesh, 0, mesh_node_id(1, 3, 4), path, 32) == 4); /* mesh dispatch */
    topology_free(ring);
    topology_free(mesh);
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_ring_diameter();
    failures += test_mesh_diameter();
    failures += test_ring_bisection();
    failures += test_mesh_bisection();
    failures += test_mesh_xy_routing_shortest_and_order();
    failures += test_ring_shortest_direction_routing();
    failures += test_topology_route_dispatch();

    if (failures == 0) {
        printf("ALL TESTS PASSED (%d checks across 7 test functions)\n", tests_run);
        return 0;
    } else {
        printf("%d test function(s) FAILED\n", failures);
        return 1;
    }
}
