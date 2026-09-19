#!/usr/bin/env python3
"""
gap48_figures.py — analysis/gap48/gap_summary.csv から要約図を生成する。

出力: figures/Gap48/
  - box_by_method_condition.png : 手法別 gap のボックスプロット（条件ごと）
  - gap_vs_RR_trend.png         : RR を横軸にした gap の推移（手法別、RVで分割）
  - top10_gap_instances.png     : gap 上位10インスタンス一覧（method=MaxShiftDur, 条件平均）

実行: cmake-build-release から `python ../tools/gap48_figures.py`
"""
import csv
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

METHOD_COLOR = {
    "MaxShiftDur": "#1f77b4",
    "MaxShift": "#ff7f0e",
    "SchedObj": "#2ca02c",
}
METHODS = ["MaxShiftDur", "MaxShift", "SchedObj"]
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]


def load_rows(csv_path):
    with open(csv_path, encoding="utf-8") as f:
        return list(csv.DictReader(f))


def box_by_method_condition(rows, out_path):
    data = defaultdict(lambda: defaultdict(list))
    for r in rows:
        if r["excluded"] == "True" or r["gap_mean"] == "":
            continue
        data[r["condition"]][r["method"]].append(float(r["gap_mean"]) * 100)

    fig, ax = plt.subplots(figsize=(13, 6))
    positions = []
    box_data = []
    colors = []
    labels = []
    width = 0.22
    for ci, cond in enumerate(CONDS):
        for mi, method in enumerate(METHODS):
            vals = data[cond][method]
            if not vals:
                continue
            pos = ci + (mi - 1) * width
            positions.append(pos)
            box_data.append(vals)
            colors.append(METHOD_COLOR[method])
    bp = ax.boxplot(box_data, positions=positions, widths=width * 0.9,
                     patch_artist=True, showfliers=True,
                     flierprops=dict(marker="o", ms=3, alpha=0.4, markeredgewidth=0))
    for patch, c in zip(bp["boxes"], colors):
        patch.set_facecolor(c)
        patch.set_alpha(0.65)
        patch.set_edgecolor("#333333")
    for med in bp["medians"]:
        med.set_color("#111111")
        med.set_linewidth(1.4)

    ax.set_xticks(range(len(CONDS)))
    ax.set_xticklabels(CONDS)
    ax.set_ylabel("matched-makespan gap  (NSGA vs MIP, %)")
    ax.set_title("j30 48-instance: gap distribution by method x condition")
    ax.axhline(0, color="#999999", lw=0.8)
    ax.axhline(1.0, color="#999999", lw=0.8, ls="--")
    ax.text(len(CONDS) - 0.5, 1.05, "1% threshold", fontsize=8, color="#666666")
    handles = [plt.Rectangle((0, 0), 1, 1, facecolor=METHOD_COLOR[m], alpha=0.65,
                              edgecolor="#333333", label=m) for m in METHODS]
    ax.legend(handles=handles, loc="upper left", frameon=False)
    ax.grid(axis="y", color="#e5e5e5", lw=0.6)
    ax.set_axisbelow(True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"[WRITE] {out_path}")


def gap_vs_rr_trend(rows, out_path):
    # RR trend, averaged over instances, split by RV
    rr_labels = ["RR000", "RR025", "RR050"]
    rv_ls = {"RV0": "-", "RV1": "--"}

    fig, ax = plt.subplots(figsize=(9, 6))
    for method in METHODS:
        for rv in ["RV0", "RV1"]:
            ys = []
            for rr in rr_labels:
                cond = f"{rr}_{rv}"
                vals = [float(r["gap_mean"]) * 100 for r in rows
                        if r["method"] == method and r["condition"] == cond
                        and r["excluded"] != "True" and r["gap_mean"] != ""]
                ys.append(sum(vals) / len(vals) if vals else None)
            xs = [i for i, y in enumerate(ys) if y is not None]
            yv = [y for y in ys if y is not None]
            ax.plot(xs, yv, color=METHOD_COLOR[method], ls=rv_ls[rv], marker="o", ms=5,
                    lw=1.8, label=f"{method} ({rv})")

    ax.set_xticks(range(len(rr_labels)))
    ax.set_xticklabels(rr_labels)
    ax.set_xlabel("Resource Reduction (RR)")
    ax.set_ylabel("mean matched-makespan gap (%, averaged over 48 instances)")
    ax.set_title("Gap vs resource tightness (RR), split by vacation (RV)")
    ax.grid(axis="y", color="#e5e5e5", lw=0.6)
    ax.set_axisbelow(True)
    ax.legend(loc="upper left", fontsize=8, frameon=False, ncol=2)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"[WRITE] {out_path}")


def top10_gap_instances(rows, out_path, method="MaxShiftDur"):
    per_instance = defaultdict(list)
    for r in rows:
        if r["method"] != method or r["excluded"] == "True" or r["gap_mean"] == "":
            continue
        per_instance[r["instance"]].append(float(r["gap_mean"]) * 100)

    avg = [(inst, sum(v) / len(v)) for inst, v in per_instance.items()]
    avg.sort(key=lambda t: -t[1])
    top = avg[:10]
    top = top[::-1]  # so largest ends up at top of horizontal bar chart

    fig, ax = plt.subplots(figsize=(8, 6))
    ys = range(len(top))
    vals = [v for _, v in top]
    ax.barh(ys, vals, color=METHOD_COLOR[method], alpha=0.75, edgecolor="#333333")
    ax.set_yticks(ys)
    ax.set_yticklabels([inst for inst, _ in top])
    for y, v in zip(ys, vals):
        ax.text(v + 0.02, y, f"{v:.2f}%", va="center", fontsize=8)
    ax.set_xlabel(f"mean matched-makespan gap across 6 conditions (%) — {method}")
    ax.set_title(f"Top-10 instances by gap ({method})")
    ax.grid(axis="x", color="#e5e5e5", lw=0.6)
    ax.set_axisbelow(True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"[WRITE] {out_path}")


def _box_panel(ax, rows, field, transform, methods=METHODS, conds=CONDS, width=0.22):
    """1パネル分のボックスプロットを描画する共通ヘルパー。"""
    data = defaultdict(lambda: defaultdict(list))
    for r in rows:
        if r["excluded"] == "True" or r[field] == "":
            continue
        data[r["condition"]][r["method"]].append(transform(float(r[field])))

    positions, box_data, colors = [], [], []
    for ci, cond in enumerate(conds):
        for mi, method in enumerate(methods):
            vals = data[cond][method]
            if not vals:
                continue
            positions.append(ci + (mi - 1) * width)
            box_data.append(vals)
            colors.append(METHOD_COLOR[method])
    bp = ax.boxplot(box_data, positions=positions, widths=width * 0.9,
                     patch_artist=True, showfliers=True,
                     flierprops=dict(marker="o", ms=2.5, alpha=0.35, markeredgewidth=0))
    for patch, c in zip(bp["boxes"], colors):
        patch.set_facecolor(c)
        patch.set_alpha(0.65)
        patch.set_edgecolor("#333333")
    for med in bp["medians"]:
        med.set_color("#111111")
        med.set_linewidth(1.2)
    ax.set_xticks(range(len(conds)))
    ax.set_xticklabels(conds, fontsize=8)
    ax.grid(axis="y", color="#e5e5e5", lw=0.6)
    ax.set_axisbelow(True)


def box_three_metrics(rows, out_path):
    """3指標（コスト超過率／工期超過率／HV比）を並べたボックスプロット（条件ごと）。"""
    fig, axes = plt.subplots(3, 1, figsize=(13, 14))

    _box_panel(axes[0], rows, "gap_mean", lambda v: v * 100)
    axes[0].axhline(0, color="#999999", lw=0.8)
    axes[0].axhline(1.0, color="#999999", lw=0.8, ls="--")
    axes[0].set_ylabel("matched-makespan gap\n(cost overrun, %)")
    axes[0].set_title("(1) matched-makespan gap (existing metric: cost overrun at matched ms<=m)")

    _box_panel(axes[1], rows, "ms_gap_mean", lambda v: v * 100)
    axes[1].axhline(0, color="#999999", lw=0.8)
    axes[1].axhline(1.0, color="#999999", lw=0.8, ls="--")
    axes[1].set_ylabel("matched-cost makespan gap\n(makespan overrun, %)")
    axes[1].set_title("(2) matched-cost makespan gap (new metric: makespan overrun at matched cost<=c)")

    _box_panel(axes[2], rows, "hv_ratio", lambda v: v)
    axes[2].axhline(1.0, color="#999999", lw=0.8, ls="--")
    axes[2].set_ylabel("HV ratio (NSGA/MIP)")
    axes[2].set_title("(3) Hypervolume ratio (1.0 = matches MIP; normalized space, shared nadir=(1.1,1.1))")
    axes[2].set_ylim(0, 1.05)

    handles = [plt.Rectangle((0, 0), 1, 1, facecolor=METHOD_COLOR[m], alpha=0.65,
                              edgecolor="#333333", label=m) for m in METHODS]
    fig.legend(handles=handles, loc="upper center", ncol=3, fontsize=10, frameon=False,
               bbox_to_anchor=(0.5, 1.0))
    fig.suptitle("j30 48-instance: three-metric distribution comparison (cost gap / makespan gap / HV ratio)",
                 fontsize=12, y=1.02)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"[WRITE] {out_path}")


def hv_ratio_shortfall(rows, out_path):
    """HV比の分布（1.0からどれだけ下回るか＝shortfall = 1-hv_ratio）を手法別に示す。"""
    data = defaultdict(list)
    for r in rows:
        if r["excluded"] == "True" or r["hv_ratio"] == "":
            continue
        data[r["method"]].append((1.0 - float(r["hv_ratio"])) * 100)

    fig, ax = plt.subplots(figsize=(7, 6))
    box_data = [data[m] for m in METHODS]
    bp = ax.boxplot(box_data, positions=range(len(METHODS)), widths=0.5,
                     patch_artist=True, showfliers=True,
                     flierprops=dict(marker="o", ms=3, alpha=0.35, markeredgewidth=0))
    for patch, m in zip(bp["boxes"], METHODS):
        patch.set_facecolor(METHOD_COLOR[m])
        patch.set_alpha(0.65)
        patch.set_edgecolor("#333333")
    for med in bp["medians"]:
        med.set_color("#111111")
        med.set_linewidth(1.4)
    ax.set_xticks(range(len(METHODS)))
    ax.set_xticklabels(METHODS)
    ax.set_ylabel("HV shortfall = (1 - hv_ratio) x 100  (%, lower is better)")
    ax.set_title("Deviation of HV ratio from 1.0 (all conditions pooled)")
    ax.axhline(0, color="#999999", lw=0.8)
    ax.grid(axis="y", color="#e5e5e5", lw=0.6)
    ax.set_axisbelow(True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"[WRITE] {out_path}")


def unmatched_rate_comparison(rows, out_path):
    """unmatched率（コスト方向=現行 / 工期方向=新規）の手法別比較。"""
    considered = [r for r in rows if r["excluded"] != "True"]
    stats = {}
    for method in METHODS:
        mrows = [r for r in considered if r["method"] == method]
        n_rows = len(mrows)
        cost_rows = sum(1 for r in mrows if int(r["unmatched_mip_points"]) > 0)
        ms_rows = sum(1 for r in mrows if int(r["ms_unmatched_points"]) > 0)
        total_pts = sum(int(r["mip_pf_size"]) for r in mrows)
        cost_pts = sum(int(r["unmatched_mip_points"]) for r in mrows)
        ms_pts = sum(int(r["ms_unmatched_points"]) for r in mrows)
        stats[method] = dict(n_rows=n_rows, cost_rows=cost_rows, ms_rows=ms_rows,
                              total_pts=total_pts, cost_pts=cost_pts, ms_pts=ms_pts)

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    x = range(len(METHODS))
    width = 0.35

    ax = axes[0]
    cost_rate = [stats[m]["cost_rows"] / stats[m]["n_rows"] * 100 for m in METHODS]
    ms_rate = [stats[m]["ms_rows"] / stats[m]["n_rows"] * 100 for m in METHODS]
    ax.bar([i - width / 2 for i in x], cost_rate, width, color="#6495ED", label="cost direction (existing gap's blind spot)")
    ax.bar([i + width / 2 for i in x], ms_rate, width, color="#d62728", label="makespan direction (new metric)")
    ax.set_xticks(list(x)); ax.set_xticklabels(METHODS)
    ax.set_ylabel("% of rows with >=1 unmatched point")
    ax.set_title("Rows containing at least one unmatched point")
    ax.legend(fontsize=8, frameon=False)
    ax.grid(axis="y", color="#e5e5e5", lw=0.6)
    ax.set_axisbelow(True)

    ax = axes[1]
    cost_pt_rate = [stats[m]["cost_pts"] / stats[m]["total_pts"] * 100 for m in METHODS]
    ms_pt_rate = [stats[m]["ms_pts"] / stats[m]["total_pts"] * 100 for m in METHODS]
    ax.bar([i - width / 2 for i in x], cost_pt_rate, width, color="#6495ED", label="cost direction (existing gap's blind spot)")
    ax.bar([i + width / 2 for i in x], ms_pt_rate, width, color="#d62728", label="makespan direction (new metric)")
    ax.set_xticks(list(x)); ax.set_xticklabels(METHODS)
    ax.set_ylabel("unmatched points (% of all MIP points)")
    ax.set_title("Unmatched MIP points")
    ax.legend(fontsize=8, frameon=False)
    ax.grid(axis="y", color="#e5e5e5", lw=0.6)
    ax.set_axisbelow(True)

    fig.suptitle("Unreachable region (unmatched) comparison by method", fontsize=12)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"[WRITE] {out_path}")


def main():
    bd = "."
    csv_path = os.path.join(bd, "analysis", "gap48", "gap_summary.csv")
    out_dir = os.path.join(bd, "figures", "Gap48")
    os.makedirs(out_dir, exist_ok=True)
    rows = load_rows(csv_path)

    box_by_method_condition(rows, os.path.join(out_dir, "box_by_method_condition.png"))
    gap_vs_rr_trend(rows, os.path.join(out_dir, "gap_vs_RR_trend.png"))
    for method in METHODS:
        top10_gap_instances(rows, os.path.join(out_dir, f"top10_gap_instances_{method}.png"), method=method)

    box_three_metrics(rows, os.path.join(out_dir, "box_three_metrics.png"))
    hv_ratio_shortfall(rows, os.path.join(out_dir, "hv_ratio_shortfall.png"))
    unmatched_rate_comparison(rows, os.path.join(out_dir, "unmatched_rate_comparison.png"))


if __name__ == "__main__":
    main()
