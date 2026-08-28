#!/usr/bin/env python3
"""
analyze_network_results.py

Project 13 -- Wiring the Data Centre: Interconnection Networks for a
Simulated Accra Warehouse-Scale Cluster.
CPEN 438, Group 1. Week 3 deliverable (Python/Analysis Lead).

Reads the flit-level simulator's sweep output and produces every figure
and table the Week 3 schedule calls for:

  Fig 1  latency vs injection rate, ring vs mesh, uniform traffic
  Fig 2  latency vs injection rate, ring vs mesh, hot-node traffic
  Fig 3  accepted throughput vs offered load, all four baseline cases
  Fig 4  analytical model vs measured latency (validation, 2x2)
  Fig 5  background-class latency: baseline vs the two controls vs the
         class-isolation innovation, under hot-node traffic
  Fig 6  explicit packet-loss accounting vs offered load
  Fig 7  ideal vs achieved saturation injection rate

  week3_results_table.csv     diameter, bisection bandwidth and measured
                              saturation point per topology and pattern
  week3_model_vs_measured.csv analytical vs measured validation table
  week3_innovation_table.csv  the innovation ablation at the knee load

The analytical model implemented in `analytical_model()` is the same model
as matlab/bisection_latency_model.m -- same channel-load walk, same
M/D/1 source-queue term. It is duplicated here so the figures can be
regenerated on a machine without a MATLAB licence; the MATLAB file remains
the submitted analytical deliverable and the two agree numerically.

Usage:
    python3 analyze_network_results.py
    python3 analyze_network_results.py --results ../results
"""

import argparse
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# --------------------------------------------------------------------------
# Group 1 configuration
# --------------------------------------------------------------------------
N = 16
ROWS, COLS = 4, 4
PACKET_FLITS = 4
EJECT_BW = 4
HOT = (5, 1)
HOT_FRACTION = 0.50
ROUTER_DELAY = 1

STATIC = {                       # ring/mesh verified in Week 2, torus in Week 4
    "ring":  {"diameter": 8, "bisection": 2, "links": 16, "degree": 2},
    "mesh":  {"diameter": 6, "bisection": 4, "links": 24, "degree": 4},
    "torus": {"diameter": 4, "bisection": 8, "links": 32, "degree": 4},
}

TOPO_LABEL = {"ring": "ring(16)", "mesh": "mesh(4x4)", "torus": "torus(4x4)"}

# Headline comparison: equal USABLE virtual channels per packet, which is what
# isolates topology from flow-control resources. The mesh needs no dateline
# (XY routing on a mesh is already acyclic) so its 2 VCs are both usable. The
# ring and torus must reserve half their VCs for the dateline rule, so the
# torus is given 4 to reach the same 2 usable channels. The equal-TOTAL-budget
# view -- where deadlock freedom is charged to the topology -- is reported
# separately as the cost accounting (fig 8).
HEADLINE_MODE = {"ring": "baseline_vc2", "mesh": "baseline_vc2",
                 "torus": "vc4_plain"}
USABLE_VC_NOTE = {"ring": "2 VCs / 1 usable", "mesh": "2 VCs / 2 usable",
                  "torus": "4 VCs / 2 usable"}
TOPOS = ("ring", "mesh", "torus")

MODE_LABEL = {
    "baseline_vc2": "Baseline (2 VCs, shared queue)",
    "vc4_plain":    "Control A: 4 VCs, shared queue",
    "vc2_qclass":   "Control B: 2 VCs, class queues",
    "vc4_class":    "Innovation: 4 VCs + class queues",
}


# --------------------------------------------------------------------------
# Routing (identical to the Week 2 C module)
# --------------------------------------------------------------------------

def ring_path(s, d, n=N):
    cw, ccw = (d - s) % n, (s - d) % n
    step, hops = (1, cw) if cw <= ccw else (-1, ccw)
    path, c = [s], s
    for _ in range(hops):
        c = (c + step) % n
        path.append(c)
    return path


def torus_path_dor(s, d, rows=ROWS, cols=COLS):
    """Dimension-order with the shorter way round each dimension; ties go to
    the increasing direction. Mirrors torus_route_dor() in the C module."""
    r1, c1 = divmod(s, cols)
    r2, c2 = divmod(d, cols)

    def steps(a, b, n):
        if a == b:
            return 0, 0
        up, down = (b - a) % n, (a - b) % n
        return (up, 1) if up <= down else (down, -1)

    chops, cstep = steps(c1, c2, cols)
    rhops, rstep = steps(r1, r2, rows)

    path, r, c = [s], r1, c1
    for _ in range(chops):
        c = (c + cstep) % cols
        path.append(r * cols + c)
    for _ in range(rhops):
        r = (r + rstep) % rows
        path.append(r * cols + c)
    return path


def mesh_path_xy(s, d, cols=COLS):
    r1, c1 = divmod(s, cols)
    r2, c2 = divmod(d, cols)
    path, r, c = [s], r1, c1
    while c != c2:
        c += 1 if c2 > c else -1
        path.append(r * cols + c)
    while r != r2:
        r += 1 if r2 > r else -1
        path.append(r * cols + c)
    return path


def destination_distribution(pattern):
    """P(dst | src), mirroring student_implementation/traffic.c."""
    P = [[0.0] * N for _ in range(N)]
    for s in range(N):
        row = [0.0] * N
        if pattern == "hotnode":
            fallthrough = 0.0
            for h in HOT:
                if h != s:
                    row[h] += HOT_FRACTION / len(HOT)
                else:
                    fallthrough += HOT_FRACTION / len(HOT)
            remaining = 1.0 - HOT_FRACTION + fallthrough
        else:
            remaining = 1.0
        for d in range(N):
            if d != s:
                row[d] += remaining / (N - 1)
        P[s] = row
    return P


def analytical_model(topology, pattern):
    """Channel-load saturation bound and zero-load latency."""
    P = destination_distribution(pattern)
    pathf = {"ring": ring_path, "mesh": mesh_path_xy,
             "torus": torus_path_dor}[topology]
    load, eject, hops = {}, [0.0] * N, 0.0

    for s in range(N):
        for d in range(N):
            p = P[s][d]
            if p <= 0.0:
                continue
            path = pathf(s, d)
            hops += p * (len(path) - 1) / N
            for a, b in zip(path, path[1:]):
                load[(a, b)] = load.get((a, b), 0.0) + p
            eject[d] += p

    gamma = max(load.values())
    lam_channel = 1.0 / gamma
    lam_eject = EJECT_BW / max(eject)

    # Classical bisection bound, as used in the Project 13 worked example.
    cross_fraction = (N / 2) * ((N / 2) / (N - 1))
    lam_bisection = STATIC[topology]["bisection"] / cross_fraction

    lam_star = min(lam_channel, lam_eject)
    t0 = hops * ROUTER_DELAY + (PACKET_FLITS - 1)
    return {
        "avg_hops": hops, "peak_channel_load": gamma,
        "lam_channel": lam_channel, "lam_eject": lam_eject,
        "lam_bisection": lam_bisection, "lam_star": lam_star,
        "zero_load_latency": t0,
    }


def model_latency(m, lam):
    """T(lambda) = T0 + (rho/(1-rho)) * L/2, an M/D/1 source-queue term."""
    rho = lam / m["lam_star"]
    if rho >= 0.999:
        return float("inf")
    return m["zero_load_latency"] + (rho / (1.0 - rho)) * (PACKET_FLITS / 2.0)


# --------------------------------------------------------------------------
# Data loading
# --------------------------------------------------------------------------

def load_sweep(path):
    rows = []
    with open(path) as fh:
        for r in csv.DictReader(fh):
            for k, v in list(r.items()):
                if k not in ("topology", "traffic", "vc_mode"):
                    r[k] = float(v)
            rows.append(r)
    return rows


def series(rows, topology, traffic, mode, field):
    sel = [r for r in rows
           if r["topology"] == topology and r["traffic"] == traffic
           and r["vc_mode"] == mode]
    sel.sort(key=lambda r: r["offered_rate"])
    return [r["offered_rate"] for r in sel], [r[field] for r in sel]


def achieved_saturation(rows, topology, traffic, mode="baseline_vc2"):
    """Highest offered rate that was still stable and loss-free."""
    sel = [r for r in rows
           if r["topology"] == topology and r["traffic"] == traffic
           and r["vc_mode"] == mode]
    sel.sort(key=lambda r: r["offered_rate"])
    best = 0.0
    for r in sel:
        if r["saturated"] == 0 and r["dropped_full"] == 0:
            best = r["offered_rate"]
        else:
            break
    return best


# --------------------------------------------------------------------------
# Figures
# --------------------------------------------------------------------------

def fig_latency_by_traffic(rows, traffic, outpath, title):
    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    for topo, marker in (("ring", "o"), ("mesh", "s"), ("torus", "^")):
        mode = HEADLINE_MODE[topo]
        x, y = series(rows, topo, traffic, mode, "avg_latency")
        if not x:
            continue
        st = STATIC[topo]
        ax.plot(x, y, marker=marker, linewidth=1.6, markersize=5,
                label=f"{topo} (diam {st['diameter']}, bisect {st['bisection']}"
                      f"; {USABLE_VC_NOTE[topo]})")
        sat = achieved_saturation(rows, topo, traffic, mode)
        ax.axvline(sat, linestyle=":", linewidth=1.1, color=ax.lines[-1].get_color())
        ax.annotate(f"saturation\n{sat:.2f}", xy=(sat, 20),
                    xytext=(sat + 0.01, 30), fontsize=8,
                    color=ax.lines[-1].get_color())
    ax.set_yscale("log")
    ax.set_xlabel("offered injection rate (flits/node/cycle)")
    ax.set_ylabel("average packet latency (cycles)")
    ax.set_title(title)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="upper left", fontsize=9)
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)


def fig_throughput(rows, outpath):
    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    styles = {("ring", "uniform"): ("o", "-"), ("mesh", "uniform"): ("s", "-"),
              ("torus", "uniform"): ("^", "-"),
              ("ring", "hotnode"): ("o", "--"), ("mesh", "hotnode"): ("s", "--"),
              ("torus", "hotnode"): ("^", "--")}
    for (topo, traf), (marker, ls) in styles.items():
        x, y = series(rows, topo, traf, HEADLINE_MODE[topo], "accepted_throughput")
        if not x:
            continue
        ax.plot(x, y, marker=marker, linestyle=ls, linewidth=1.5,
                markersize=4.5, label=f"{topo} / {traf}")
    lim = max(r["offered_rate"] for r in rows)
    ax.plot([0, lim], [0, lim], color="0.4", linewidth=1.0,
            label="ideal (accepted = offered)")
    ax.set_xlabel("offered injection rate (flits/node/cycle)")
    ax.set_ylabel("accepted throughput (flits/node/cycle)")
    ax.set_title("Accepted throughput vs offered load (equal usable VCs per packet)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)


def fig_model_validation(rows, models, outpath):
    fig, axes = plt.subplots(3, 2, figsize=(8.2, 9.4), sharex=True)
    combos = [("ring", "uniform"), ("ring", "hotnode"),
              ("mesh", "uniform"), ("mesh", "hotnode"),
              ("torus", "uniform"), ("torus", "hotnode")]
    for ax, (topo, traf) in zip(axes.ravel(), combos):
        m = models[(topo, traf)]
        xs = [0.005 + i * (0.98 * m["lam_star"] - 0.005) / 199 for i in range(200)]
        ax.plot(xs, [model_latency(m, x) for x in xs], linewidth=1.8,
                label="analytical model")
        x, y = series(rows, topo, traf, HEADLINE_MODE[topo], "avg_latency")
        ax.plot(x, y, "ko", markersize=4, markerfacecolor="white",
                label="measured")
        ax.axvline(m["lam_star"], linestyle="--", linewidth=1.0, color="0.5")
        ax.set_yscale("log")
        ax.set_ylim(3, 2e4)
        ax.set_title(f"{topo} / {traf}   (ideal $\\lambda^*$ = {m['lam_star']:.3f})",
                     fontsize=10)
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(fontsize=8, loc="upper left")
    for ax in axes[-1]:
        ax.set_xlabel("offered injection rate (flits/node/cycle)")
    for ax in axes[:, 0]:
        ax.set_ylabel("average latency (cycles)")
    fig.suptitle("Analytical channel-load model validated against the flit-level simulator")
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)


def fig_innovation(rows, outpath):
    fig, axes = plt.subplots(1, 2, figsize=(10.4, 4.4), sharey=True)
    for ax, topo in zip(axes, ("ring", "mesh")):
        for mode, marker in (("baseline_vc2", "o"), ("vc4_plain", "^"),
                             ("vc2_qclass", "v"), ("vc4_class", "s")):
            x, y = series(rows, topo, "hotnode", mode, "bg_latency")
            ax.plot(x, y, marker=marker, linewidth=1.5, markersize=4.5,
                    label=MODE_LABEL[mode])
        ax.set_yscale("log")
        ax.set_xlabel("offered injection rate (flits/node/cycle)")
        ax.set_title(f"{topo}, hot-node traffic", fontsize=10)
        ax.grid(True, which="both", alpha=0.3)
    axes[0].set_ylabel("background-traffic latency (cycles)")
    axes[0].legend(fontsize=8, loc="upper left")
    fig.suptitle("Head-of-line blocking removed: latency of traffic NOT addressed to a database node")
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)


def fig_packet_loss(rows, outpath):
    fig, ax = plt.subplots(figsize=(7.2, 4.4))
    for topo, traf, marker in (("ring", "uniform", "o"), ("mesh", "uniform", "s"),
                               ("torus", "uniform", "^"), ("ring", "hotnode", "P"),
                               ("mesh", "hotnode", "D"), ("torus", "hotnode", "v")):
        mode = HEADLINE_MODE[topo]
        x, y = series(rows, topo, traf, mode, "dropped_full")
        if not x:
            continue
        ax.plot(x, y, marker=marker, linewidth=1.5, markersize=4.5,
                label=f"{topo} / {traf}")
        sat = achieved_saturation(rows, topo, traf, mode)
        ax.axvline(sat, linestyle=":", linewidth=1.0,
                   color=ax.lines[-1].get_color())
    ax.set_xlabel("offered injection rate (flits/node/cycle)")
    ax.set_ylabel("packets dropped (full source queue)")
    ax.set_title("Explicit packet-loss accounting: zero loss below saturation")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)


def fig_saturation_bar(rows, models, outpath):
    combos = [("ring", "uniform"), ("mesh", "uniform"), ("torus", "uniform"),
              ("ring", "hotnode"), ("mesh", "hotnode"), ("torus", "hotnode")]
    labels = [f"{t}\n{p}" for t, p in combos]
    ideal = [models[c]["lam_star"] for c in combos]
    achieved = [achieved_saturation(rows, t, p, HEADLINE_MODE[t]) for t, p in combos]
    xs = range(len(combos))
    fig, ax = plt.subplots(figsize=(9.0, 4.4))
    ax.bar([x - 0.19 for x in xs], ideal, width=0.38,
           label="ideal (channel-load model)")
    ax.bar([x + 0.19 for x in xs], achieved, width=0.38,
           label="achieved (measured, loss-free)")
    for x, (i, a) in enumerate(zip(ideal, achieved)):
        ax.text(x + 0.19, a + 0.012, f"{a:.2f}", ha="center", fontsize=8)
        ax.text(x - 0.19, i + 0.012, f"{i:.2f}", ha="center", fontsize=8)
    ax.set_xticks(list(xs))
    ax.set_xticklabels(labels)
    ax.set_ylabel("saturation injection rate (flits/node/cycle)")
    ax.set_title("Ideal vs achieved saturation injection rate")
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)


def fig_vc_cost(rows, outpath):
    """The equal-total-VC view: deadlock freedom charged to the topology.

    The mesh needs no dateline, so both its VCs carry traffic. The torus must
    reserve half of its budget, so at an equal TOTAL budget of 2 VCs it has
    only one usable channel per packet and loses to the mesh despite its
    better diameter and bisection. Restoring the second usable channel costs
    2 more VCs of buffering."""
    fig, ax = plt.subplots(figsize=(7.6, 4.6))
    tracks = [
        ("mesh",  "baseline_vc2", "s", "-",  "mesh  2 VCs (2 usable, no dateline)"),
        ("torus", "baseline_vc2", "^", "--", "torus 2 VCs (1 usable, dateline)"),
        ("torus", "vc4_plain",    "^", "-",  "torus 4 VCs (2 usable, dateline)"),
    ]
    for topo, mode, marker, ls, label in tracks:
        x, y = series(rows, topo, "uniform", mode, "accepted_throughput")
        if not x:
            continue
        ax.plot(x, y, marker=marker, linestyle=ls, linewidth=1.6,
                markersize=5, label=label)
    lim = max(r["offered_rate"] for r in rows)
    ax.plot([0, lim], [0, lim], color="0.45", linewidth=1.0,
            label="ideal (accepted = offered)")
    ax.set_xlabel("offered injection rate (flits/node/cycle)")
    ax.set_ylabel("accepted throughput (flits/node/cycle)")
    ax.set_title("What deadlock freedom costs the torus (uniform traffic)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8.5, loc="upper left")
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)


# --------------------------------------------------------------------------
# Tables
# --------------------------------------------------------------------------

def write_results_table(rows, models, path):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["topology", "traffic", "diameter", "bisection_bandwidth_links",
                    "avg_hops_model", "zero_load_latency_measured",
                    "saturation_rate_measured", "peak_accepted_throughput",
                    "latency_at_0.20", "vc_budget"])
        for topo in TOPOS:
            for traf in ("uniform", "hotnode"):
                mode = HEADLINE_MODE[topo]
                sel = [r for r in rows if r["topology"] == topo
                       and r["traffic"] == traf and r["vc_mode"] == mode]
                if not sel:
                    continue
                sel.sort(key=lambda r: r["offered_rate"])
                lat020 = next((r["avg_latency"] for r in sel
                               if abs(r["offered_rate"] - 0.20) < 1e-9), "")
                w.writerow([
                    topo, traf, STATIC[topo]["diameter"], STATIC[topo]["bisection"],
                    f"{models[(topo, traf)]['avg_hops']:.3f}",
                    f"{sel[0]['avg_latency']:.2f}",
                    f"{achieved_saturation(rows, topo, traf, mode):.2f}",
                    f"{max(r['accepted_throughput'] for r in sel):.4f}",
                    f"{lat020:.2f}" if lat020 != "" else "",
                    USABLE_VC_NOTE[topo],
                ])


def write_model_table(rows, models, path):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["topology", "traffic", "avg_hops", "peak_channel_load",
                    "lam_channel", "lam_eject", "lam_classical_bisection",
                    "lam_star_model", "zero_load_latency_model",
                    "zero_load_latency_measured", "lam_achieved_measured",
                    "efficiency"])
        for topo in TOPOS:
            for traf in ("uniform", "hotnode"):
                m = models[(topo, traf)]
                mode = HEADLINE_MODE[topo]
                sel = [r for r in rows if r["topology"] == topo
                       and r["traffic"] == traf and r["vc_mode"] == mode]
                if not sel:
                    continue
                sel.sort(key=lambda r: r["offered_rate"])
                ach = achieved_saturation(rows, topo, traf, mode)
                w.writerow([topo, traf, f"{m['avg_hops']:.3f}",
                            f"{m['peak_channel_load']:.3f}",
                            f"{m['lam_channel']:.3f}", f"{m['lam_eject']:.3f}",
                            f"{m['lam_bisection']:.3f}", f"{m['lam_star']:.3f}",
                            f"{m['zero_load_latency']:.2f}",
                            f"{sel[0]['avg_latency']:.2f}",
                            f"{ach:.2f}", f"{ach / m['lam_star']:.3f}"])


def write_innovation_table(rows, path, knee=0.24):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["topology", "vc_mode", "description", "vcs", "queues",
                    "offered_rate", "background_latency", "hot_latency",
                    "accepted_throughput", "peak_accepted_throughput",
                    "background_speedup_vs_baseline"])
        def at_knee(topo, mode):
            return next((x for x in rows if x["topology"] == topo
                         and x["traffic"] == "hotnode" and x["vc_mode"] == mode
                         and abs(x["offered_rate"] - knee) < 1e-9), None)

        for topo in TOPOS:
            base = at_knee(topo, "baseline_vc2")
            if base is None or not base["bg_latency"]:
                continue
            for mode in ("baseline_vc2", "vc4_plain", "vc2_qclass", "vc4_class"):
                r = at_knee(topo, mode)
                if r is None or not r["bg_latency"]:
                    continue
                peak = max(x["accepted_throughput"] for x in rows
                           if x["topology"] == topo and x["traffic"] == "hotnode"
                           and x["vc_mode"] == mode)
                w.writerow([topo, mode, MODE_LABEL[mode], int(r["vcs"]),
                            int(r["queues"]), f"{knee:.2f}",
                            f"{r['bg_latency']:.1f}", f"{r['hot_latency']:.1f}",
                            f"{r['accepted_throughput']:.4f}", f"{peak:.4f}",
                            f"{base['bg_latency'] / r['bg_latency']:.1f}x"])


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default=os.path.join(here, "..", "results"),
                    help="results root holding raw/, processed/ and figures/")
    ap.add_argument("--raw", help="override the raw-results directory")
    ap.add_argument("--processed", help="override the tables directory")
    ap.add_argument("--figures", help="override the figures directory")
    args = ap.parse_args()

    rdir = os.path.abspath(args.results)
    raw = os.path.abspath(args.raw) if args.raw else os.path.join(rdir, "raw")
    pdir = os.path.abspath(args.processed) if args.processed else os.path.join(rdir, "processed")
    fdir = os.path.abspath(args.figures) if args.figures else os.path.join(rdir, "figures")
    for d in (pdir, fdir):
        os.makedirs(d, exist_ok=True)

    sweep = os.path.join(raw, "week3_sweep_results.csv")
    if not os.path.exists(sweep):
        raise SystemExit(
            f"sweep results not found: {sweep}\n"
            "Run ./run_sweep first, or point --raw at the directory holding "
            "week3_sweep_results.csv.")
    rows = load_sweep(sweep)
    models = {(t, p): analytical_model(t, p)
              for t in TOPOS for p in ("uniform", "hotnode")}

    print("=== Analytical channel-load model (Group 1 configuration) ===")
    print(f"{'topo':6s} {'traffic':8s} {'avgHops':>8s} {'peakLoad':>9s} "
          f"{'lam_chan':>9s} {'lam_eject':>10s} {'lam_bisec':>10s} "
          f"{'lam*':>7s} {'measured':>9s} {'eff':>6s}")
    for topo in TOPOS:
        for traf in ("uniform", "hotnode"):
            m = models[(topo, traf)]
            ach = achieved_saturation(rows, topo, traf, HEADLINE_MODE[topo])
            print(f"{topo:6s} {traf:8s} {m['avg_hops']:8.3f} "
                  f"{m['peak_channel_load']:9.3f} {m['lam_channel']:9.3f} "
                  f"{m['lam_eject']:10.3f} {m['lam_bisection']:10.3f} "
                  f"{m['lam_star']:7.3f} {ach:9.2f} {ach / m['lam_star']:6.2f}")

    fig_latency_by_traffic(rows, "uniform",
                           os.path.join(fdir, "fig1_latency_uniform.png"),
                           "Latency vs injection rate -- uniform-random traffic")
    fig_latency_by_traffic(rows, "hotnode",
                           os.path.join(fdir, "fig2_latency_hotnode.png"),
                           "Latency vs injection rate -- hot-node-skewed traffic")
    fig_throughput(rows, os.path.join(fdir, "fig3_throughput.png"))
    fig_model_validation(rows, models, os.path.join(fdir, "fig4_model_validation.png"))
    fig_innovation(rows, os.path.join(fdir, "fig5_innovation.png"))
    fig_packet_loss(rows, os.path.join(fdir, "fig6_packet_loss.png"))
    fig_saturation_bar(rows, models, os.path.join(fdir, "fig7_saturation.png"))
    fig_vc_cost(rows, os.path.join(fdir, "fig8_vc_cost.png"))

    write_results_table(rows, models, os.path.join(pdir, "week3_results_table.csv"))
    write_model_table(rows, models, os.path.join(pdir, "week3_model_vs_measured.csv"))
    write_innovation_table(rows, os.path.join(pdir, "week3_innovation_table.csv"))

    print("\nFigures written to", fdir)
    for f in sorted(os.listdir(fdir)):
        print("  ", f)
    print("Tables written to", pdir)


if __name__ == "__main__":
    main()
