/* =========================================================================
 * network_sim.c -- see network_sim.h for the model description.
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_sim.h"

/* ------------------------------------------------------------------- */
/* Small helpers                                                       */
/* ------------------------------------------------------------------- */

const char *vc_mode_name(vc_mode_t m) {
    switch (m) {
        case VC_MODE_MORE:   return "vc4_plain";
        case VC_MODE_QCLASS: return "vc2_qclass";
        case VC_MODE_CLASS:  return "vc4_class";
        default:             return "baseline_vc2";
    }
}

int vc_mode_vcs(vc_mode_t m) {
    return (m == VC_MODE_MORE || m == VC_MODE_CLASS) ? 4 : 2;
}

int vc_mode_queues(vc_mode_t m) {
    return (m == VC_MODE_QCLASS || m == VC_MODE_CLASS) ? 2 : 1;
}

static int sim_scratch_idx(int node, int port, int vc) {
    return (node * MAX_PORTS + port) * MAX_VCS + vc;
}

static void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n, sz);
    if (!p) { fprintf(stderr, "FATAL: out of memory\n"); exit(2); }
    return p;
}

/* ------------------------------------------------------------------- */
/* Construction                                                        */
/* ------------------------------------------------------------------- */

sim_t *sim_create(const topology_t *t, traffic_type_t traffic,
                  uint32_t seed, double hot_fraction, int num_hot,
                  vc_mode_t mode) {
    int n = t->num_nodes;
    sim_t *s = (sim_t *)xcalloc(1, sizeof(sim_t));

    s->topo      = t;
    s->num_nodes = n;
    s->vc_mode   = mode;
    s->vcs       = vc_mode_vcs(mode);
    s->rt        = (router_t *)xcalloc((size_t)n, sizeof(router_t));
    s->space     = (int *)xcalloc((size_t)n * MAX_PORTS * MAX_VCS, sizeof(int));
    s->req_op    = (int *)xcalloc((size_t)n * MAX_PORTS * MAX_VCS, sizeof(int));
    s->moves     = (move_t *)xcalloc((size_t)n * NUM_OUT_PORTS * EJECT_BW, sizeof(move_t));

    s->rev_port = (int **)xcalloc((size_t)n, sizeof(int *));
    s->nh_port  = (int **)xcalloc((size_t)n, sizeof(int *));
    s->cross    = (int **)xcalloc((size_t)n, sizeof(int *));
    s->nqueues  = vc_mode_queues(mode);
    s->q_cap    = INJ_QUEUE_CAP / s->nqueues;   /* total buffering is the
                                                 * same in every mode      */
    s->q        = (qpkt_t **)xcalloc((size_t)n * 2, sizeof(qpkt_t *));
    s->q_head   = (int *)xcalloc((size_t)n * 2, sizeof(int));
    s->q_count  = (int *)xcalloc((size_t)n * 2, sizeof(int));

    for (int i = 0; i < n; i++) {
        s->rev_port[i] = (int *)xcalloc(MAX_PORTS, sizeof(int));
        s->cross[i]    = (int *)xcalloc(MAX_PORTS, sizeof(int));
        s->nh_port[i]  = (int *)xcalloc((size_t)n, sizeof(int));
        for (int c = 0; c < 2; c++)
            s->q[i * 2 + c] = (qpkt_t *)xcalloc((size_t)s->q_cap, sizeof(qpkt_t));
        for (int p = 0; p < MAX_PORTS; p++) {
            s->rev_port[i][p] = -1;
            for (int v = 0; v < MAX_VCS; v++) {
                s->rt[i].in[p][v].out_port = -1;
                s->rt[i].in[p][v].out_vc   = -1;
            }
        }
    }

    /* Reverse-port table: the port index on neighbour m that points
     * back at n. Needed to find the correct downstream input buffer. */
    for (int i = 0; i < n; i++) {
        for (int p = 0; p < t->adj_count[i]; p++) {
            int m = t->adj[i][p];
            for (int q = 0; q < t->adj_count[m]; q++) {
                if (t->adj[m][q] == i) { s->rev_port[i][p] = q; break; }
            }
            /* Ring dateline: the link joining node N-1 and node 0. */
            if (t->type == TOPO_RING &&
                ((i == n - 1 && m == 0) || (i == 0 && m == n - 1))) {
                s->cross[i][p] = 1;
            }
        }
    }

    /* Next-hop table, memoised from the *Week 2* routing functions so the
     * simulator and the Week 2 verifier are guaranteed to agree. */
    int *path = (int *)xcalloc((size_t)(n + 2), sizeof(int));
    for (int a = 0; a < n; a++) {
        for (int b = 0; b < n; b++) {
            if (a == b) { s->nh_port[a][b] = EJECT_PORT; continue; }
            int hops = topology_route(t, a, b, path, n + 2);
            if (hops < 1) { fprintf(stderr, "FATAL: no route %d->%d\n", a, b); exit(2); }
            int next = path[1];
            int port = -1;
            for (int p = 0; p < t->adj_count[a]; p++) {
                if (t->adj[a][p] == next) { port = p; break; }
            }
            if (port < 0) { fprintf(stderr, "FATAL: route %d->%d leaves the topology\n", a, b); exit(2); }
            s->nh_port[a][b] = port;
        }
    }
    free(path);

    traffic_init(&s->traffic, traffic, n, seed, hot_fraction, num_hot);
    s->next_pid = 1;
    return s;
}

void sim_free(sim_t *s) {
    if (!s) return;
    for (int i = 0; i < s->num_nodes; i++) {
        free(s->rev_port[i]); free(s->nh_port[i]);
        free(s->cross[i]);
        free(s->q[i * 2]); free(s->q[i * 2 + 1]);
    }
    free(s->rev_port); free(s->nh_port); free(s->cross);
    free(s->space); free(s->req_op); free(s->moves);
    free(s->q); free(s->q_head); free(s->q_count);
    free(s->rt);
    free(s);
}

/* ------------------------------------------------------------------- */
/* VC selection policy                                                 */
/*                                                                     */
/* The legal VC range for a packet is narrowed in two independent      */
/* steps:                                                              */
/*   1. Class partition (innovation only): hot-destined packets may    */
/*      only use the upper half of the VCs, everything else the lower  */
/*      half. This is what removes head-of-line blocking between the   */
/*      congested hot-node flow and ordinary background traffic.       */
/*   2. Dateline partition (ring only): inside whatever range step 1   */
/*      produced, packets that have not yet crossed the N-1 <-> 0 link */
/*      use the lower half and packets that have use the upper half.   */
/*      This is the deadlock-avoidance rule and is applied to the      */
/*      baseline as well.                                              */
/* ------------------------------------------------------------------- */

static void vc_range(const sim_t *s, int hot, int dateline, int *lo, int *hi) {
    int a = 0, b = s->vcs;

    if (s->vc_mode == VC_MODE_CLASS) {
        int half = s->vcs / 2;
        a = hot ? half : 0;
        b = a + half;
    }
    if (s->topo->type == TOPO_RING) {
        int half = (b - a) / 2;
        if (half >= 1) {
            a = a + dateline * half;
            b = a + half;
        }
    }
    *lo = a; *hi = b;
}

/* Find a free VC on input port `q` of node `m`, honouring the policy. */
static int select_free_vc(const sim_t *s, int m, int q, int hot, int dateline) {
    int lo, hi;
    vc_range(s, hot, dateline, &lo, &hi);
    for (int v = lo; v < hi; v++) {
        if (!s->rt[m].in[q][v].reserved) return v;
    }
    return -1;
}

/* ------------------------------------------------------------------- */
/* Buffer primitives                                                   */
/* ------------------------------------------------------------------- */

static flit_t *vc_front(vc_t *vc) { return &vc->buf[vc->head]; }

static void vc_pop(vc_t *vc) {
    vc->head = (vc->head + 1) % VC_BUF_FLITS;
    vc->count--;
}

static void vc_push(vc_t *vc, const flit_t *f) {
    int tail = (vc->head + vc->count) % VC_BUF_FLITS;
    vc->buf[tail] = *f;
    vc->count++;
}

/* ------------------------------------------------------------------- */
/* One simulation cycle                                                */
/* ------------------------------------------------------------------- */

void sim_step(sim_t *s) {
    const topology_t *t = s->topo;
    int n = s->num_nodes;

    /* ---- Phase 0: credit snapshot (start-of-cycle buffer occupancy) --
     * Taken before any movement so that a flit cannot be forwarded
     * through two routers within the same cycle. */
    int *space = s->space;
    for (int i = 0; i < n; i++)
        for (int p = 0; p < MAX_PORTS; p++)
            for (int v = 0; v < s->vcs; v++)
                space[sim_scratch_idx(i, p, v)] = VC_BUF_FLITS - s->rt[i].in[p][v].count;

    /* ---- Phase 1: route computation + VC allocation ------------------ */
    int *req_op = s->req_op;
    for (int i = 0; i < n; i++)
        for (int p = 0; p < MAX_PORTS; p++)
            for (int v = 0; v < s->vcs; v++)
                req_op[sim_scratch_idx(i, p, v)] = -1;

    for (int i = 0; i < n; i++) {
        for (int p = 0; p < MAX_PORTS; p++) {
            if (p != LOCAL_PORT && p >= t->adj_count[i]) continue;
            for (int v = 0; v < s->vcs; v++) {
                vc_t *vc = &s->rt[i].in[p][v];
                if (vc->count == 0) continue;
                flit_t *f = vc_front(vc);

                if (vc->out_port < 0) {
                    /* Only a head flit performs route computation / VC
                     * allocation; body and tail flits inherit it. */
                    if (!f->is_head) continue;

                    int op = s->nh_port[i][f->dst];
                    if (op == EJECT_PORT) {
                        vc->out_port = EJECT_PORT;
                        vc->out_vc   = -1;
                    } else {
                        int m = t->adj[i][op];
                        int q = s->rev_port[i][op];
                        int d_next = s->cross[i][op] ? 1 : f->dateline;
                        int ov = select_free_vc(s, m, q, f->hot, d_next);
                        if (ov < 0) continue;             /* VC busy: stall */
                        s->rt[m].in[q][ov].reserved = 1;  /* reserve it     */
                        vc->out_port = op;
                        vc->out_vc   = ov;
                    }
                }
                req_op[sim_scratch_idx(i, p, v)] = vc->out_port;
            }
        }
    }

    /* ---- Phase 2: switch allocation (round-robin per output port) ---- */
    move_t *moves = s->moves;
    int nmoves = 0;

    for (int i = 0; i < n; i++) {
        for (int op = 0; op < NUM_OUT_PORTS; op++) {
            if (op == LOCAL_PORT) continue;   /* input-only port */
            if (op != EJECT_PORT && op >= t->adj_count[i]) continue;
            int grants = (op == EJECT_PORT) ? EJECT_BW : 1;
            int slots  = MAX_PORTS * MAX_VCS;
            int start  = s->rt[i].rr[op];

            for (int k = 0; k < slots && grants > 0; k++) {
                int idx = (start + k) % slots;
                int p = idx / MAX_VCS, v = idx % MAX_VCS;
                if (v >= s->vcs) continue;
                if (req_op[sim_scratch_idx(i, p, v)] != op) continue;

                vc_t *vc = &s->rt[i].in[p][v];
                if (op == EJECT_PORT) {
                    moves[nmoves].n = i; moves[nmoves].p = p; moves[nmoves].v = v;
                    moves[nmoves].op = EJECT_PORT;
                    moves[nmoves].m = -1; moves[nmoves].q = -1; moves[nmoves].ov = -1;
                    nmoves++;
                } else {
                    int m  = t->adj[i][op];
                    int q  = s->rev_port[i][op];
                    int ov = vc->out_vc;
                    if (space[sim_scratch_idx(m, q, ov)] <= 0) continue;   /* no credit     */
                    space[sim_scratch_idx(m, q, ov)]--;
                    moves[nmoves].n = i; moves[nmoves].p = p; moves[nmoves].v = v;
                    moves[nmoves].op = op;
                    moves[nmoves].m = m; moves[nmoves].q = q; moves[nmoves].ov = ov;
                    nmoves++;
                }
                grants--;
                s->rt[i].rr[op] = (idx + 1) % slots;
            }
        }
    }

    /* ---- Phase 3: commit ------------------------------------------- */
    for (int k = 0; k < nmoves; k++) {
        move_t *mv = &moves[k];
        vc_t *src = &s->rt[mv->n].in[mv->p][mv->v];
        flit_t f = *vc_front(src);
        vc_pop(src);

        if (mv->op == EJECT_PORT) {
            /* Accepted throughput is measured as flits ejected *during the
             * measurement window*, irrespective of when they were
             * generated. This is the standard definition and makes the
             * metric independent of how long the drain phase is allowed
             * to run. */
            if (s->cycle >= s->warmup && s->cycle < s->warmup + s->measure) {
                s->st.m_flits_ejected++;
                s->st.m_flits_cls[f.hot ? 1 : 0]++;
            }
            if (f.is_tail) {
                s->st.delivered++;
                s->st.in_network--;
                if (f.measured) {
                    long lat = s->cycle - f.gen_cycle;
                    int  cls = f.hot ? 1 : 0;
                    s->st.m_delivered++;
                    s->st.m_latency_sum += lat;
                    s->st.m_hop_sum     += f.hops;
                    s->st.m_del_cls[cls]++;
                    s->st.m_lat_cls[cls] += lat;
                    if (lat > s->st.m_latency_max) s->st.m_latency_max = lat;
                }
            }
        } else {
            f.hops++;
            if (s->cross[mv->n][mv->op]) f.dateline = 1;
            vc_push(&s->rt[mv->m].in[mv->q][mv->ov], &f);
        }

        if (f.is_tail) {
            /* The packet has completely left this VC: release both the
             * output reservation it held and the VC itself. */
            src->out_port = -1;
            src->out_vc   = -1;
            src->reserved = 0;
        }
    }

    /* ---- Phase 4: packet generation and injection -------------------- */
    for (int i = 0; i < n; i++) {
        if (s->injecting) {
            if (rng_next_double(&s->traffic.rng) < s->p_packet) {
                int dst = traffic_dest(&s->traffic, i);
                int hot = traffic_is_hot(&s->traffic, dst);
                int cls = hot ? 1 : 0;
                int measured = (s->cycle >= s->warmup &&
                                s->cycle <  s->warmup + s->measure) ? 1 : 0;
                /* Which source queue this packet joins. With one queue
                 * (baseline / buffer-only control) everything shares a
                 * single FIFO, which is where injection-side head-of-line
                 * blocking comes from. */
                int qi = i * 2 + ((s->nqueues == 2) ? cls : 0);

                s->st.generated++;
                if (measured) { s->st.m_generated++; s->st.m_gen_cls[cls]++; }

                if (s->q_count[qi] >= s->q_cap) {
                    s->st.dropped_full++;               /* explicit loss   */
                    if (measured) { s->st.m_dropped++; s->st.m_drop_cls[cls]++; }
                } else {
                    int slot = (s->q_head[qi] + s->q_count[qi]) % s->q_cap;
                    s->q[qi][slot].dst       = dst;
                    s->q[qi][slot].hot       = hot;
                    s->q[qi][slot].pid       = s->next_pid++;
                    s->q[qi][slot].measured  = measured;
                    s->q[qi][slot].gen_cycle = s->cycle;
                    s->q_count[qi]++;
                    if (s->q_count[qi] > s->st.peak_queue)
                        s->st.peak_queue = s->q_count[qi];
                }
            }
        }

        /* Assign queued packets to free local VCs (policy-constrained).
         * With two class queues the node alternates between them, so a
         * backlogged hot queue can never starve background injection. */
        for (int pass = 0; pass < 2 * MAX_VCS; pass++) {
            int c  = (int)((s->cycle + pass) % s->nqueues);
            int qi = i * 2 + c;
            if (s->q_count[qi] == 0) continue;
            qpkt_t *pk = &s->q[qi][s->q_head[qi]];
            int v = select_free_vc(s, i, LOCAL_PORT, pk->hot, 0);
            if (v < 0) continue;
            vc_t *lvc = &s->rt[i].in[LOCAL_PORT][v];
            lvc->reserved      = 1;
            lvc->inj_remaining = PACKET_FLITS;
            lvc->inj_dst       = pk->dst;
            lvc->inj_hot       = pk->hot;
            lvc->inj_pid       = pk->pid;
            lvc->inj_measured  = pk->measured;
            lvc->inj_gen       = pk->gen_cycle;
            s->q_head[qi] = (s->q_head[qi] + 1) % s->q_cap;
            s->q_count[qi]--;
            s->st.in_network++;
        }

        /* Serialise one flit per local VC per cycle into its buffer. */
        for (int v = 0; v < s->vcs; v++) {
            vc_t *lvc = &s->rt[i].in[LOCAL_PORT][v];
            if (lvc->inj_remaining <= 0) continue;
            if (lvc->count >= VC_BUF_FLITS) continue;
            flit_t f;
            memset(&f, 0, sizeof(f));
            f.pid       = lvc->inj_pid;
            f.dst       = lvc->inj_dst;
            f.hot       = lvc->inj_hot;
            f.gen_cycle = lvc->inj_gen;
            f.measured  = lvc->inj_measured;
            f.dateline  = 0;
            f.hops      = 0;
            f.is_head   = (lvc->inj_remaining == PACKET_FLITS);
            f.is_tail   = (lvc->inj_remaining == 1);
            vc_push(lvc, &f);
            lvc->inj_remaining--;
        }
    }

    s->cycle++;
}

/* ------------------------------------------------------------------- */
/* Run control                                                         */
/* ------------------------------------------------------------------- */

void sim_run(sim_t *s, double inject_rate, long warmup, long measure,
             long drain_max) {
    s->inject_rate = inject_rate;
    s->p_packet    = inject_rate / (double)PACKET_FLITS;
    s->warmup      = warmup;
    s->measure     = measure;
    s->injecting   = 1;

    for (long c = 0; c < warmup + measure; c++) sim_step(s);

    /* Drain: stop offering new load and let the measurement packets
     * retire. Bounded so that a post-saturation run still terminates. */
    s->injecting = 0;
    for (long c = 0; c < drain_max; c++) {
        if (s->st.m_delivered >= s->st.m_generated - s->st.m_dropped) break;
        sim_step(s);
    }

    /* Final queue census for the conservation identity. */
    long q = 0;
    for (int i = 0; i < s->num_nodes * 2; i++) q += s->q_count[i];
    s->st.queued = q;
}

int sim_check_conservation(const sim_t *s) {
    long q = 0;
    for (int i = 0; i < s->num_nodes * 2; i++) q += s->q_count[i];
    return (s->st.generated ==
            s->st.delivered + s->st.dropped_full + s->st.in_network + q);
}

/* ------------------------------------------------------------------- */
/* Derived metrics                                                     */
/* ------------------------------------------------------------------- */

double sim_accepted_throughput(const sim_t *s) {
    if (s->measure <= 0) return 0.0;
    return (double)s->st.m_flits_ejected /
           ((double)s->measure * (double)s->num_nodes);
}

double sim_avg_latency(const sim_t *s) {
    if (s->st.m_delivered == 0) return 0.0;
    return (double)s->st.m_latency_sum / (double)s->st.m_delivered;
}

double sim_avg_hops(const sim_t *s) {
    if (s->st.m_delivered == 0) return 0.0;
    return (double)s->st.m_hop_sum / (double)s->st.m_delivered;
}

double sim_avg_latency_class(const sim_t *s, int cls) {
    if (cls < 0 || cls > 1 || s->st.m_del_cls[cls] == 0) return 0.0;
    return (double)s->st.m_lat_cls[cls] / (double)s->st.m_del_cls[cls];
}

double sim_throughput_class(const sim_t *s, int cls) {
    if (cls < 0 || cls > 1 || s->measure <= 0) return 0.0;
    return (double)s->st.m_flits_cls[cls] /
           ((double)s->measure * (double)s->num_nodes);
}
