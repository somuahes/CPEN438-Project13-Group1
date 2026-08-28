/* =========================================================================
 * dump_traffic.c
 *
 * Project 13 / Group 1 -- WEEK 3 SUPPORT TOOL
 *
 * Writes the first N (src,dst) pairs produced by the C traffic generator
 * for each pattern, so that python/gen_datacenter_traffic.py can be shown
 * to reproduce exactly the same seeded stream. This is the evidence that
 * the Python analysis pipeline and the C simulator are working from the
 * same traffic, which is what "reproducibility" means for this project.
 *
 * Usage:  ./dump_traffic [count]      (default 2000)
 * Output: results/raw/traffic_reference_c.csv (override with argv[2])
 *
 * Build (from repository root):
 *   gcc -O2 -Wall -Wextra -std=c11 -Istudent_implementation \
 *       -o dump_traffic student_implementation/traffic.c \
 *                       tests/dump_traffic.c -lm
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include "traffic.h"

#define SEED         1301u
#define NODES        16
#define HOT_FRACTION 0.50
#define HOT_NODES    2

int main(int argc, char **argv) {
    int count = (argc > 1) ? atoi(argv[1]) : 2000;

    const char *out = (argc > 2) ? argv[2] : "results/raw/traffic_reference_c.csv";
    FILE *f = fopen(out, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", out); return 1; }
    fprintf(f, "pattern,index,src,dst\n");

    traffic_type_t types[2] = { TRAFFIC_UNIFORM, TRAFFIC_HOTNODE };
    for (int k = 0; k < 2; k++) {
        traffic_t t;
        traffic_init(&t, types[k], NODES, SEED, HOT_FRACTION, HOT_NODES);
        if (types[k] == TRAFFIC_HOTNODE) {
            printf("hot nodes derived from seed %u: ", SEED);
            for (int i = 0; i < t.num_hot; i++) printf("%d ", t.hot[i]);
            printf("\n");
        }
        for (int i = 0; i < count; i++) {
            int src = i % NODES;
            int dst = traffic_dest(&t, src);
            fprintf(f, "%s,%d,%d,%d\n", traffic_name(types[k]), i, src, dst);
        }
    }
    fclose(f);
    printf("wrote %d pairs per pattern to %s\n", count, out);
    return 0;
}
