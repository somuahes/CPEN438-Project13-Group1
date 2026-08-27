# CPEN 438 — Project 13, Group 1
## Wiring the Data Centre: Interconnection Networks for a Simulated Accra Warehouse-Scale Cluster

**Week 3 — flit-level simulation, traffic experiments, saturation analysis and innovation.**

Assigned configuration: 16 nodes · 16-node ring · 4 × 4 mesh · traffic seed 1301 ·
hot database nodes {5, 1} derived from the seed · 50 % hot traffic share.

---

### Repository layout

```
student_implementation/
  network_topology.c/.h   Week 2 module — UNCHANGED, reused as the routing oracle
  traffic.c/.h            Week 3 — seeded uniform-random and hot-node generators
  network_sim.c/.h        Week 3 — cycle-accurate flit-level simulator
tests/
  test_topology.c         Week 2 unit tests   (35 checks / 7 functions)
  verify_topology.c       Week 2 verification driver
  test_simulator.c        Week 3 unit tests   (97 checks / 12 functions)
  run_sweep.c             Week 3 injection-rate sweep driver (216 runs)
  dump_traffic.c          C traffic trace dumper, for cross-language verification
python/
  gen_datacenter_traffic.py    Seeded traffic generator + C bit-equality verifier
  analyze_network_results.py   Figures and result tables
matlab/
  bisection_latency_model.m    Analytical channel-load / latency model
configs/group1_config.txt      Every parameter needed to reproduce a run
results/                       CSVs, console log and figures/
```

### Build and run everything

```
make week3
```

That target rebuilds all five executables, re-runs the Week 2 tests, runs the
Week 3 tests, verifies the C and Python traffic generators against each other,
runs the full sweep, and regenerates every figure and table.

Individual steps:

```
gcc -O2 -Wall -Wextra -std=c11 -Istudent_implementation \
    -o run_sweep student_implementation/network_topology.c \
                 student_implementation/traffic.c \
                 student_implementation/network_sim.c \
                 tests/run_sweep.c -lm
./run_sweep
python3 python/analyze_network_results.py
```

### Week 3 headline results

| Topology | Traffic | Diameter | Bisection | Saturation (flits/node/cycle) | Peak throughput |
|----------|---------|---------:|----------:|------------------------------:|----------------:|
| ring(16) | uniform | 8 | 2 | 0.28 | 0.272 |
| mesh(4×4)| uniform | 6 | 4 | **0.70** | 0.724 |
| ring(16) | hot-node| 8 | 2 | 0.20 | 0.242 |
| mesh(4×4)| hot-node| 6 | 4 | 0.20 | 0.385 |

216 runs · 0 packet-conservation violations · 0 packets lost below saturation.

**Innovation** — end-to-end traffic-class separation (class-partitioned virtual
channels *and* class-separated injection queues). At an offered load of 0.24
under hot-node traffic it cuts background-traffic latency from 574 → 8.0 cycles
on the mesh (71×) and 985 → 16.5 on the ring (60×), and raises mesh peak
accepted throughput from 0.385 to 0.568. Two controls — extra buffers alone,
and class queues alone — recover only 1.5× and 3.6×, so the gain is attributable
to isolation rather than to buffering.

### Notes for reviewers

- The ring uses the **dateline virtual-channel rule** (Dally & Towles) because a
  bidirectional ring with shortest-direction routing has a cyclic
  channel-dependency graph. The mesh is given the same VC budget so the
  comparison measures topology, not buffers.
- **Ejection bandwidth is 4 flits/cycle**, set to the maximum node degree, so that
  the hot-node experiment measures the network rather than the processing
  element. This is stated explicitly in §4.4 of the design specification.
- The MATLAB model is the submitted analytical deliverable; the identical model
  is mirrored in `analyze_network_results.py` so the figures can be regenerated
  without a MATLAB licence.
