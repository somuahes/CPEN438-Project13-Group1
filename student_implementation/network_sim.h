/* =========================================================================
 * network_sim.h
 *
 * Project 13 -- Wiring the Data Centre: Interconnection Networks for a
 * Simulated Accra Warehouse-Scale Cluster
 *
 * WEEK 3 MODULE: cycle-accurate flit-level interconnection-network
 * simulator built on top of the verified Week 2 topology/routing module.
 *
 * -------------------------------------------------------------------------
 * NETWORK MODEL
 * -------------------------------------------------------------------------
 * Each node holds one input-buffered, virtual-channel (VC) router plus a
 * processing element with an injection queue.
 *
 *   Packet          PACKET_FLITS flits (1 head, N-2 body, 1 tail).
 *   Link            One flit per cycle per direction (unit channel width).
 *   Router ports    One port per topology neighbour (max 4) + one local
 *                   injection port; ejection is a separate output port.
 *   Flow control    Credit-based wormhole flow control. A flit advances
 *                   only if the downstream VC buffer has a free slot.
 *   VC allocation   Performed by the head flit; held until the tail flit
 *                   leaves the VC (so a packet is never interleaved).
 *   Switch alloc    One flit per output port per cycle, round-robin among
 *                   the requesting input VCs.
 *   Routing         Supplied unchanged by the Week 2 module
 *                   (mesh: deterministic XY; ring: shortest direction).
 *
 * Per-cycle pipeline (evaluated as a two-phase read/commit so that no
 * flit can traverse two routers in one cycle):
 *   1. snapshot credits
 *   2. route computation + VC allocation for newly arrived head flits
 *   3. switch allocation (round-robin arbitration per output port)
 *   4. commit all granted flit movements
 *   5. packet generation and injection into the local port
 *
 * -------------------------------------------------------------------------
 * DEADLOCK FREEDOM
 * -------------------------------------------------------------------------
 * The 2D mesh with strict XY dimension-order routing has an acyclic
 * channel-dependency graph and is deadlock-free with a single VC.
 * A bidirectional ring with shortest-direction routing is NOT: the
 * clockwise channels alone form a cycle. The baseline therefore uses the
 * standard *dateline* rule (Dally & Towles): the ring is given two VCs
 * per port; a packet starts on the low VC and is forced onto the high VC
 * once it crosses the link between node N-1 and node 0. Because shortest
 * -direction routing never exceeds floor(N/2) hops, a packet crosses the
 * dateline at most once, which breaks the cycle.
 *
 * A torus has a cycle in EVERY dimension, so it needs the dateline rule in
 * both. Dimension-order routing already forbids returning to the X
 * dimension once the Y dimension has been entered, so the two dimensions
 * cannot form a joint cycle and one pair of VCs can be reused for both:
 * the dateline bit is RESET at the X->Y transition. A packet therefore
 * crosses at most one dateline per dimension while holding the low VC,
 * and 2 VCs suffice -- the same budget as the ring and the mesh.
 *
 * All three topologies are therefore given the SAME baseline VC budget
 * (VC_BASELINE = 2 VCs of VC_BUF_FLITS flits each) so that the
 * topology comparison is not confounded by buffer resources.
 *
 * -------------------------------------------------------------------------
 * PACKET ACCOUNTING (Project 13 Section M)
 * -------------------------------------------------------------------------
 * Nothing is discarded silently. The only place a packet can be lost is a
 * full source queue, and that event is counted in stats.dropped_full.
 * At all times the simulator maintains the exact conservation identity
 *
 *   generated == delivered + dropped_full + in_network + queued
 *
 * which sim_check_conservation() asserts, and which the unit tests in
 * tests/test_simulator.c verify at every swept injection rate.
 * ========================================================================= */

#ifndef NETWORK_SIM_H
#define NETWORK_SIM_H

#include "network_topology.h"
#include "traffic.h"

/* ---- Fixed model parameters ------------------------------------------ */
#define PACKET_FLITS    4     /* flits per packet (1 head, 2 body, 1 tail) */
#define VC_BUF_FLITS    4     /* flit slots per virtual channel            */
#define MAX_VCS         4     /* compile-time maximum VCs per port         */
#define MAX_PORTS       5     /* 4 neighbour ports + 1 local inject port   */
#define LOCAL_PORT      4
#define EJECT_PORT      5     /* output-port index used for ejection       */
#define NUM_OUT_PORTS   6
#define EJECT_BW        4     /* flits ejected per node per cycle          */
#define INJ_QUEUE_CAP   1024  /* source-queue depth, in packets            */

/* Baseline / innovation VC configurations ------------------------------- */
typedef enum {
    VC_MODE_BASELINE = 0, /* 2 VCs, 1 source queue  -- Week 3 baseline     */
    VC_MODE_MORE     = 1, /* 4 VCs, 1 source queue  -- buffer-only control */
    VC_MODE_QCLASS   = 2, /* 2 VCs, 2 class queues  -- queue-only control  */
    VC_MODE_CLASS    = 3  /* 4 VCs, 2 class queues  -- THE INNOVATION      */
} vc_mode_t;

/* -------------------------------------------------------------------------
 * THE WEEK 3 INNOVATION: end-to-end traffic-class separation
 * -------------------------------------------------------------------------
 * Under hot-node-skewed traffic the queues feeding the congested database
 * nodes back-pressure into shared buffers, and ordinary background packets
 * that merely happen to sit behind a hot-destined packet are stalled even
 * though their own route is completely free. That is head-of-line (HoL)
 * blocking, and it is what the Project 13 innovation brief asks a virtual
 * -channel scheme to remove.
 *
 * VC_MODE_CLASS splits every shared buffering resource in the machine into
 * two classes -- "hot-destined" and "background" -- at BOTH points where
 * HoL blocking can occur:
 *   (a) the source queue at each node becomes two class queues, so a
 *       backed-up hot flow cannot block background injection; and
 *   (b) the router input buffers are partitioned so a hot packet can never
 *       occupy a VC that a background packet needs.
 *
 * Each class is given exactly the baseline's per-port VC budget, so the
 * scheme is measured against two deliberate controls:
 *   VC_MODE_MORE   -- same total buffering, no isolation  (isolates the
 *                     effect of simply adding buffers), and
 *   VC_MODE_QCLASS -- class queues only, shared VCs       (isolates the
 *                     effect of the injection-side split).
 * ------------------------------------------------------------------------- */

const char *vc_mode_name(vc_mode_t m);
int         vc_mode_vcs(vc_mode_t m);      /* VCs per input port           */
int         vc_mode_queues(vc_mode_t m);   /* source queues per node       */

/* ---- Flit ------------------------------------------------------------- */
typedef struct {
    int  pid;          /* packet id (for tracing / assertions)             */
    int  dst;
    int  is_head;
    int  is_tail;
    int  hot;          /* destination belongs to the hot-node set          */
    int  dateline;     /* dateline bit (0 before crossing, 1 after)        */
    int  dim;          /* torus DOR phase: 0 = X (columns), 1 = Y (rows)   */
    int  hops;         /* hops travelled so far                            */
    long gen_cycle;    /* cycle at which the packet was generated          */
    int  measured;     /* generated inside the measurement window          */
} flit_t;

/* ---- Virtual channel -------------------------------------------------- */
typedef struct {
    flit_t buf[VC_BUF_FLITS];
    int    head;       /* circular-buffer read index                       */
    int    count;
    int    out_port;   /* allocated output port, -1 if unallocated         */
    int    out_vc;     /* allocated downstream VC index                    */
    int    reserved;   /* held by a packet (set at head, cleared at tail)   */

    /* Local-injection bookkeeping (LOCAL_PORT VCs only) */
    int    inj_remaining;
    int    inj_dst, inj_hot, inj_pid, inj_measured;
    long   inj_gen;
} vc_t;

/* ---- Router ----------------------------------------------------------- */
typedef struct {
    vc_t in[MAX_PORTS][MAX_VCS];
    int  rr[NUM_OUT_PORTS];   /* round-robin arbitration pointers          */
} router_t;

/* ---- Source-queue entry ----------------------------------------------- */
typedef struct {
    int  dst, hot, pid, measured;
    long gen_cycle;
} qpkt_t;

/* ---- Statistics ------------------------------------------------------- */
typedef struct {
    long generated;            /* packets generated (whole run)            */
    long dropped_full;         /* packets lost to a full source queue      */
    long delivered;            /* packets fully ejected (whole run)        */
    long in_network;           /* packets injected but not yet delivered   */
    long queued;               /* packets waiting in source queues         */

    long m_generated;          /* packets generated in measurement window  */
    long m_delivered;          /* ... of those, delivered by end of drain  */
    long m_latency_sum;        /* sum of their end-to-end latencies        */
    long m_hop_sum;            /* sum of their hop counts                  */
    long m_latency_max;
    long m_flits_ejected;      /* flits ejected during measurement window  */
    long m_dropped;            /* drops during the measurement window      */

    /* Per-traffic-class breakdown. Class 1 = destined for a hot database
     * node, class 0 = ordinary background traffic. Under uniform traffic
     * every packet is class 0. These are the metrics that expose
     * head-of-line blocking and the effect of the innovation. */
    long m_gen_cls[2], m_del_cls[2], m_lat_cls[2], m_drop_cls[2];
    long m_flits_cls[2];

    long peak_queue;           /* deepest source queue seen                */
} sim_stats_t;

/* ---- Switch-allocation grant ------------------------------------------ */
typedef struct {
    int n, p, v;          /* source: node, input port, input VC          */
    int op;               /* output port (EJECT_PORT for ejection)       */
    int m, q, ov;         /* destination: node, input port, VC           */
} move_t;

/* ---- Simulator -------------------------------------------------------- */
typedef struct {
    const topology_t *topo;
    int        num_nodes;
    int        vcs;
    vc_mode_t  vc_mode;

    router_t  *rt;

    /* Per-cycle scratch, allocated for num_nodes. space[] and req_op[] are
     * [node][port][vc] flattened via sim_scratch_idx(). */
    int       *space;
    int       *req_op;
    move_t    *moves;

    int      **rev_port;   /* rev_port[n][p]: port on adj[n][p] facing n   */
    int      **nh_port;    /* nh_port[n][d]: output port from n toward d,  */
                           /* or EJECT_PORT when n == d                    */
    int      **cross;      /* cross[n][p]: 1 if that link is the dateline  */

    qpkt_t   **q;          /* source queues, indexed [node*2 + class]      */
    int       *q_head, *q_count;
    int        nqueues;    /* 1 (shared FIFO) or 2 (class-separated)        */
    int        q_cap;      /* capacity of each queue, in packets           */

    traffic_t  traffic;
    double     inject_rate;   /* offered load, flits/node/cycle            */
    double     p_packet;      /* per-node per-cycle packet probability     */
    int        injecting;

    long       cycle;
    long       warmup, measure;
    int        next_pid;

    sim_stats_t st;
} sim_t;

/* ---- API -------------------------------------------------------------- */

/* Build a simulator over an already-constructed topology. The topology is
 * borrowed, not owned; the caller frees it with topology_free(). */
sim_t *sim_create(const topology_t *t, traffic_type_t traffic,
                  uint32_t seed, double hot_fraction, int num_hot,
                  vc_mode_t mode);
void   sim_free(sim_t *s);

/* Advance the simulator by exactly one cycle. */
void   sim_step(sim_t *s);

/* Run warmup -> measurement -> drain and fill in the statistics.
 * `drain_max` bounds the drain phase so that a run above saturation still
 * terminates; any measurement packet still undelivered at that point is
 * reported (m_generated - m_delivered) rather than being ignored. */
void   sim_run(sim_t *s, double inject_rate, long warmup, long measure,
               long drain_max);

/* Conservation check: returns 1 if
 * generated == delivered + dropped_full + in_network + queued. */
int    sim_check_conservation(const sim_t *s);

/* Derived metrics -------------------------------------------------------- */
double sim_accepted_throughput(const sim_t *s);  /* flits/node/cycle       */
double sim_avg_latency(const sim_t *s);          /* cycles                 */
double sim_avg_hops(const sim_t *s);

/* Per-class metrics. cls: 0 = background traffic, 1 = hot-node traffic. */
double sim_avg_latency_class(const sim_t *s, int cls);
double sim_throughput_class(const sim_t *s, int cls);

#endif /* NETWORK_SIM_H */
