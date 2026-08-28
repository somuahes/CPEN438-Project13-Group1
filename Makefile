# Project 13 / Group 1 -- Week 3 build
CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -Istudent_implementation
LDLIBS  = -lm
CFG     = student_implementation/sim_config.c
SRC     = student_implementation/network_topology.c \
          student_implementation/traffic.c \
          student_implementation/network_sim.c

all: test_topology verify_topology test_simulator run_sweep dump_traffic

test_topology:   student_implementation/network_topology.c tests/test_topology.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

verify_topology: student_implementation/network_topology.c $(CFG) tests/verify_topology.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_simulator:  $(SRC) tests/test_simulator.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

run_sweep:       $(SRC) $(CFG) tests/run_sweep.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

dump_traffic:    student_implementation/traffic.c tests/dump_traffic.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Full Week 3 pipeline, kept as an alias so existing instructions still work.
week3: week4

# Full Week 4 pipeline: rebuilds every binary, re-runs the Week 2 and Week 3
# test suites, cross-checks the C and Python traffic generators, runs the
# ring/mesh/torus sweep, and regenerates every reported figure and table.
# Raw output lands in results/raw, tables in results/processed, figures in
# results/figures -- the same layout that is committed.
week4: all
	./test_topology
	./verify_topology
	./test_simulator
	./dump_traffic 3000
	python3 python/gen_datacenter_traffic.py --verify results/raw/traffic_reference_c.csv
	./run_sweep | tee results/raw/week3_sweep_console.txt
	python3 python/analyze_network_results.py
	python3 python/area_model.py

clean:
	rm -f test_topology verify_topology test_simulator run_sweep dump_traffic *.exe

.PHONY: all week3 week4 clean
