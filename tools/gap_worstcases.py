#!/usr/bin/env python3
"""
gap_worstcases.py — gap（MaxShiftDur, matched-makespan）が大きい上位10の
(instance, condition) について、フロントを拡大表示した詳細図を作る。

MIP の各点から、対応する MaxShiftDur の matched 点（ms<=m での最小コスト）へ
補助線を引き、makespan ごとのコスト差を明示する。

出力: figures/WorstCases/worst_<rank>_<instance>_<condition>.png

使い方: cmake-build-release から
  python ../tools/gap_worstcases.py
"""
import csv
import importlib.util
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g48", os.path.join(HERE, "gap48_analysis.py"))
g48 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g48)

RANK_METHOD = "MaxShiftDur"
TOP_N = 10


def top_n_rows(csv_path, n=TOP_N):
    rows = []
    with open(csv_path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["method"] != RANK_METHOD or r["excluded"] == "True" or r["gap_mean"] == "":
                continue
            rows.append(r)
    rows.sort(key=lambda r: -float(r["gap_mean"]))
    return rows[:n]


def build_detail(bd, rank, row, out_dir):
    prefix, cond = row["instance"], row["condition"]
    sm_path = os.path.join(bd, "j30.sm", f"{prefix}.sm")

    mip_sched = os.path.join(bd, "results", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
    mip_pts, _ = g48.load_valid_points(mip_sched, sm_path)
    mip_front = g48.pareto_front(mip_pts)

    enc_sched = os.path.join(
        bd, "results", "FUN_ENC", prefix,
        "SCHED_ENC_" + g48.METHODS[RANK_METHOD].format(prefix=prefix, cond=cond) + ".txt")
    nsga_pts, _ = g48.load_valid_points(enc_sched, sm_path)
    nsga_front = g48.pareto_front(nsga_pts)

    fig, ax = plt.subplots(figsize=(8, 6))
    mxs = [p[0] for p in mip_front]; mys = [p[1] for p in mip_front]
    nxs = [p[0] for p in nsga_front]; nys = [p[1] for p in nsga_front]

    ax.plot(mxs, mys, color="#111111", lw=1.2, alpha=0.7, zorder=2)
    ax.scatter(mxs, mys, c="#111111", marker="*", s=90, zorder=5, label="MIP (exact)")
    ax.plot(nxs, nys, color="#d62728", lw=1.2, alpha=0.7, zorder=3)
    ax.scatter(nxs, nys, c="#d62728", marker="D", s=45, zorder=6,
               edgecolors="black", linewidths=0.4, label=RANK_METHOD)

    for m_ms, m_cost in mip_front:
        c_nsga = g48.min_cost_at_or_before(nsga_front, m_ms)
        if c_nsga is None:
            continue
        ax.plot([m_ms, m_ms], [m_cost, c_nsga], color="#ff7f0e", lw=1.0, alpha=0.6, zorder=1)

    ax.set_xlabel("Makespan")
    ax.set_ylabel("Total Resource Cost")
    ax.set_title(f"Worst-case #{rank}: {prefix}  [{cond}]", fontsize=12, fontweight="bold")
    ax.grid(True, ls="--", alpha=0.35, color="#bbbbbb")
    ax.legend(loc="upper right", fontsize=9)
    for sp in ["top", "right"]:
        ax.spines[sp].set_visible(False)

    caption = (f"gap_mean={float(row['gap_mean'])*100:.2f}%  gap_max={float(row['gap_max'])*100:.2f}%  "
               f"mip_pf={row['mip_pf_size']}  nsga_pf={row['nsga_pf_size']}  n_points={row['n_points']}")
    ax.text(0.02, 0.02, caption, transform=ax.transAxes, fontsize=8, family="monospace",
            va="bottom", bbox=dict(boxstyle="round,pad=0.35", facecolor="white",
                                    alpha=0.85, edgecolor="#cccccc", linewidth=0.8))

    plt.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"worst_{rank:02d}_{prefix}_{cond}.png")
    fig.savefig(out_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"[SAVED] {out_path}  gap_mean={row['gap_mean']}")


def main():
    bd = "."
    gap_csv = os.path.join(bd, "analysis", "gap48", "gap_summary.csv")
    rows = top_n_rows(gap_csv)
    out_dir = os.path.join(bd, "figures", "WorstCases")
    for rank, row in enumerate(rows, start=1):
        build_detail(bd, rank, row, out_dir)


if __name__ == "__main__":
    main()
