/* =========================================================================
 * network_topology.c
 * See network_topology.h for the module contract and Project 13 mapping.
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include "network_topology.h"

/* ------------------------------------------------------------------- */
/* Construction                                                        */
/* ------------------------------------------------------------------- */

static topology_t *alloc_topology(topology_type_t type, int num_nodes,
                                   int rows, int cols, int max_degree) {
    topology_t *t = (topology_t *)malloc(sizeof(topology_t));
    t->type = type;
    t->num_nodes = num_nodes;
    t->rows = rows;
    t->cols = cols;
    t->adj_cap = max_degree;
    t->adj_count = (int *)calloc((size_t)num_nodes, sizeof(int));
    t->adj = (int **)malloc((size_t)num_nodes * sizeof(int *));
    for (int i = 0; i < num_nodes; i++)
        t->adj[i] = (int *)malloc((size_t)max_degree * sizeof(int));
    return t;
}

/* Overflowing adj[] would corrupt the heap silently, so this is fatal
 * rather than clamped: it means a builder declared the wrong degree. */
static void add_edge(topology_t *t, int a, int b) {
    if (t->adj_count[a] >= t->adj_cap || t->adj_count[b] >= t->adj_cap) {
        fprintf(stderr, "FATAL: node degree exceeds %d adding edge %d-%d\n",
                t->adj_cap, a, b);
        exit(2);
    }
    t->adj[a][t->adj_count[a]++] = b;
    t->adj[b][t->adj_count[b]++] = a;
}

topology_t *topology_build_ring(int num_nodes) {
    if (num_nodes < 2) return NULL;
    topology_t *t = alloc_topology(TOPO_RING, num_nodes, 1, num_nodes, 2);

    if (num_nodes == 2) {
        /* Degenerate case: a 2-node "ring" is a single pair. We record
         * one bidirectional link; bisection_bandwidth() special-cases
         * this below rather than relying on adjacency alone, since a
         * true ring topology assumes num_nodes >= 3. */
        add_edge(t, 0, 1);
        return t;
    }

    for (int i = 0; i < num_nodes; i++) {
        int next = (i + 1) % num_nodes;
        add_edge(t, i, next);
    }
    return t;
}

topology_t *topology_build_mesh(int rows, int cols) {
    if (rows < 1 || cols < 1) return NULL;
    int num_nodes = rows * cols;
    topology_t *t = alloc_topology(TOPO_MESH, num_nodes, rows, cols, 4);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int id = mesh_node_id(r, c, cols);
            /* Only add the "right" and "down" edge from each node so
             * every edge is added exactly once. */
            if (c + 1 < cols) add_edge(t, id, mesh_node_id(r, c + 1, cols));
            if (r + 1 < rows) add_edge(t, id, mesh_node_id(r + 1, c, cols));
        }
    }
    return t;
}

void topology_free(topology_t *t) {
    if (!t) return;
    for (int i = 0; i < t->num_nodes; i++) free(t->adj[i]);
    free(t->adj);
    free(t->adj_count);
    free(t);
}

/* ------------------------------------------------------------------- */
/* Coordinate helpers                                                  */
/* ------------------------------------------------------------------- */

int mesh_node_id(int row, int col, int cols) {
    return row * cols + col;
}

void mesh_node_coords(int id, int cols, int *row, int *col) {
    *row = id / cols;
    *col = id % cols;
}

/* ------------------------------------------------------------------- */
/* Diameter (generic BFS -- topology-agnostic, used to cross-check the  */
/* closed-form bisection/diameter values computed by hand)             */
/* ------------------------------------------------------------------- */

static int bfs_eccentricity(const topology_t *t, int src) {
    int n = t->num_nodes;
    int *dist = (int *)malloc((size_t)n * sizeof(int));
    int *queue = (int *)malloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) dist[i] = -1;

    int head = 0, tail = 0;
    dist[src] = 0;
    queue[tail++] = src;
    int max_dist = 0;

    while (head < tail) {
        int u = queue[head++];
        for (int k = 0; k < t->adj_count[u]; k++) {
            int v = t->adj[u][k];
            if (dist[v] == -1) {
                dist[v] = dist[u] + 1;
                if (dist[v] > max_dist) max_dist = dist[v];
                queue[tail++] = v;
            }
        }
    }
    free(dist);
    free(queue);
    return max_dist;
}

int topology_diameter(const topology_t *t) {
    int diam = 0;
    for (int i = 0; i < t->num_nodes; i++) {
        int ecc = bfs_eccentricity(t, i);
        if (ecc > diam) diam = ecc;
    }
    return diam;
}

/* ------------------------------------------------------------------- */
/* Bisection bandwidth (closed-form)                                   */
/*                                                                     */
/* Ring: cutting a ring anywhere splits it into exactly two crossing   */
/*   links, independent of node count -- reproduces the Project 13    */
/*   worked example ("16-node ring ... bisection bandwidth equal to   */
/*   just 2 links").                                                  */
/*                                                                     */
/* 2D mesh (rows x cols, no wraparound): cutting along the column     */
/*   midpoint crosses `rows` links; cutting along the row midpoint    */
/*   crosses `cols` links. The bisection bandwidth is the minimum of  */
/*   the two, min(rows, cols) -- reproduces the worked example        */
/*   ("4x4 mesh has ... bisection bandwidth of 4 links").             */
/*                                                                     */
/* General minimum-bisection is NP-hard; these closed forms are the   */
/* standard textbook (Hennessy & Patterson / Dally & Towles) values   */
/* for these two *regular* topologies and are what "hand-computed"    */
/* bisection bandwidth means for this assignment.                     */
/* ------------------------------------------------------------------- */

int topology_bisection_bandwidth(const topology_t *t) {
    if (t->type == TOPO_RING) {
        if (t->num_nodes < 3) return t->num_nodes - 1; /* degenerate n=2 */
        return 2;
    }
    /* TOPO_MESH */
    return (t->rows < t->cols) ? t->rows : t->cols;
}

/* ------------------------------------------------------------------- */
/* Routing                                                             */
/* ------------------------------------------------------------------- */

int mesh_route_xy(const topology_t *t, int src, int dst, int *path, int max_len) {
    int r1, c1, r2, c2;
    mesh_node_coords(src, t->cols, &r1, &c1);
    mesh_node_coords(dst, t->cols, &r2, &c2);

    int hops = 0;
    if (hops >= max_len) return -1;
    path[hops] = src;

    int r = r1, c = c1;

    /* X-first: walk along columns until the column matches. */
    while (c != c2) {
        c += (c2 > c) ? 1 : -1;
        hops++;
        if (hops >= max_len) return -1;
        path[hops] = mesh_node_id(r, c, t->cols);
    }
    /* Then Y: walk along rows until the row matches. */
    while (r != r2) {
        r += (r2 > r) ? 1 : -1;
        hops++;
        if (hops >= max_len) return -1;
        path[hops] = mesh_node_id(r, c, t->cols);
    }
    return hops;
}

int ring_route_shortest(const topology_t *t, int src, int dst, int *path, int max_len) {
    int n = t->num_nodes;
    int cw_hops  = (dst - src + n) % n;         /* clockwise (increasing id) */
    int ccw_hops = (src - dst + n) % n;         /* counter-clockwise */

    int hops = (cw_hops <= ccw_hops) ? cw_hops : ccw_hops;
    if (hops >= max_len) return -1;

    int step = (cw_hops <= ccw_hops) ? 1 : -1;
    int cur = src;
    path[0] = src;
    for (int i = 1; i <= hops; i++) {
        cur = (cur + step + n) % n;
        path[i] = cur;
    }
    return hops;
}

int topology_route(const topology_t *t, int src, int dst, int *path, int max_len) {
    if (t->type == TOPO_MESH) return mesh_route_xy(t, src, dst, path, max_len);
    return ring_route_shortest(t, src, dst, path, max_len);
}
