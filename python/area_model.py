#!/usr/bin/env python3
"""
area_model.py

Project 13 -- Wiring the Data Centre.
CPEN 438, Group 1. Week 4 deliverable (Level-3 Advanced extension).

Simplified area and wiring-cost accounting for the three topologies, in the
style of the estimate Dally & Towles give in "Route Packets, Not Wires"
(DAC '01), which puts the network area of a 16-tile folded torus at about
6.6 % of chip area. That paper's configuration is the same one built here --
16 tiles, 2D folded torus -- so it is used directly as the calibration point,
and the ring and mesh are scaled against it by their own resource counts.

Three cost terms are estimated, each first-order:

  wiring   total interconnect length, in node pitches, from an explicit
           placement of the nodes on a 4 x 4 tile grid. Wire *area* is
           proportional to total length x channel width.
  crossbar each router's switch is a p x p crossbar for p ports
           (topology degree + 1 local), so its area grows as p^2.
  buffers  input buffering is ports x VCs x buffer depth flits.

The point of the exercise is not an absolute number -- that would need a
real floorplan and process data -- but the *ratios* between topologies, and
what the torus's measured throughput advantage actually costs.

Usage:
    python3 area_model.py [--outdir DIR]
"""

import argparse
import csv
import os

N = 16
ROWS, COLS = 4, 4
CHANNEL_BITS = 32          # flit width carried by one physical channel
VC_BUF_FLITS = 4

# Dally & Towles's reported network-area overhead for their 16-tile folded
# torus, used as the anchor for the relative estimates below.
DALLY_TORUS_OVERHEAD = 0.066


# --------------------------------------------------------------------------
# Placement: every topology is laid out on the same 4 x 4 tile grid, so the
# wire lengths are directly comparable.
# --------------------------------------------------------------------------

def fold_axis(k):
    """Folded ordering along one axis: node i sits at position 2i in the
    first half and 2(k-i)-1 in the second. This interleaves the nodes so
    that the end-around link becomes short -- every link then spans at most
    2 tile pitches, for any k, instead of one link spanning k-1."""
    return {i: (2 * i if i < k / 2 else 2 * (k - i) - 1) for i in range(k)}


def place_grid(rows, cols):
    """Mesh: node (r,c) sits at grid point (c,r)."""
    return {r * cols + c: (c, r) for r in range(rows) for c in range(cols)}


def place_folded(rows, cols):
    """Torus: the folded ordering is applied independently to each axis."""
    fr, fc = fold_axis(rows), fold_axis(cols)
    return {r * cols + c: (fc[c], fr[r]) for r in range(rows) for c in range(cols)}


def place_serpentine(rows, cols):
    """Ring: a Hamiltonian cycle through the same grid, boustrophedon order,
    which is how a ring is actually embedded on a 2D die."""
    pos = {}
    i = 0
    for r in range(rows):
        cs = range(cols) if r % 2 == 0 else range(cols - 1, -1, -1)
        for c in cs:
            pos[i] = (c, r)
            i += 1
    return pos


# --------------------------------------------------------------------------
# Edge sets
# --------------------------------------------------------------------------

def edges_ring(n):
    return [(i, (i + 1) % n) for i in range(n)]


def edges_mesh(rows, cols):
    e = []
    for r in range(rows):
        for c in range(cols):
            i = r * cols + c
            if c + 1 < cols:
                e.append((i, r * cols + c + 1))
            if r + 1 < rows:
                e.append((i, (r + 1) * cols + c))
    return e


def edges_torus(rows, cols):
    e = []
    for r in range(rows):
        for c in range(cols):
            i = r * cols + c
            e.append((i, r * cols + (c + 1) % cols))
            e.append((i, ((r + 1) % rows) * cols + c))
    return e


# --------------------------------------------------------------------------
# Cost model
# --------------------------------------------------------------------------

def manhattan(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def topology_cost(name, edges, pos, degree, vcs, usable_vcs):
    spans = [manhattan(pos[a], pos[b]) for a, b in edges]
    ports = degree + 1                      # neighbours + local injection
    return {
        "topology": name,
        "links": len(edges),
        "degree": degree,
        "wire_pitches": sum(spans),
        "max_span": max(spans),
        "wire_bit_pitches": sum(spans) * CHANNEL_BITS,
        "ports": ports,
        "crossbar_units": N * ports * ports,
        "vcs": vcs,
        "usable_vcs": usable_vcs,
        "buffer_flits": N * ports * vcs * VC_BUF_FLITS,
    }


def build():
    """The VC budgets are the ones the sweep actually reports. The mesh needs
    no dateline, so both its VCs are usable; the ring and torus must reserve
    half of theirs for deadlock freedom. The torus is costed at 4 VCs, the
    configuration in which it has the same 2 usable channels as the mesh --
    the headline comparison -- so that its advantage is priced honestly."""
    return [
        topology_cost("ring",  edges_ring(N),         place_serpentine(ROWS, COLS), 2, 2, 1),
        topology_cost("mesh",  edges_mesh(ROWS, COLS), place_grid(ROWS, COLS),      4, 2, 2),
        topology_cost("torus", edges_torus(ROWS, COLS), place_folded(ROWS, COLS),   4, 4, 2),
    ]


def add_overhead(costs):
    """Scale to a chip-area percentage using Dally & Towles's 6.6 % for the
    16-tile folded torus as the anchor. Network area is taken as wiring plus
    router (crossbar + buffers), each weighted by its share in the torus."""
    torus = next(c for c in costs if c["topology"] == "torus")

    for c in costs:
        wire_ratio = c["wire_bit_pitches"] / torus["wire_bit_pitches"]
        logic_ratio = ((c["crossbar_units"] + c["buffer_flits"]) /
                       (torus["crossbar_units"] + torus["buffer_flits"]))
        # Dally & Towles attribute the bulk of network area to wiring; a
        # 70/30 wiring:logic split is assumed and stated as an assumption.
        c["area_ratio_vs_torus"] = 0.70 * wire_ratio + 0.30 * logic_ratio
        c["chip_area_overhead"] = DALLY_TORUS_OVERHEAD * c["area_ratio_vs_torus"]
    return costs


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--outdir", default=os.path.join(here, "..", "results"))
    args = ap.parse_args()
    outdir = os.path.abspath(args.outdir)
    os.makedirs(outdir, exist_ok=True)

    costs = add_overhead(build())

    print("=== Area and wiring-cost model (16 nodes, 4x4 tile grid) ===\n")
    print(f"{'topology':9s} {'links':>6s} {'deg':>4s} {'wire':>6s} {'maxsp':>6s} "
          f"{'ports':>6s} {'xbar':>6s} {'VCs':>5s} {'buf':>6s} {'area%':>7s}")
    for c in costs:
        print(f"{c['topology']:9s} {c['links']:6d} {c['degree']:4d} "
              f"{c['wire_pitches']:6d} {c['max_span']:6d} {c['ports']:6d} "
              f"{c['crossbar_units']:6d} {c['vcs']:5d} {c['buffer_flits']:6d} "
              f"{c['chip_area_overhead'] * 100:6.2f}%")

    mesh = next(c for c in costs if c["topology"] == "mesh")
    torus = next(c for c in costs if c["topology"] == "torus")
    print(f"\nTorus vs mesh: {torus['wire_pitches'] / mesh['wire_pitches']:.2f}x wire length, "
          f"{torus['buffer_flits'] / mesh['buffer_flits']:.2f}x buffering, "
          f"{torus['chip_area_overhead'] / mesh['chip_area_overhead']:.2f}x network area.")
    print(f"Folding caps the longest torus wire at {torus['max_span']} tile pitches; "
          f"an unfolded torus would need {COLS - 1}.")

    path = os.path.join(outdir, "week4_area_model.csv")
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        cols = ["topology", "links", "degree", "wire_pitches", "max_span",
                "ports", "crossbar_units", "vcs", "usable_vcs", "buffer_flits",
                "area_ratio_vs_torus", "chip_area_overhead"]
        w.writerow(cols)
        for c in costs:
            w.writerow([c[k] if not isinstance(c[k], float) else f"{c[k]:.4f}"
                        for k in cols])
    print(f"\nWritten to {path}")


if __name__ == "__main__":
    main()
