# Project 13 Paper Review Notes

## Paper 1
William J. Dally and Brian Towles, “Route Packets, Not Wires: On-Chip Interconnection Networks.”

Main idea:
The paper proposes replacing ad-hoc global on-chip wiring with a structured packet-switched interconnection network.

Key points:
- Modules communicate by sending packets instead of using dedicated wires.
- Structured wiring gives more predictable electrical behaviour.
- The proposed network supports modular design.
- The authors estimate the network area overhead as about 6.6%.
- Their example uses 16 tiles and a 2D folded torus topology.
- The paper discusses flits, virtual channels, routing, buffering, latency, bandwidth, power, and topology trade-offs.

Connection to Project 13:
Our project applies this idea by comparing structured network topologies: a 16-node ring and a 4×4 mesh. We will measure latency, throughput, saturation point, diameter and bisection bandwidth.

## Paper 2
Hadi Esmaeilzadeh et al., “Dark Silicon and the End of Multicore Scaling.”

Main idea:
The paper studies how power limits prevent future processors from using all available transistors at the same time.

Key points:
- Multicore scaling is limited by power, not just transistor count.
- The authors combine device scaling, core scaling and multicore scaling models.
- They study CPU-like and GPU-like multicore organisations.
- The paper reports that at 22 nm, about 21% of a fixed-size chip must be powered off.
- At 8 nm, more than 50% must be powered off.
- It predicts only about 7.9× average speedup through 2024, far below ideal scaling.

Connection to Project 13:
This paper helps us explain why topology choice is not only about performance. A topology with more links may improve throughput, but extra wiring also has cost, area and power implications.