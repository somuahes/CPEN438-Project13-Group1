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

---

## 6. 4 × 4 Folded Torus (Week 4, Level-3 Advanced extension)

The folded torus is the Level-3 Advanced extension. It is added in Week 4 and
these values are hand-computed here **before** the topology is implemented, so
that the simulator's own construction code can be checked against them, exactly
as was done for the ring and the mesh in Week 2.

A 4 × 4 torus is a 4-ary 2-cube: it is the 4 × 4 mesh with wraparound links
added, so that every row is a 4-node ring and every column is a 4-node ring.

### 6.1 Node Degree and Link Count

Every node has one neighbour in each of four directions (up, down, left,
right), and the wraparound means this is true even at the edges. So:

* Maximum node degree = **4**
* Minimum node degree = **4** (uniform, unlike the mesh)

Each of the 16 nodes has 4 ports, and every link is shared by 2 nodes:

```
links = 16 × 4 / 2 = 32 bidirectional links
```

Compared with 24 for the 4 × 4 mesh and 16 for the 16-node ring.

### 6.2 Torus Diameter

Distance is the sum of the distance travelled in each dimension, and each
dimension is an independent 4-node ring.

In a 4-node ring, the distances from any node to the other three are 1, 2 and 1,
so the greatest distance within one dimension is:

```
floor(4 / 2) = 2 hops
```

The worst-case pair is therefore the one that is maximally distant in both
dimensions at once:

```
diameter = 2 (row dimension) + 2 (column dimension) = 4 hops
```

Worked check from node (0, 0), using row-major ids `id = row × 4 + col`:

| Destination | Row distance | Col distance | Total |
| ----------- | -----------: | -----------: | ----: |
| (0, 2) = 2  |            0 |            2 |     2 |
| (2, 0) = 8  |            2 |            0 |     2 |
| (2, 2) = 10 |            2 |            2 | **4** |
| (3, 3) = 15 |            1 |            1 |     2 |

Note that (3, 3), which is the *farthest* node in the mesh at 6 hops, is only
2 hops away in the torus: one wraparound step in each dimension. The single
most distant node is (2, 2), at 4 hops.

**Torus diameter = 4 hops**, against 6 for the mesh and 8 for the ring.

### 6.3 Torus Bisection Bandwidth

Cut the network into two halves of 8 nodes by splitting between column 1 and
column 2. Two sets of links cross that cut:

* the four direct links (r, 1) – (r, 2), one per row, and
* the four wraparound links (r, 3) – (r, 0), one per row.

```
crossing links = 4 (direct) + 4 (wraparound) = 8 links
```

The horizontal cut is symmetric and also crosses 8 links. In general, for an
R × C torus a vertical cut crosses 2R links and a horizontal cut 2C links, so:

```
bisection bandwidth = 2 × min(R, C)
```

For R = C = 4 this gives **8 links**, exactly double the 4 × 4 mesh's 4 links.
This is the structural reason the torus is expected to sustain a higher
injection rate: the wraparound both halves the worst-case distance and doubles
the number of links crossing the midpoint.

### 6.4 Average Hop Count

Within one 4-node ring, the distances from a node to all four nodes (including
itself) are 0, 1, 2, 1, averaging 1 hop per dimension. Summing over the 16
destinations from a fixed source:

```
total = 4 × (0+1+2+1) + 4 × (0+1+2+1) = 16 + 16 = 32 hops
```

Excluding the source itself, the average over the other 15 nodes is:

```
32 / 15 = 2.133 hops
```

against 2.667 for the mesh and 4.267 for the ring.

### 6.5 What "Folded" Means

A folded torus is **logically identical** to a plain torus: same adjacency,
same diameter, same bisection bandwidth. Folding is a physical layout
technique, not a change of topology. Laying the nodes out in their natural
order leaves one very long end-around wire per ring; interleaving the node
positions instead removes that long wire, at the cost of making every link span
two node pitches rather than one.

This distinction matters only for the area and wiring-cost accounting required
by the Level-3 Advanced task, where it is compared against the ~6.6 % area
overhead reported by Dally & Towles. It does not affect any of the values in
§6.1–§6.4, all of which the simulator must reproduce.

### 6.6 Updated Comparison

| Property            | Ring (16) | Mesh (4 × 4) | Torus (4 × 4) |
| ------------------- | --------: | -----------: | ------------: |
| Number of nodes     |        16 |           16 |            16 |
| Diameter            |    8 hops |       6 hops |    **4 hops** |
| Bisection bandwidth |   2 links |      4 links |   **8 links** |
| Maximum node degree |         2 |            4 |             4 |
| Bidirectional links |        16 |           24 |            32 |
| Average hop count   |     4.267 |        2.667 |     **2.133** |

### 6.7 Prediction Before Running

The torus has the smallest diameter and the largest bisection bandwidth of the
three topologies, so under uniform-random traffic it is expected to:

* show the lowest zero-load latency of the three;
* saturate at a **higher** injection rate than the 4 × 4 mesh, which itself
  saturates well above the ring;
* buy that advantage at the cost of 8 more links than the mesh and a uniform
  degree of 4 at every node, including the corners.

Under hot-node-skewed traffic the torus advantage is expected to **shrink
sharply**. The Week 3 measurements already show the mesh's advantage over the
ring collapsing under hot-node traffic, because the binding constraint stops
being the bisection cut and becomes the single unit-bandwidth channel into each
hot node — a constraint every topology shares. The torus adds bisection
bandwidth, which is not what is scarce in that regime. It should therefore look
much like the mesh under hot-node traffic despite being clearly better under
uniform traffic.

These predictions are recorded here *before* the extension is implemented, and
are checked against the measured results in the Week 4 report.
