# Topology Hand Calculations

**Course:** CPEN 438 — Advanced Computer Architecture
**Team:** Group 1
**Project:** Project 13
**Selected Node Count:** 16

## 1. Purpose

Before running traffic experiments, the theoretical properties of the selected topologies are calculated manually. These values will later be used to verify that the simulator has constructed the ring and mesh correctly.

The two baseline topologies are:

* 16-node ring
* 4 × 4 mesh

---

## 2. 16-Node Ring

### 2.1 Ring Diameter

The network diameter is the maximum number of hops in the shortest path between any two nodes.

For an even-numbered ring:

[
Diameter = \frac{N}{2}
]

where:

[
N = 16
]

Therefore:

[
Diameter = \frac{16}{2}
]

[
Diameter = 8
]

So the diameter of the 16-node ring is:

[
\boxed{8\ hops}
]

An example of the longest shortest path is from Node 0 to Node 8:

[
0 \rightarrow 1 \rightarrow 2 \rightarrow 3 \rightarrow 4 \rightarrow 5 \rightarrow 6 \rightarrow 7 \rightarrow 8
]

This path contains 8 hops.

### 2.2 Ring Bisection Bandwidth

Bisection bandwidth is the minimum total link capacity that must be cut to divide the network into two equal halves.

The 16-node ring can be divided into:

* Nodes 0–7
* Nodes 8–15

To separate these two halves, two links must be cut.

Therefore:

[
\boxed{Bisection\ Bandwidth = 2\ links}
]

If each link has bandwidth (B), then the total bisection bandwidth is:

[
2B
]

---

## 3. 4 × 4 Mesh

### 3.1 Mesh Diameter

The diameter of a two-dimensional mesh is determined by the maximum Manhattan distance between two nodes.

For a mesh with (R) rows and (C) columns:

[
Diameter = (R-1) + (C-1)
]

For the selected 4 × 4 mesh:

[
R = 4
]

[
C = 4
]

Therefore:

[
Diameter = (4-1) + (4-1)
]

[
Diameter = 3 + 3
]

[
Diameter = 6
]

So the diameter of the 4 × 4 mesh is:

[
\boxed{6\ hops}
]

An example of the longest shortest path is from Node 0 to Node 15.

One valid route is:

[
0 \rightarrow 1 \rightarrow 2 \rightarrow 3 \rightarrow 7 \rightarrow 11 \rightarrow 15
]

This path contains 6 hops.

### 3.2 Mesh Bisection Bandwidth

The 4 × 4 mesh can be divided vertically into two equal halves.

The links crossing the centre are:

* Node 1 ↔ Node 2
* Node 5 ↔ Node 6
* Node 9 ↔ Node 10
* Node 13 ↔ Node 14

Therefore, four links must be cut.

So:

[
\boxed{Bisection\ Bandwidth = 4\ links}
]

If each link has bandwidth (B), then:

[
Bisection\ Bandwidth = 4B
]

---

## 4. Comparison

| Metric              | 16-Node Ring | 4 × 4 Mesh |
| ------------------- | -----------: | ---------: |
| Number of nodes     |           16 |         16 |
| Diameter            |       8 hops |     6 hops |
| Bisection bandwidth |      2 links |    4 links |
| Maximum node degree |            2 |          4 |

## 5. Initial Prediction

The 4 × 4 mesh has a smaller diameter and a larger bisection bandwidth than the 16-node ring.

Therefore, before running the simulator, the mesh is expected to:

* provide lower average communication latency;
* carry more traffic before reaching saturation;
* perform better when traffic is distributed across many nodes;
* handle high communication demand more effectively than the ring.

The ring is expected to saturate earlier because fewer links connect opposite halves of the network.

These predictions will later be checked against the measured latency and throughput results from the simulator.
