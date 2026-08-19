# Project 13 — Technical Paper Review

**Course:** CPEN 438 — Advanced Computer Architecture  
**Team:** Group 1  
**Project:** Project 13

## Required Papers

### Paper 1

William J. Dally and Brian Towles,  
"Route Packets, Not Wires: On-Chip Interconnection Networks,"  
Proceedings of the 38th Design Automation Conference (DAC '01),  
Las Vegas, Nevada, June 2001, pp. 684–689.

DOI: 10.1109/DAC.2001.935594

### Paper 2

Hadi Esmaeilzadeh, Emily Blem, Renée St. Amant,
Karthikeyan Sankaralingam, and Doug Burger,

"Dark Silicon and the End of Multicore Scaling,"

Proceedings of the 38th Annual International Symposium
on Computer Architecture (ISCA '11), pp. 365–376.

DOI: 10.1145/2000064.2000108


# Planned Presentation Structure

## Slide 1 — Title and Paper Citations

- Course and project
- Group 1
- Full citations of both papers
- Brief introduction to why the papers are relevant to Project 13

## Slide 2 — Problem Addressed

- Problem addressed by Dally and Towles
- Problem addressed by Esmaeilzadeh et al.
- Relationship between the two architectural problems

## Slide 3 — Motivation

- Why interconnection-network design matters
- Why scaling constraints make efficient architectural design important

## Slide 4 — Paper 1 Method / Approach

- Approach used by Dally and Towles
- Network/interconnect design ideas considered
- Important assumptions

## Slide 5 — Paper 1 Architecture / Diagrams

- Important interconnection-network diagram from the paper
- Explanation of packet-switched structured networks
- Relevant topology/wiring concepts

## Slide 6 — Paper 1 Results

- Experimental or analytical setup
- Metrics
- Important quantitative results
- Area/bandwidth trade-offs

## Slide 7 — Paper 2 Method and Results

- Scaling problem investigated
- Method used
- Important quantitative results
- Power/area implications

## Slide 8 — Critical Evaluation

- Strengths of the papers
- Limitations
- Assumptions
- Relevance to modern systems
- What the papers do not directly address

## Slide 9 — Connection to Project 13

Our project will investigate the ideas experimentally by comparing:

- 16-node ring
- 4 x 4 mesh
- uniform-random traffic
- hot-node-skewed traffic
- average packet latency
- achieved throughput
- saturation injection rate
- diameter
- bisection bandwidth

## Slide 10 — What Group 1 Will Reproduce or Extend

Group 1 will use a flit-level simulator to investigate how
structured network topology affects communication performance.

The initial hypothesis is that the 4 x 4 mesh will sustain a
higher injection rate before saturation than the 16-node ring
because it has:

- smaller diameter: 6 hops compared with 8 hops
- larger bisection bandwidth: 4 links compared with 2 links

The measured simulation results will later be compared with
this theoretical prediction.

## Questions

Prepare for questions about:

- network diameter
- bisection bandwidth
- packet switching
- flits
- ring versus mesh
- wiring cost
- saturation
- latency
- throughput
- power and area constraints