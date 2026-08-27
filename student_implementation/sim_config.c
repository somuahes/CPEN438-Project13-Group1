/* =========================================================================
 * sim_config.c
 * See sim_config.h for the module contract.
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_config.h"

void sim_config_defaults(sim_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->node_count = 16; c->mesh_rows = 4; c->mesh_cols = 4;
    c->seed = 1301u; c->hot_nodes = 2; c->hot_fraction = 0.50;
    c->warmup = 3000L; c->measure = 12000L; c->drain_max = 30000L;
    c->sat_efficiency = 0.95;
    c->do_ring = c->do_mesh = 1;
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

        if      (!strcmp(k, "node_count"))            c->node_count = atoi(v);
        else if (!strcmp(k, "ring_nodes"))            c->node_count = atoi(v);
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
