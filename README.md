# Wiring the Data Centre

**Interconnection Networks for a Simulated Accra Warehouse-Scale Cluster**

CPEN 438 — Advanced Computer Architecture Systems and Design
Project 13 · Group 1 · University of Ghana, Legon

---

## 1. Purpose

A cycle-accurate, flit-level interconnection-network simulator used to measure how
topology choice governs latency, throughput and saturation in a 16-node
warehouse-scale cluster modelling a simplified Accra data centre.

Three topologies are compared — a 16-node ring, a 4×4 mesh, and a 4×4 folded torus —
under two traffic patterns, uniform-random and hot-node-skewed. The simulator is the
instrument; the deliverable is the explanation connecting hand-computed diameter and
bisection bandwidth to measured saturation behaviour, with every packet explicitly
accounted for.

Headline results are in [`report/`](report/); the full argument is in the Week 4
technical report.

## 2. Team

| Member | Primary contribution (per Git history) |
|---|---|
| Peggy Somuah (`somuahes`) | Project charter, requirements specification, architecture and topology diagrams, paper review, assigned configuration |
| Vanessa Ayertey | Ring and mesh topology construction, dimension-order routing, Week 2 unit tests and verification driver |
| Ebo Okrah (`bernardinee`) | Flit-level simulation core, virtual channels and credit-based flow control, traffic generators, sweep driver, analysis pipeline |
| Haqq Bassit | Folded-torus extension and area model, simulator hardening, configuration and driver parameterisation, technical report, reproducibility package |

> Role assignments per Part I §9 are recorded in each member's individual
> contribution form. The table above reflects the Git history, not the formal role
> allocation.

## 3. Assigned configuration

Every reported result uses this configuration, recorded in full in
[`configs/group1_week3_config.txt`](configs/group1_week3_config.txt):

| Parameter | Value |
|---|---|
| Node count | 16 |
| Topologies | ring(16), mesh(4×4), torus(4×4) |
| Traffic seed | **1301** |
| Hot nodes | {5, 1} — *derived from the seed, not hard-coded* |
| Hot traffic share | 50% |
| Packet size | 4 flits (1 head, 2 body, 1 tail) |
| Link bandwidth | 1 flit/cycle/direction |
| Router delay | 1 cycle/hop |
| VC buffer depth | 4 flits |
| Ejection bandwidth | 4 flits/cycle |
| Warm-up / measurement / drain cap | 3000 / 12 000 / 30 000 cycles |
| Injection rates | 18 points, 0.02 → 0.80 flits/node/cycle |

Because the seed is team-specific, our numbers should differ from every other team's.

## 4. Requirements

| Tool | Version used | Needed for |
|---|---|---|
| GCC | 13.3.0 (any C11 compiler) | Simulator and test suites |
| GNU Make | any | Build and pipeline targets |
| Python | 3.12.3 (3.8+ expected to work) | Traffic generator, analysis, area model |
| matplotlib | ≥ 3.5 | Figure generation only |
| MATLAB or GNU Octave | optional | `matlab/bisection_latency_model.m` — see §10 |

Only matplotlib is a third-party dependency, and only for figures. The traffic
generator and area model use the standard library alone.

## 5. Installation

```sh
git clone <repository-url>
cd CPEN438-Project13-Group1
python3 -m pip install -r requirements.txt
```

## 6. Compilation

```sh
make            # builds all five executables
make clean      # removes them
```

Compiler flags are `-O2 -Wall -Wextra -std=c11`. The build is warning-free.

Executables produced:

| Binary | Purpose |
|---|---|
| `test_topology` | Topology and routing unit tests (2065 assertions) |
| `verify_topology` | Hand-calculation verification gate |
| `test_simulator` | Simulator unit tests (121 assertions) |
| `run_sweep` | Injection-rate sweep driver |
| `dump_traffic` | C traffic trace dumper, for cross-language verification |

## 7. Execution

### Reproduce every reported result

```sh
make week4
```

One command. It rebuilds the binaries, runs both test suites, verifies the
hand-computed topology metrics, cross-checks the C and Python traffic generators for
bit-exact equality, runs the full 306-run sweep, and regenerates every figure and
table. Takes about a minute. `make week3` is an alias for the same pipeline.

**The pipeline is idempotent** — a second run produces byte-identical output.

### Individual steps

```sh
./test_topology                                   # topology + routing tests
./verify_topology                                 # hand-calculation gate
./test_simulator                                  # simulator tests
./dump_traffic 3000                               # C reference trace
python3 python/gen_datacenter_traffic.py --verify results/raw/traffic_reference_c.csv
./run_sweep                                       # full sweep
python3 python/analyze_network_results.py         # figures and tables
python3 python/area_model.py                      # area and wiring cost
```

### Running an unseen configuration

Node count, topology and seed are command-line parameters; nothing is recompiled.

```sh
./verify_topology --nodes 25 --rows 5 --cols 5
./run_sweep --nodes 25 --rows 5 --cols 5 --seed 4242 --outdir /tmp/out
./run_sweep --topology torus --rates 0.10,0.30 --outdir /tmp/out
python3 python/gen_datacenter_traffic.py --seed 4242 --outdir /tmp/out --summary
```

Run `./run_sweep --help` for the full option list. Defaults come from
`configs/group1_week3_config.txt`; command-line flags override the file.

## 8. Test procedure

| Suite | Assertions | Covers |
|---|---:|---|
| `test_topology` | 2065 | Ring, mesh and torus construction; diameter and bisection against closed forms; exhaustive all-pairs routing minimality, dimension order and link validity |
| `test_simulator` | 121 | Traffic generator distribution; reproducibility; packet conservation; zero loss below saturation; latency monotonicity; deadlock freedom; the innovation and its controls; a 72-node scale test |
| `verify_topology` | 6 | The §G hard gate: hand-computed metrics against the simulator's own construction, for whatever configuration is supplied |

There is no per-test filter; each binary runs its whole suite and returns non-zero on
failure. To isolate one test, comment out the others in that file's `main()`.

Memory safety is checked separately:

```sh
gcc -g -fsanitize=address,undefined -std=c11 -Istudent_implementation \
    -o /tmp/test_asan student_implementation/*.c tests/test_simulator.c -lm
/tmp/test_asan
```

## 9. Expected outputs

A successful `make week4` prints, among other things:

```
ALL TESTS PASSED (2065 checks across 9 test functions)
Topology verification passed (6 checks).
ALL TESTS PASSED (121 checks across 15 test functions)
VERIFIED: the Python and C traffic generators are bit-for-bit identical.
Packet-conservation check: PASSED (0 violation(s) across 306 runs)
```

Headline measurements (uniform-random traffic):

| Topology | Diameter | Bisection | Saturation | Peak throughput |
|---|---:|---:|---:|---:|
| ring(16) | 8 | 2 links | 0.28 | 0.272 |
| mesh(4×4) | 6 | 4 links | 0.70 | 0.724 |
| torus(4×4) | 4 | 8 links | 0.80 | 0.800 |

Rates in flits/node/cycle. Under hot-node-skewed traffic the ordering changes
sharply — see §V-C of the technical report.

Artefacts written:

| Location | Contents |
|---|---|
| `results/raw/` | Sweep CSVs, console log, C reference trace |
| `results/processed/` | Result, model-validation, innovation and area tables |
| `results/figures/` | Eight figures (`fig1`–`fig8`) |
| `traces/` | Seeded traffic traces |

## 10. Known limitations

Stated honestly; expanded in §IX of the technical report.

1. **Single configuration.** All reported results are 16 nodes at seed 1301. The
   simulator runs correctly at other sizes (verified at 25 and 72 nodes) but those
   results are not reported.
2. **Synthetic traffic.** Uniform-random and hot-node-skewed are the two required
   patterns; neither is a real data-centre trace. Real workloads are burstier, which
   generally moves saturation earlier than reported here.
3. **The area model is analytical, not physical.** It rests on tile-pitch wire
   lengths, a *p*² crossbar scaling assumption and a 70/30 wiring-to-logic split,
   calibrated against Dally & Towles's 6.6% figure. Ratios between topologies are
   meaningful; absolute percentages inherit that estimate's error.
4. **No power or thermal model.** Wire length is a weak proxy for dynamic power.
5. **The MATLAB model was not executed for the torus extension.** MATLAB and Octave
   were unavailable on the development machine. The MATLAB torus routing was verified
   against the C implementation across all 256 source–destination pairs by
   transliteration, and the Python mirror of the same model produced every reported
   figure — but the `.m` file itself has not been run since the torus was added.
6. **Flow-control efficiency is unmodelled.** The analytical model predicts
   saturation to within 5–36%; the residual is attributed to finite buffering,
   head-of-line blocking and arbitration loss without being decomposed.
7. **The submitted PDF is single-column.** No LaTeX or pandoc toolchain was available;
   the report source is Markdown and requires the IEEE template for final two-column
   formatting.

## 11. AI-use summary

AI assistance was used during this project and is declared in full in
[`ai_use_declaration/`](ai_use_declaration/), one row per instance, recording tool,
date, prompt summary, output summary, verification method, what was modified, and
which report sections contain AI-assisted material.

In summary: AI was used for concept clarification, error explanation, test-case
suggestion, code review, and drafting and editing documentation. Every AI-assisted
contribution was verified before being committed — C changes against the existing
test suites and the committed Week 3 results, which had to remain byte-identical; the
analytical model against the independent C implementation; and every quantitative
claim in the technical report against the generated result files. No results,
citations or measurements were generated by AI.

Prohibited uses — generating the project wholesale, submitting unexplained generated
code, or fabricating results or citations — were not employed.

## 12. Repository layout

```
student_implementation/   Simulator core
  network_topology.c/.h     Topology construction, metrics, routing
  traffic.c/.h              Seeded traffic generators
  network_sim.c/.h          Flit-level simulator with virtual channels
  sim_config.c/.h           Shared configuration loader
tests/                    Test suites and experiment drivers
python/                   Traffic generator, analysis pipeline, area model
matlab/                   Analytical latency and saturation model
configs/                  Assigned run parameters
traces/  datasets/        Workload inputs (see datasets/README.md)
results/                  raw/ · processed/ · figures/
docs/                     Charter, requirements, hand calculations, architecture
report/                   Technical report
presentation/             Paper review and defence decks
ai_use_declaration/       AI-use declarations
```
