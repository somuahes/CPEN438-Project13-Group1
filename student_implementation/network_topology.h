/* =========================================================================
 * network_topology.h
 *
 * Project 13 -- Wiring the Data Centre: Interconnection Networks for a
 * Simulated Accra Warehouse-Scale Cluster
 *
 * WEEK 2 MODULE: topology construction, static topology metrics
 * (diameter, bisection bandwidth), and dimension-order routing.
 *
 * Designed to slot into the flit-level simulation core built for the
 * Week 1 ring demo: this module owns everything under "Design the flit
 * -level packet model" / "Design at least two topologies" / "Design
 * dimension-order routing" (Project 13 brief, Section H).
 * ========================================================================= */

#ifndef NETWORK_TOPOLOGY_H
#define NETWORK_TOPOLOGY_H

typedef enum {
    TOPO_RING  = 0,
    TOPO_MESH  = 1,
    TOPO_TORUS = 2
} topology_type_t;

/* Adjacency-list graph representation. Works for any topology; the ring
 * and mesh builders below populate it according to each topology's
 * connectivity rule. Using one shared representation means diameter,
 * routing, and (later) the traffic/injection harness do not need to know
 * which topology they are looking at. */
typedef struct {
    topology_type_t type;
    int num_nodes;
    int rows;   /* mesh only; 1 for ring */
    int cols;   /* mesh: number of columns; ring: num_nodes */
    int adj_cap;      /* allocated neighbour slots per node */
    int *adj_count;   /* [num_nodes] */
    int **adj;        /* [num_nodes][adj_count[i]] neighbour ids */
} topology_t;

/* ---- Construction / teardown ---------------------------------------- */
topology_t *topology_build_ring(int num_nodes);
topology_t *topology_build_mesh(int rows, int cols);

/* Folded torus: the mesh plus wraparound links, so every row and every
 * column is a ring. "Folded" refers to the physical layout only -- the
 * adjacency, diameter and bisection are those of a plain torus.
 *
 * A dimension of extent < 3 is left unwrapped: its wraparound link would
 * duplicate the edge the mesh already provides, and the parallel link
 * would alias in the simulator's reverse-port lookup. */
topology_t *topology_build_torus(int rows, int cols);
void        topology_free(topology_t *t);

/* ---- Mesh coordinate helpers (row-major indexing) --------------------
 * id = row * cols + col, 0 <= row < rows, 0 <= col < cols               */
int  mesh_node_id(int row, int col, int cols);
void mesh_node_coords(int id, int cols, int *row, int *col);

/* ---- Static topology metrics -----------------------------------------
 * diameter: computed generically via BFS from every node (works for any
 *   topology_t, used to cross-check the closed-form hand computation).
 * bisection_bandwidth: closed-form for ring/mesh (see .c file for the
 *   derivation and its citation to the Project 13 worked example).      */
int topology_diameter(const topology_t *t);
int topology_bisection_bandwidth(const topology_t *t);

/* ---- Routing -----------------------------------------------------------
 * Fills path[] with the sequence of node ids from src to dst INCLUSIVE
 * of both endpoints (path[0] == src, path[hops] == dst). Returns the
 * number of hops (0 if src == dst), or -1 if max_len is too small.
 *
 * mesh_route_xy: dimension-order (X-then-Y) routing -- move along
 *   columns first until the column matches, then along rows.
 * torus_route_dor: the same dimension order, but each dimension takes the
 *   shorter way round the ring. Ties (exactly half way) break to the
 *   increasing direction, deterministically -- the routing stays strictly
 *   single-path, never choosing a path on congestion.
 * ring_route_shortest: shortest-direction routing on the ring (clockwise
 *   or counter-clockwise, whichever is fewer hops).
 * topology_route: dispatches to the correct routine based on t->type.   */
int mesh_route_xy(const topology_t *t, int src, int dst, int *path, int max_len);
int torus_route_dor(const topology_t *t, int src, int dst, int *path, int max_len);
int ring_route_shortest(const topology_t *t, int src, int dst, int *path, int max_len);
int topology_route(const topology_t *t, int src, int dst, int *path, int max_len);

#endif /* NETWORK_TOPOLOGY_H */
