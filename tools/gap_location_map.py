#!/usr/bin/env python3
"""
gap_location_map.py — フロント上のどの位置でギャップが開くかを示す図（★最重要）。

各 (instance, condition) について MIP フロントの各点をフロント上の正規化位置
  pos = (m - ms_min) / (ms_max - ms_min)   （0=メイクスパン最小端, 1=メイクスパン最大端≈コスト最小端）
に変換し、その点での matched-makespan gap（NSGA vs MIP）を縦軸に取る。
全インスタンスの点を条件別パネルに重ね、手法別に色分けして移動平均線を重ねる。

gap48_analysis.py の load_valid_points / pareto_front / min_cost_at_or_before を再利用する。
出力: figures/GapLocation/gap_vs_frontposition.png

使い方: cmake-build-release から
  python ../tools/gap_location_map.py
"""
import csv
import importlib.util
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g48", os.path.join(HERE, "gap48_analysis.py"))
g48 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g48)

CONDITIONS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
METHODS = ["MaxShiftDur", "MaxShift", "SchedObj"]
METHOD_COLOR = {"MaxShiftDur": "#d62728", "MaxShift": "#6495ED", "SchedObj": "#2ca02c"}
N_BINS = 20


def load_excluded(csv_path):
    excl = set()
    with open(csv_path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["excluded"] == "True":
                excl.add((r["instance"], r["condition"]))
    return excl


def collect_points(bd, excluded_pairs):
    """cond -> method -> list of (pos, gap) を返す。"""
    out = {c: {m: [] for m in METHODS} for c in CONDITIONS}
    sm_dir = os.path.join(bd, "j30.sm")

    for x in range(1, 49):
        prefix = f"j30{x}_1"
        sm_path = os.path.join(sm_dir, f"{prefix}.sm")
        for cond in CONDITIONS:
            if (prefix, cond) in excluded_pairs:
                continue
            mip_sched = os.path.join(bd, "results", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
            mip_pts, mip_stats = g48.load_valid_points(mip_sched, sm_path)
            if mip_pts is None or not mip_pts:
                continue
            mip_front = g48.pareto_front(mip_pts)
            if len(mip_front) < 2:
                continue
            ms_lo = mip_front[0][0]
            ms_hi = mip_front[-1][0]
            if ms_hi <= ms_lo:
                continue

            for method, pattern in g48.METHODS.items():
                enc_sched = os.path.join(
                    bd, "results", "FUN_ENC", prefix,
                    "SCHED_ENC_" + pattern.format(prefix=prefix, cond=cond) + ".txt")
                nsga_pts, _ = g48.load_valid_points(enc_sched, sm_path)
                nsga_front = g48.pareto_front(nsga_pts) if nsga_pts else []
                if not nsga_front:
                    continue
                for m_ms, m_cost in mip_front:
                    c_nsga = g48.min_cost_at_or_before(nsga_front, m_ms)
                    if c_nsga is None:
                        continue
                    pos = (m_ms - ms_lo) / (ms_hi - ms_lo)
                    gap = (c_nsga - m_cost) / m_cost
                    out[cond][method].append((pos, gap))
    return out


def moving_average(points, n_bins=N_BINS):
    if not points:
        return [], []
    xs = np.array([p[0] for p in points])
    ys = np.array([p[1] for p in points])
    edges = np.linspace(0, 1, n_bins + 1)
    centers, means = [], []
    for i in range(n_bins):
        mask = (xs >= edges[i]) & (xs < edges[i + 1] if i < n_bins - 1 else xs <= edges[i + 1])
        if mask.sum() == 0:
            continue
        centers.append((edges[i] + edges[i + 1]) / 2)
        means.append(ys[mask].mean())
    return centers, means


def main():
    bd = "."
    excluded_pairs = load_excluded(os.path.join(bd, "analysis", "gap48", "gap_summary.csv"))
    data = collect_points(bd, excluded_pairs)

    fig, axes = plt.subplots(2, 3, figsize=(16, 9), sharex=True, sharey=True)
    for i, cond in enumerate(CONDITIONS):
        ax = axes[i // 3][i % 3]
        for method in METHODS:
            pts = data[cond][method]
            if not pts:
                continue
            xs = [p[0] for p in pts]
            ys = [p[1] * 100 for p in pts]
            ax.scatter(xs, ys, s=4, alpha=0.12, color=METHOD_COLOR[method], linewidths=0)
            cx, cy = moving_average(pts)
            cy = [v * 100 for v in cy]
            ax.plot(cx, cy, color=METHOD_COLOR[method], lw=2.2, label=method)
        ax.axhline(0, color="#999999", lw=0.8)
        ax.axhline(1.0, color="#999999", lw=0.8, ls="--")
        ax.set_title(cond, fontsize=11)
        ax.set_ylim(-1, 8)
        ax.grid(color="#eeeeee", lw=0.6)
        if i // 3 == 1:
            ax.set_xlabel("front position (0=makespan-min end, 1=cost-min end)")
        if i % 3 == 0:
            ax.set_ylabel("matched-makespan gap (%)")

    handles = [plt.Line2D([0], [0], color=METHOD_COLOR[m], lw=2.2, label=m) for m in METHODS]
    fig.legend(handles=handles, loc="upper center", ncol=3, fontsize=10, frameon=False,
               bbox_to_anchor=(0.5, 1.02))
    fig.suptitle("Where does the gap open on the front? (dots=all MIP points across 48 instances, "
                 "line=20-bin moving average)", fontsize=11, y=1.07)
    fig.tight_layout()

    out_dir = os.path.join(bd, "figures", "GapLocation")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "gap_vs_frontposition.png")
    fig.savefig(out_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"[SAVED] {out_path}")

    # 数値サマリもCSVに残す（後段の考察用）
    summary_path = os.path.join(bd, "analysis", "gap48", "gap_vs_frontposition_summary.csv")
    with open(summary_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["condition", "method", "bin_center", "gap_mean_pct", "n_points"])
        for cond in CONDITIONS:
            for method in METHODS:
                pts = data[cond][method]
                xs = np.array([p[0] for p in pts]) if pts else np.array([])
                ys = np.array([p[1] for p in pts]) if pts else np.array([])
                edges = np.linspace(0, 1, N_BINS + 1)
                for i in range(N_BINS):
                    if i < N_BINS - 1:
                        mask = (xs >= edges[i]) & (xs < edges[i + 1])
                    else:
                        mask = (xs >= edges[i]) & (xs <= edges[i + 1])
                    n = int(mask.sum())
                    center = (edges[i] + edges[i + 1]) / 2
                    mean = float(ys[mask].mean() * 100) if n > 0 else ""
                    w.writerow([cond, method, f"{center:.3f}", mean, n])
    print(f"[WRITE] {summary_path}")


if __name__ == "__main__":
    main()
