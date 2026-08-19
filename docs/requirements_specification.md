# Requirements Specification

**Course:** CPEN 438 — Advanced Computer Architecture
**Team:** Group 1
**Project:** Project 13
**Project Title:** *Wiring the Data Centre: Interconnection Networks for a Simulated Accra Warehouse-Scale Cluster*

## 1. Purpose

This document defines the functional and non-functional requirements for the interconnection-network simulator developed for Project 13. The simulator will be used to compare the performance of a 16-node ring and a 4 × 4 mesh under different traffic conditions.

## 2. Functional Requirements

### FR1 — Network Configuration

The simulator shall support a configurable number of network nodes. The baseline configuration for Group 1 shall use 16 nodes.

### FR2 — Ring Topology

The simulator shall support a 16-node ring topology in which every node is connected to two neighbouring nodes.

### FR3 — 2D Mesh Topology

The simulator shall support a 4 × 4 two-dimensional mesh containing 16 nodes connected through horizontal and vertical links.

### FR4 — Packet Generation

The simulator shall generate packets containing a source node and destination node according to the selected traffic pattern.

### FR5 — Flit-Level Simulation

The simulator shall represent packets as flits and model their movement through the network on a hop-by-hop basis.

### FR6 — Ring Routing

The simulator shall provide shortest-path routing for packets travelling through the ring topology.

### FR7 — Mesh Routing

The simulator shall implement deterministic dimension-order routing for packets travelling through the 2D mesh.

### FR8 — Uniform-Random Traffic

The simulator shall support a uniform-random traffic pattern in which destination nodes are selected across the network.

### FR9 — Hot-Node-Skewed Traffic

The simulator shall support a hot-node-skewed traffic pattern in which selected database or cache nodes receive a larger proportion of the generated traffic.

### FR10 — Configurable Injection Rate

The simulator shall allow the packet injection rate to be varied so that network behaviour can be evaluated at different offered loads.

### FR11 — Router and Link Behaviour

The simulator shall model packet/flit movement through routers, queues, and network links with an explicit router pipeline delay.

### FR12 — Latency Measurement

The simulator shall measure the latency of delivered packets and calculate average packet latency for each experiment.

### FR13 — Throughput Measurement

The simulator shall calculate the achieved throughput of the network for each experiment.

### FR14 — Packet Delivery and Loss Tracking

The simulator shall record the number of injected packets, delivered packets, and any packets that are lost or remain undelivered.

### FR15 — Saturation Analysis

The simulator shall support experiments over increasing injection rates to determine the approximate saturation injection rate of each topology.

### FR16 — Result Output

The simulator shall generate experimental results in a form that can be processed and analysed using Python and MATLAB.

## 3. Non-Functional Requirements

### NFR1 — Correctness

The simulator shall correctly route packets from their source nodes to their intended destination nodes. Below the network saturation point, there should be no unexplained packet loss.

### NFR2 — Reproducibility

Experiments shall be reproducible. Each experiment shall record the node count, topology, routing method, traffic pattern, injection rate, and random seed.

### NFR3 — Configurability

Important experimental parameters shall be configurable rather than permanently hard-coded into the simulator.

### NFR4 — Deterministic Baseline Routing

The baseline 2D mesh shall use deterministic dimension-order routing so that routing behaviour can be reproduced and validated.

### NFR5 — Validation

The network topology shall be validated using manually calculated diameter and bisection bandwidth before full experimental results are accepted.

### NFR6 — Fair Comparison

The ring and mesh shall use the same total number of nodes and comparable traffic conditions when their performance is compared.

### NFR7 — Result Integrity

Latency results shall not hide packets that fail to arrive. Packet-delivery and packet-loss counts shall be reported together with performance measurements.

### NFR8 — Documentation

The implementation, experimental configuration, testing procedure, results, limitations, and use of AI tools shall be documented throughout the project.

## 4. Baseline System Configuration

| Parameter                    | Group 1 Configuration   |
| ---------------------------- | ----------------------- |
| Number of nodes              | 16                      |
| Ring                         | 16-node ring            |
| Mesh                         | 4 × 4                   |
| Ring routing                 | Shortest-path routing   |
| Mesh routing                 | Dimension-order routing |
| Traffic pattern 1            | Uniform-random          |
| Traffic pattern 2            | Hot-node-skewed         |
| Random seed                  | 1301                    |
| Main implementation language | C/C++                   |
| Analysis                     | Python                  |
| Analytical modelling         | MATLAB                  |
| Topology visualisation       | Graphviz                |

## 5. Required Performance Metrics

The following measurements shall be collected during the experimental stage:

* Average packet latency
* Achieved throughput
* Packet injection rate
* Saturation injection rate
* Number of packets injected
* Number of packets delivered
* Number of packets lost or undelivered
* Network diameter
* Bisection bandwidth

## 6. Week 1 Validation Targets

Before moving to the full experimental stage, the following theoretical values shall be manually verified for the selected 16-node configuration:

**16-node Ring**

* Diameter: 8 hops
* Bisection bandwidth: 2 links

**4 × 4 Mesh**

* Diameter: 6 hops
* Bisection bandwidth: 4 links

These theoretical values will later be compared with the implemented network structure to verify that the topology has been constructed correctly.
