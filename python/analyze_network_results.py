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

STATIC = {                       # verified in Week 2
    "ring": {"diameter": 8, "bisection": 2, "links": 16, "degree": 2},
    "mesh": {"diameter": 6, "bisection": 4, "links": 24, "degree": 4},
}

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
    pathf = ring_path if topology == "ring" else mesh_path_xy
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
    for topo, marker in (("ring", "o"), ("mesh", "s")):
        x, y = series(rows, topo, traffic, "baseline_vc2", "avg_latency")
        st = STATIC[topo]
        ax.plot(x, y, marker=marker, linewidth=1.6, markersize=5,
                label=f"{topo} (diameter {st['diameter']}, bisection {st['bisection']})")
        sat = achieved_saturation(rows, topo, traffic)
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
              ("ring", "hotnode"): ("o", "--"), ("mesh", "hotnode"): ("s", "--")}
    for (topo, traf), (marker, ls) in styles.items():
        x, y = series(rows, topo, traf, "baseline_vc2", "accepted_throughput")
        ax.plot(x, y, marker=marker, linestyle=ls, linewidth=1.5,
                markersize=4.5, label=f"{topo} / {traf}")
    lim = max(r["offered_rate"] for r in rows)
    ax.plot([0, lim], [0, lim], color="0.4", linewidth=1.0,
            label="ideal (accepted = offered)")
    ax.set_xlabel("offered injection rate (flits/node/cycle)")
    ax.set_ylabel("accepted throughput (flits/node/cycle)")
    ax.set_title("Accepted throughput vs offered load (baseline flow control)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)


def fig_model_validation(rows, models, outpath):
    fig, axes = plt.subplots(2, 2, figsize=(9.6, 6.8), sharex=True)
    combos = [("ring", "uniform"), ("mesh", "uniform"),
              ("ring", "hotnode"), ("mesh", "hotnode")]
    for ax, (topo, traf) in zip(axes.ravel(), combos):
        m = models[(topo, traf)]
        xs = [0.005 + i * (0.98 * m["lam_star"] - 0.005) / 199 for i in range(200)]
        ax.plot(xs, [model_latency(m, x) for x in xs], linewidth=1.8,
                label="analytical model")
        x, y = series(rows, topo, traf, "baseline_vc2", "avg_latency")
        ax.plot(x, y, "ko", markersize=4, markerfacecolor="white",
                label="measured")
        ax.axvline(m["lam_star"], linestyle="--", linewidth=1.0, color="0.5")
        ax.set_yscale("log")
        ax.set_ylim(3, 2e4)
        ax.set_title(f"{topo} / {traf}   (ideal $\\lambda^*$ = {m['lam_star']:.3f})",
                     fontsize=10)
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(fontsize=8, loc="upper left")
    for ax in axes[1]:
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
                               ("ring", "hotnode", "^"), ("mesh", "hotnode", "v")):
        x, y = series(rows, topo, traf, "baseline_vc2", "dropped_full")
        ax.plot(x, y, marker=marker, linewidth=1.5, markersize=4.5,
                label=f"{topo} / {traf}")
        sat = achieved_saturation(rows, topo, traf)
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
    combos = [("ring", "uniform"), ("mesh", "uniform"),
              ("ring", "hotnode"), ("mesh", "hotnode")]
    labels = [f"{t}\n{p}" for t, p in combos]
    ideal = [models[c]["lam_star"] for c in combos]
    achieved = [achieved_saturation(rows, *c) for c in combos]
    xs = range(len(combos))
    fig, ax = plt.subplots(figsize=(7.2, 4.4))
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


# --------------------------------------------------------------------------
# Tables
# --------------------------------------------------------------------------

def write_results_table(rows, models, path):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["topology", "traffic", "diameter", "bisection_bandwidth_links",
                    "avg_hops_model", "zero_load_latency_measured",
                    "saturation_rate_measured", "peak_accepted_throughput",
                    "latency_at_0.20"])
        for topo in ("ring", "mesh"):
            for traf in ("uniform", "hotnode"):
                sel = [r for r in rows if r["topology"] == topo
                       and r["traffic"] == traf and r["vc_mode"] == "baseline_vc2"]
                sel.sort(key=lambda r: r["offered_rate"])
                lat020 = next((r["avg_latency"] for r in sel
                               if abs(r["offered_rate"] - 0.20) < 1e-9), "")
                w.writerow([
                    topo, traf, STATIC[topo]["diameter"], STATIC[topo]["bisection"],
                    f"{models[(topo, traf)]['avg_hops']:.3f}",
                    f"{sel[0]['avg_latency']:.2f}",
                    f"{achieved_saturation(rows, topo, traf):.2f}",
                    f"{max(r['accepted_throughput'] for r in sel):.4f}",
                    f"{lat020:.2f}" if lat020 != "" else "",
                ])


def write_model_table(rows, models, path):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["topology", "traffic", "avg_hops", "peak_channel_load",
                    "lam_channel", "lam_eject", "lam_classical_bisection",
                    "lam_star_model", "zero_load_latency_model",
                    "zero_load_latency_measured", "lam_achieved_measured",
                    "efficiency"])
        for topo in ("ring", "mesh"):
            for traf in ("uniform", "hotnode"):
                m = models[(topo, traf)]
                sel = [r for r in rows if r["topology"] == topo
                       and r["traffic"] == traf and r["vc_mode"] == "baseline_vc2"]
                sel.sort(key=lambda r: r["offered_rate"])
                ach = achieved_saturation(rows, topo, traf)
                w.writerow([topo, traf, f"{m['avg_hops']:.3f}",
                            f"{m['peak_channel_load']:.3f}",
                            f"{m['lam_channel']:.3f}", f"{m['lam_eject']:.3f}",
                            f"{m['lam_bisection']:.3f}", f"{m['lam_star']:.3f}",
                            f"{m['zero_load_latency']:.2f}",
                            f"{sel[0]['avg_latency']:.2f}",
                            f"{ach:.2f}", f"{ach / m['lam_star']:.3f}"])


def write_innovation_table(rows, path, knee=0.24):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["topology", "vc_mode", "description", "vcs", "queues",
                    "offered_rate", "background_latency", "hot_latency",
                    "accepted_throughput", "peak_accepted_throughput",
                    "background_speedup_vs_baseline"])
        for topo in ("ring", "mesh"):
            base = next(r for r in rows if r["topology"] == topo
                        and r["traffic"] == "hotnode"
                        and r["vc_mode"] == "baseline_vc2"
                        and abs(r["offered_rate"] - knee) < 1e-9)
            for mode in ("baseline_vc2", "vc4_plain", "vc2_qclass", "vc4_class"):
                r = next(x for x in rows if x["topology"] == topo
                         and x["traffic"] == "hotnode" and x["vc_mode"] == mode
                         and abs(x["offered_rate"] - knee) < 1e-9)
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
    ap.add_argument("--results", default=os.path.join(here, "..", "results"))
    args = ap.parse_args()

    rdir = os.path.abspath(args.results)
    fdir = os.path.join(rdir, "figures")
    os.makedirs(fdir, exist_ok=True)

    rows = load_sweep(os.path.join(rdir, "week3_sweep_results.csv"))
    models = {(t, p): analytical_model(t, p)
              for t in ("ring", "mesh") for p in ("uniform", "hotnode")}

    print("=== Analytical channel-load model (Group 1 configuration) ===")
    print(f"{'topo':5s} {'traffic':8s} {'avgHops':>8s} {'peakLoad':>9s} "
          f"{'lam_chan':>9s} {'lam_eject':>10s} {'lam_bisec':>10s} "
          f"{'lam*':>7s} {'measured':>9s} {'eff':>6s}")
    for topo in ("ring", "mesh"):
        for traf in ("uniform", "hotnode"):
            m = models[(topo, traf)]
            ach = achieved_saturation(rows, topo, traf)
            print(f"{topo:5s} {traf:8s} {m['avg_hops']:8.3f} "
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

    write_results_table(rows, models, os.path.join(rdir, "week3_results_table.csv"))
    write_model_table(rows, models, os.path.join(rdir, "week3_model_vs_measured.csv"))
    write_innovation_table(rows, os.path.join(rdir, "week3_innovation_table.csv"))

    print("\nFigures written to", fdir)
    for f in sorted(os.listdir(fdir)):
        print("  ", f)
    print("Tables written to", rdir)


if __name__ == "__main__":
    main()
