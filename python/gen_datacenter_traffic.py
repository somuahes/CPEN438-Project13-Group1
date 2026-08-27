#!/usr/bin/env python3
"""
gen_datacenter_traffic.py

Project 13 -- Wiring the Data Centre: Interconnection Networks for a
Simulated Accra Warehouse-Scale Cluster.
CPEN 438, Group 1. Week 3 deliverable (Python/Analysis Lead).

Generates the team's seeded warehouse-scale traffic patterns:

  uniform  -- every node is equally likely to be addressed (self excluded)
  hotnode  -- a small set of "database" nodes receives a disproportionate
              share of all packets, modelling a real data-centre skew

The generator is a bit-for-bit reimplementation of the C generator in
student_implementation/traffic.c: the same xorshift64* stream seeded
through the same SplitMix64 initialiser, the same hot-node derivation
from the seed, and the same order of random draws. That means the traffic
this script writes out is exactly the traffic the C simulator injects, so
Python-side analysis and C-side measurement can never silently diverge.

Group 1 configuration: 16 nodes, seed 1301, 2 hot nodes, 50% hot share.

Usage
-----
    python3 gen_datacenter_traffic.py                    # write both traces
    python3 gen_datacenter_traffic.py --count 5000
    python3 gen_datacenter_traffic.py --verify ../results/traffic_reference_c.csv
    python3 gen_datacenter_traffic.py --summary
"""

import argparse
import csv
import os
import sys

MASK64 = (1 << 64) - 1
MASK32 = (1 << 32) - 1

DEFAULT_NODES = 16
DEFAULT_SEED = 1301
DEFAULT_HOT_NODES = 2
DEFAULT_HOT_FRACTION = 0.50


# --------------------------------------------------------------------------
# Deterministic RNG -- mirrors traffic.c exactly
# --------------------------------------------------------------------------

def _splitmix64(x):
    x = (x + 0x9E3779B97F4A7C15) & MASK64
    z = x
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return x, z ^ (z >> 31)


class Rng:
    """xorshift64* with a SplitMix64-derived initial state."""

    def __init__(self, seed):
        _, state = _splitmix64(seed & MASK64)
        self.state = state or 0x9E3779B97F4A7C15

    def next_u32(self):
        x = self.state
        x ^= x >> 12
        x = (x ^ (x << 25)) & MASK64
        x ^= x >> 27
        self.state = x
        return ((x * 0x2545F4914F6CDD1D) & MASK64) >> 32

    def next_double(self):
        return (self.next_u32() >> 8) / 16777216.0

    def next_int(self, n):
        if n <= 1:
            return 0
        return int(self.next_double() * n)


# --------------------------------------------------------------------------
# Traffic patterns -- mirrors traffic.c exactly
# --------------------------------------------------------------------------

def derive_hot_nodes(seed, num_nodes, num_hot):
    """Seed-derived hot-node set. For seed 1301 and N=16 this gives [5, 1]."""
    hot, s = [], seed
    for _ in range(num_hot):
        cand = s % num_nodes
        while cand in hot:
            cand = (cand + 1) % num_nodes
        hot.append(cand)
        s //= num_nodes
    return hot


class Traffic:
    def __init__(self, pattern, num_nodes=DEFAULT_NODES, seed=DEFAULT_SEED,
                 hot_fraction=DEFAULT_HOT_FRACTION, num_hot=DEFAULT_HOT_NODES):
        if pattern not in ("uniform", "hotnode"):
            raise ValueError("pattern must be 'uniform' or 'hotnode'")
        self.pattern = pattern
        self.num_nodes = num_nodes
        self.seed = seed
        self.hot_fraction = hot_fraction
        self.num_hot = num_hot
        self.hot = derive_hot_nodes(seed, num_nodes, num_hot)
        self.rng = Rng(seed)

    def is_hot(self, node):
        return self.pattern == "hotnode" and node in self.hot

    def dest(self, src):
        n = self.num_nodes
        if self.pattern == "hotnode":
            if self.rng.next_double() < self.hot_fraction:
                pick = self.hot[self.rng.next_int(self.num_hot)]
                if pick != src:
                    return pick
                # A hot node drawing itself falls through to the uniform
                # component rather than being discarded, so the offered
                # load per node stays exactly at the requested rate.
        d = self.rng.next_int(n - 1)
        if d >= src:
            d += 1
        return d


# --------------------------------------------------------------------------
# Outputs
# --------------------------------------------------------------------------

def write_trace(path, pattern, count, **kw):
    t = Traffic(pattern, **kw)
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["pattern", "index", "src", "dst"])
        for i in range(count):
            src = i % t.num_nodes
            w.writerow([pattern, i, src, t.dest(src)])
    return t


def summarise(pattern, count=200000, **kw):
    t = Traffic(pattern, **kw)
    counts = [0] * t.num_nodes
    for i in range(count):
        counts[t.dest(i % t.num_nodes)] += 1
    print(f"\n--- {pattern} pattern, seed {t.seed}, {t.num_nodes} nodes, "
          f"{count} packets ---")
    if t.pattern == "hotnode":
        print(f"hot (database) nodes : {t.hot}")
        hot_share = sum(counts[h] for h in t.hot) / count
        print(f"share addressed to hot nodes : {hot_share:.4f}")
    print("destination distribution (% of all packets):")
    for i in range(0, t.num_nodes, 8):
        row = "  " + "  ".join(f"n{j:02d}={100.0*counts[j]/count:5.2f}"
                               for j in range(i, min(i + 8, t.num_nodes)))
        print(row)


def verify(reference_csv, count=None, **kw):
    """Check that this script reproduces the C generator's stream exactly."""
    if not os.path.exists(reference_csv):
        print(f"reference file not found: {reference_csv}")
        return 1
    rows = list(csv.DictReader(open(reference_csv)))
    by_pattern = {}
    for r in rows:
        by_pattern.setdefault(r["pattern"], []).append((int(r["src"]), int(r["dst"])))

    total_mismatch = 0
    for pattern, pairs in by_pattern.items():
        t = Traffic(pattern, **kw)
        n = len(pairs) if count is None else min(count, len(pairs))
        mismatch = 0
        for i in range(n):
            src, ref_dst = pairs[i]
            if t.dest(src) != ref_dst:
                mismatch += 1
        total_mismatch += mismatch
        print(f"{pattern:>8}: {n - mismatch}/{n} destinations match the C generator"
              f"{'' if mismatch == 0 else f'  ({mismatch} MISMATCHES)'}")
    if total_mismatch == 0:
        print("VERIFIED: the Python and C traffic generators are bit-for-bit identical.")
        return 0
    print("FAILED: the Python and C traffic generators diverge.")
    return 1


def main():
    ap = argparse.ArgumentParser(description="Group 1 seeded data-centre traffic generator")
    ap.add_argument("--nodes", type=int, default=DEFAULT_NODES)
    ap.add_argument("--seed", type=int, default=DEFAULT_SEED)
    ap.add_argument("--num-hot", type=int, default=DEFAULT_HOT_NODES)
    ap.add_argument("--hot-fraction", type=float, default=DEFAULT_HOT_FRACTION)
    ap.add_argument("--count", type=int, default=2000, help="packets per trace")
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(__file__), "..", "results"))
    ap.add_argument("--verify", metavar="CSV", help="compare against the C reference trace")
    ap.add_argument("--summary", action="store_true", help="print destination statistics")
    args = ap.parse_args()

    kw = dict(num_nodes=args.nodes, seed=args.seed,
              hot_fraction=args.hot_fraction, num_hot=args.num_hot)

    if args.verify:
        sys.exit(verify(args.verify, **kw))

    os.makedirs(args.outdir, exist_ok=True)
    for pattern in ("uniform", "hotnode"):
        path = os.path.join(args.outdir, f"traffic_{pattern}_seed{args.seed}.csv")
        t = write_trace(path, pattern, args.count, **kw)
        note = f"  hot nodes = {t.hot}" if pattern == "hotnode" else ""
        print(f"wrote {args.count} packets -> {path}{note}")

    if args.summary:
        for pattern in ("uniform", "hotnode"):
            summarise(pattern, **kw)


if __name__ == "__main__":
    main()
