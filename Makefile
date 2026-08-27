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

# Full Week 3 pipeline: verify Week 2, test Week 3, sweep, analyse.
week3: all
	./test_topology
	./verify_topology
	./test_simulator
	./dump_traffic 3000
	python3 python/gen_datacenter_traffic.py --verify results/traffic_reference_c.csv
	./run_sweep | tee results/week3_sweep_console.txt
	python3 python/analyze_network_results.py

# Full Week 4 pipeline: everything in week3, plus the folded-torus sweep
# and the area/wiring-cost model.
week4: all
	./test_topology
	./verify_topology
	./test_simulator
	./dump_traffic 3000
	python3 python/gen_datacenter_traffic.py --verify results/traffic_reference_c.csv
	./run_sweep | tee results/week3_sweep_console.txt
	python3 python/analyze_network_results.py
	python3 python/area_model.py

clean:
	rm -f test_topology verify_topology test_simulator run_sweep dump_traffic *.exe

.PHONY: all week3 week4 clean
