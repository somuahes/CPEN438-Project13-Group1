CPEN 438 Advanced Computer Architecture

Project Charter

Group: Group 1
Project Number: Project 13
Project Title: Wiring the Data Centre: Interconnection Networks for a Simulated Accra Warehouse-Scale Cluster

1. Team Members

No.	Name	Student ID
1	Bernardine Adusei-Okrah	11123762
2	Peggy Esinam Somuah	11049523
3	Vanessa Esinam Ayertey	11264010
4	Muhammad Nurul Haqq Abdul Basit Munagah	11117536

2. Project Background

Modern data-centre and multiprocessor systems depend heavily on the network that connects their computing nodes. The choice of interconnection topology affects communication latency, throughput, scalability, wiring cost, and the ability of the system to handle increasing traffic.

This project focuses on comparing structured interconnection-network topologies for a simulated warehouse-scale cluster representing a small Accra data centre. The system will be evaluated under both uniform-random traffic and hot-node-skewed traffic, where a small number of database or cache nodes receive a larger share of requests.

3. Problem Statement

Simple interconnection structures such as shared buses can become bottlenecks as the number of computing nodes increases, while highly connected designs may become expensive in terms of wiring and implementation complexity.

The project therefore aims to investigate how the choice of topology and routing method affects network performance. A flit-level simulator will be used to compare a 16-node ring topology and a 4 × 4 mesh topology using measurable architectural metrics such as average packet latency, throughput, saturation injection rate, network diameter, and bisection bandwidth.

4. Main Objective

To design, implement, and evaluate a simulated interconnection network for a 16-node Accra warehouse-scale computing cluster and quantitatively compare the performance of different network topologies.

5. Specific Objectives

1. Develop a flit-level network simulator.
2. Implement a 16-node ring topology.
3. Implement a 4 × 4 two-dimensional mesh topology.
4. Implement shortest-path routing for the ring.
5. Implement dimension-order routing for the mesh.
6. Generate uniform-random traffic.
7. Generate hot-node-skewed traffic.
8. Measure average packet latency.
9. Measure achieved network throughput.
10. Determine the saturation injection rate of each topology.
11. Calculate and compare network diameter.
12. Calculate and compare bisection bandwidth.
13. Analyse how traffic pattern affects the performance of each topology.
14. Develop and evaluate an additional innovation or extension.

6. Project Scope

The baseline project will focus on two interconnection topologies:

* 16-node ring
* 4 × 4 two-dimensional mesh

The project will evaluate both topologies using:

* uniform-random traffic;
* hot-node-skewed traffic;
* configurable packet injection rates;
* deterministic routing;
* flit-level packet movement;
* latency measurement;
* throughput measurement;
* packet-delivery and packet-loss tracking;
* saturation-point analysis;
* diameter and bisection-bandwidth analysis.

A folded-torus topology, adaptive routing, or another approved enhancement may later be considered as an extension.

7. Selected Baseline Configuration

Node Count: 16
Ring Configuration: 16 nodes
Mesh Configuration: 4 × 4 nodes
Ring Routing: Shortest-path routing
Mesh Routing: Dimension-order routing
Traffic Patterns: Uniform-random and hot-node-skewed
Traffic Seed: 1301

8. Main Tools

The project will use:

* C/C++ for the flit-level simulation core, topology construction, and routing algorithms.
* Python for traffic generation, experiment automation, data analysis, and plotting.
* Graphviz for topology visualisation.
* MATLAB for analytical modelling of latency, throughput, diameter, bisection bandwidth, and saturation behaviour.

9. Expected Outputs

At the end of the project, Group 1 expects to produce:

* a working flit-level network simulator;
* a functioning ring topology;
* a functioning 2D mesh topology;
* routing implementations for both topologies;
* uniform-random and hot-node-skewed traffic generators;
* latency and throughput measurements;
* saturation-rate comparisons;
* packet-loss accounting;
* topology diagrams;
* diameter and bisection-bandwidth calculations;
* experimental plots and tables;
* an IEEE-style technical report;
* a final project presentation and live demonstration.

10. Success Criteria

The project will be considered successful if:

* every injected packet is correctly delivered below the saturation point;
* the ring and mesh topologies are correctly constructed;
* the routing algorithms behave as expected;
* the simulator produces repeatable results using fixed configuration parameters and traffic seeds;
* measured latency and throughput are analysed quantitatively;
* observed saturation behaviour is explained using the theoretical diameter and bisection bandwidth of each topology.