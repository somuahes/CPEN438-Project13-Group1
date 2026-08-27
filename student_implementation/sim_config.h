/* =========================================================================
 * sim_config.h
 *
 * Project 13 -- Wiring the Data Centre.
 *
 * Loads the assigned run parameters from configs/, so that node count,
 * topology size, seed and sweep parameters live in one designated file
 * rather than as magic numbers inside the drivers, and so that an unseen
 * configuration can be run without a rebuild.
 * ========================================================================= */

#ifndef SIM_CONFIG_H
#define SIM_CONFIG_H

#define SIM_CONFIG_MAX_RATES 64
#define SIM_CONFIG_DEFAULT   "configs/group1_week3_config.txt"

typedef struct {
    int      node_count, mesh_rows, mesh_cols;
    unsigned seed;
    int      hot_nodes;
    double   hot_fraction;
    long     warmup, measure, drain_max;
    double   sat_efficiency;
    double   rates[SIM_CONFIG_MAX_RATES];
    int      nrates;
    int      do_ring, do_mesh, do_torus;
    char     outdir[256];
} sim_config_t;

void sim_config_defaults(sim_config_t *c);

/* Rejects out-of-range values with a message on stderr. Returns 1 if the
 * configuration is usable. Drivers must call this before building
 * topologies: an unvalidated hot_node_count of 0, for instance, silently
 * aims the whole hot share at node 0 while classifying none of it as hot. */
int  sim_config_validate(const sim_config_t *c);

/* Parses `key = value`, honouring '#' comments and a trailing '\' that
 * continues onto the next line. Returns 1 on success. A missing file is
 * an error only when `required`. */
int  sim_config_load(sim_config_t *c, const char *path, int required);

/* Reads whitespace- or comma-separated positive rates. Returns the count. */
int  sim_config_parse_rates(const char *v, double *out, int cap);

/* Closed-form expectations for the configured size, used as the Week 2
 * verification gate. */
int  sim_config_expect_ring_diameter(const sim_config_t *c);
int  sim_config_expect_ring_bisection(const sim_config_t *c);
int  sim_config_expect_mesh_diameter(const sim_config_t *c);
int  sim_config_expect_mesh_bisection(const sim_config_t *c);
int  sim_config_expect_torus_diameter(const sim_config_t *c);
int  sim_config_expect_torus_bisection(const sim_config_t *c);

#endif /* SIM_CONFIG_H */
