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

/* ---- 8. Torus construction, matching docs/topology_hand_calculations.md 6.1-6.4 */

static int test_torus_structure(void) {
    topology_t *t = topology_build_torus(4, 4);
    CHECK(t != NULL);
    CHECK(t->num_nodes == 16);

    int links = 0;
    for (int i = 0; i < 16; i++) {
        CHECK(t->adj_count[i] == 4);          /* uniform degree, unlike the mesh */
        links += t->adj_count[i];
    }
    CHECK(links / 2 == 32);                   /* hand-computed link count */

    CHECK(topology_diameter(t) == 4);         /* hand: 4, vs 6 for the mesh */
    CHECK(topology_bisection_bandwidth(t) == 8);  /* hand: 2 x min(R,C) */

    topology_free(t);

    /* Rectangular case: bisection is the smaller of the two cuts. */
    topology_t *r = topology_build_torus(3, 5);
    CHECK(topology_bisection_bandwidth(r) == 6);   /* min(3*2, 5*2) */
    CHECK(topology_diameter(r) == 1 + 2);          /* floor(3/2) + floor(5/2) */
    topology_free(r);

    /* A dimension of extent 2 is left unwrapped, so degree stays 3 there
     * rather than gaining a parallel link. */
    topology_t *n = topology_build_torus(2, 4);
    CHECK(n->adj_count[0] == 3);
    topology_free(n);
    return 0;
}

/* ---- 9. Torus dimension-order routing ------------------------------- */

static int test_torus_dor_routing(void) {
    topology_t *t = topology_build_torus(4, 4);
    int path[64];

    /* The mesh's worst-case pair is 2 hops in the torus: one wraparound
     * step in each dimension. */
    int hops = topology_route(t, 0, 15, path, 64);
    CHECK(hops == 2);
    CHECK(path[0] == 0);
    CHECK(path[hops] == 15);

    /* The genuinely farthest node is (2,2). */
    CHECK(topology_route(t, 0, 10, path, 64) == 4);

    /* Every route must be minimal, land on the destination, and take only
     * real links -- X hops first, then Y, never back to X. */
    for (int a = 0; a < 16; a++) {
        for (int b = 0; b < 16; b++) {
            if (a == b) continue;
            int h = topology_route(t, a, b, path, 64);
            CHECK(h > 0 && h <= 4);
            CHECK(path[0] == a);
            CHECK(path[h] == b);

            int seen_row_move = 0;
            for (int i = 0; i < h; i++) {
                int r1, c1, r2, c2;
                mesh_node_coords(path[i], 4, &r1, &c1);
                mesh_node_coords(path[i + 1], 4, &r2, &c2);
                int row_moved = (r1 != r2), col_moved = (c1 != c2);
                CHECK(row_moved != col_moved);       /* exactly one axis per hop */
                if (col_moved) CHECK(!seen_row_move); /* dimension order held */
                if (row_moved) seen_row_move = 1;

                int adjacent = 0;                    /* hop uses a real link */
                for (int k = 0; k < t->adj_count[path[i]]; k++)
                    if (t->adj[path[i]][k] == path[i + 1]) adjacent = 1;
                CHECK(adjacent);
            }
        }
    }

    /* Deterministic tie-break: at exactly half way round a 4-ring the
     * increasing direction is taken, so 0 -> 2 goes via column 1. */
    CHECK(topology_route(t, 0, 2, path, 64) == 2);
    CHECK(path[1] == 1);

    topology_free(t);
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
    failures += test_torus_structure();
    failures += test_torus_dor_routing();

    if (failures == 0) {
        printf("ALL TESTS PASSED (%d checks across 9 test functions)\n", tests_run);
        return 0;
    } else {
        printf("%d test function(s) FAILED\n", failures);
        return 1;
    }
}
