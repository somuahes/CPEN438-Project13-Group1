/* =========================================================================
 * sim_config.c
 * See sim_config.h for the module contract.
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_config.h"
#include "traffic.h"

void sim_config_defaults(sim_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->node_count = 16; c->mesh_rows = 4; c->mesh_cols = 4;
    c->seed = 1301u; c->hot_nodes = 2; c->hot_fraction = 0.50;
    c->warmup = 3000L; c->measure = 12000L; c->drain_max = 30000L;
    c->sat_efficiency = 0.95;
    c->do_ring = c->do_mesh = c->do_torus = 1;
    snprintf(c->outdir, sizeof(c->outdir), "results");
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

int sim_config_parse_rates(const char *v, double *out, int cap) {
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

int sim_config_load(sim_config_t *c, const char *path, int required) {
    FILE *f = fopen(path, "r");
    if (!f) {
        if (required) fprintf(stderr, "FATAL: cannot open config %s\n", path);
        return required ? 0 : 1;
    }
    int node_count = c->node_count, ring_nodes = c->node_count;
    int seen_nc = 0, seen_rn = 0;

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

        if      (!strcmp(k, "node_count"))          { node_count = atoi(v); seen_nc = 1; }
        else if (!strcmp(k, "ring_nodes"))          { ring_nodes = atoi(v); seen_rn = 1; }
        else if (!strcmp(k, "mesh_rows"))             c->mesh_rows = atoi(v);
        else if (!strcmp(k, "mesh_cols"))             c->mesh_cols = atoi(v);
        else if (!strcmp(k, "traffic_seed"))          c->seed = (unsigned)strtoul(v, NULL, 10);
        else if (!strcmp(k, "hot_node_count"))        c->hot_nodes = atoi(v);
        else if (!strcmp(k, "hot_traffic_fraction"))  c->hot_fraction = atof(v);
        else if (!strcmp(k, "warmup_cycles"))         c->warmup = atol(v);
        else if (!strcmp(k, "measurement_cycles"))    c->measure = atol(v);
        else if (!strcmp(k, "drain_cap_cycles"))      c->drain_max = atol(v);
        else if (!strcmp(k, "saturation_efficiency")) c->sat_efficiency = atof(v);
        else if (!strcmp(k, "injection_rates"))
            c->nrates = sim_config_parse_rates(v, c->rates, SIM_CONFIG_MAX_RATES);
    }
    fclose(f);

    if (seen_nc && seen_rn && node_count != ring_nodes) {
        fprintf(stderr, "FATAL: %s sets node_count=%d but ring_nodes=%d\n",
                path, node_count, ring_nodes);
        return 0;
    }
    if (seen_rn)      c->node_count = ring_nodes;
    else if (seen_nc) c->node_count = node_count;
    return 1;
}

int sim_config_validate(const sim_config_t *c) {
    const char *e = NULL;

    /* Only the selected topologies are constrained, so a ring-only run is
     * not rejected for mesh dimensions it never uses. */
    int smallest = 0;
    if (c->do_ring) smallest = c->node_count;
    if (c->do_mesh || c->do_torus) {
        int mesh_n = c->mesh_rows * c->mesh_cols;
        if (!smallest || mesh_n < smallest) smallest = mesh_n;
    }

    if (c->do_ring && c->node_count < 2)       e = "node_count must be >= 2";
    else if ((c->do_mesh || c->do_torus) && (c->mesh_rows < 1 || c->mesh_cols < 1))
                                               e = "mesh_rows/mesh_cols must be >= 1";
    else if (c->hot_nodes < 1)                 e = "hot_node_count must be >= 1";
    else if (c->hot_nodes > MAX_HOT_NODES)     e = "hot_node_count exceeds MAX_HOT_NODES";
    else if (smallest && c->hot_nodes > smallest) e = "hot_node_count exceeds the node count";
    else if (c->hot_fraction < 0.0 || c->hot_fraction > 1.0) e = "hot_traffic_fraction must be in [0,1]";
    else if (c->warmup < 0)                    e = "warmup_cycles must be >= 0";
    else if (c->measure < 1)                   e = "measurement_cycles must be >= 1";
    else if (c->drain_max < 0)                 e = "drain_cap_cycles must be >= 0";
    else if (c->sat_efficiency <= 0.0 || c->sat_efficiency > 1.0) e = "saturation_efficiency must be in (0,1]";
    else if (c->nrates < 1)                    e = "no injection rates configured";

    if (e) { fprintf(stderr, "FATAL: %s\n", e); return 0; }
    return 1;
}

int sim_config_expect_ring_diameter(const sim_config_t *c) {
    return c->node_count / 2;
}

int sim_config_expect_ring_bisection(const sim_config_t *c) {
    return (c->node_count < 3) ? c->node_count - 1 : 2;
}

int sim_config_expect_mesh_diameter(const sim_config_t *c) {
    return (c->mesh_rows - 1) + (c->mesh_cols - 1);
}

int sim_config_expect_mesh_bisection(const sim_config_t *c) {
    return (c->mesh_rows < c->mesh_cols) ? c->mesh_rows : c->mesh_cols;
}

/* Each wrapped dimension is a ring, so the worst case is floor(k/2) per
 * dimension; an unwrapped dimension (extent < 3) contributes k-1. */
int sim_config_expect_torus_diameter(const sim_config_t *c) {
    int dr = (c->mesh_rows >= 3) ? c->mesh_rows / 2 : c->mesh_rows - 1;
    int dc = (c->mesh_cols >= 3) ? c->mesh_cols / 2 : c->mesh_cols - 1;
    return dr + dc;
}

int sim_config_expect_torus_bisection(const sim_config_t *c) {
    int vertical   = c->mesh_rows * ((c->mesh_cols >= 3) ? 2 : 1);
    int horizontal = c->mesh_cols * ((c->mesh_rows >= 3) ? 2 : 1);
    return (vertical < horizontal) ? vertical : horizontal;
}
