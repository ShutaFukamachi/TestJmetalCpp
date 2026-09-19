#!/usr/bin/env python3
"""
gap_frontoverview.py — 条件ごとに48インスタンスのフロントを並べた一覧図（コンタクトシート）。

288枚の個別図を全部開く代わりに、「どのインスタンスが異常か」を一目で見つけるための
サムネイル一覧を6条件分作る（8行x6列=48パネル/条件）。

各パネルには MaxShiftDur の2つのギャップを表示する（analysis/gap48/gap_summary.csv より）:
  - cost行: matched-makespan gap（同一工期でのコスト超過率, 列 gap_mean）。> 1.0% で赤字。
  - time行: matched-cost makespan gap（同一コストでの工期超過率, 列 ms_gap_mean）。> 10.0% でオレンジ字。
どちらの指標が閾値を超えているかは、それぞれ独立した色（赤=cost超過、オレンジ=time超過）で判別できる。

出力: figures/FrontOverview/overview_<condition>.png（従来と同じ並び順＝インスタンス番号順）
      figures/FrontOverview/overview_<condition>_sorted_by_timegap.png（工期ギャップ降順）

使い方: cmake-build-release から
  python ../tools/gap_frontoverview.py
"""
import csv
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

CONDITIONS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
SERIES_STYLE = {
    "MaxShiftDur": dict(color="#d62728", marker="D", s=6, label="MaxShiftDur"),
    "MaxShift":    dict(color="#6495ED", marker="^", s=6, label="MaxShift"),
    "SchedObj":    dict(color="#2ca02c", marker="o", s=6, label="SchedObj"),
}

COST_GAP_THRESHOLD = 0.01   # 1.0%
TIME_GAP_THRESHOLD = 0.10   # 10.0%
COST_OVER_COLOR = "#b30000"
TIME_OVER_COLOR = "#cc6600"
NEUTRAL_COLOR = "#333333"


def read_fun(path):
    pts = []
    if not path or not os.path.exists(path):
        return pts
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2:
                try:
                    ms, c = float(p[0]), float(p[1])
                    if ms < 1e8 and c < 1e8:
                        pts.append((ms, c))
                except ValueError:
                    pass
    return pts


def pareto_filter(pts):
    if not pts:
        return []
    s = sorted(set(pts), key=lambda p: (p[0], p[1]))
    best = math.inf
    pf = []
    for ms, c in s:
        if c < best:
            pf.append((ms, c))
            best = c
    return pf


def load_gap_map(csv_path):
    """(instance, condition, method) -> (cost_gap, ms_gap, excluded) の辞書。
    cost_gap = gap_mean（コスト超過率）, ms_gap = ms_gap_mean（工期超過率）。
    値が空/欠損なら None。excluded 行は cost_gap=ms_gap=None として扱う。"""
    out = {}
    with open(csv_path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            key = (r["instance"], r["condition"], r["method"])
            excl = r["excluded"] == "True"
            cost_gap = float(r["gap_mean"]) if (not excl and r["gap_mean"] not in ("", None)) else None
            ms_gap = float(r["ms_gap_mean"]) if (not excl and r.get("ms_gap_mean") not in ("", None)) else None
            out[key] = (cost_gap, ms_gap, excl)
    return out


def paths_for(bd, prefix, cond):
    enc_dir = os.path.join(bd, "results", "FUN_ENC", prefix)
    mip_dir = os.path.join(bd, "results", "FUN_MIP", prefix)
    return {
        "MIP": os.path.join(mip_dir, f"FUN_MIP_{prefix}_{cond}.txt"),
        "SchedObj": os.path.join(enc_dir, f"FUN_ENC_{prefix}_{cond}_P1_SchedObj.txt"),
        "MaxShift": os.path.join(enc_dir, f"FUN_ENC_{prefix}_{cond}_P1_MaxShift.txt"),
        "MaxShiftDur": os.path.join(enc_dir, f"FUN_ENC_{prefix}_{cond}_MaxShiftDur.txt"),
    }


def draw_panel(ax, bd, prefix, cond, gap_map):
    """1パネル分の描画（フロント散布図 + 3行ヘッダー）を行う。"""
    paths = paths_for(bd, prefix, cond)
    mip_pf = pareto_filter(read_fun(paths["MIP"]))

    short_name = prefix.replace("_1", "")
    cost_gap, ms_gap, excluded = gap_map.get((prefix, cond, "MaxShiftDur"), (None, None, True))

    if not mip_pf:
        ax.text(0.5, 0.5, "MIP\ninfeasible", ha="center", va="center",
                 fontsize=8, color="#999999", transform=ax.transAxes)
        ax.text(0.5, 1.04, short_name, ha="center", va="bottom", fontsize=8,
                 color="#999999", transform=ax.transAxes, clip_on=False)
        ax.set_xticks([]); ax.set_yticks([])
        for sp in ax.spines.values():
            sp.set_edgecolor("#dddddd")
        return

    xs = [p[0] for p in mip_pf]; ys = [p[1] for p in mip_pf]
    ax.plot(xs, ys, color="#111111", lw=0.8, alpha=0.6, zorder=1)
    ax.scatter(xs, ys, c="#111111", marker="*", s=14, zorder=5, label="MIP")

    for method, sty in SERIES_STYLE.items():
        pf = pareto_filter(read_fun(paths[method]))
        if not pf:
            continue
        mxs = [p[0] for p in pf]; mys = [p[1] for p in pf]
        ax.scatter(mxs, mys, c=sty["color"], marker=sty["marker"],
                   s=sty["s"], alpha=0.65, linewidths=0, zorder=3, label=sty["label"])

    # ---- 3行ヘッダー: インスタンス名 / cost gap / time gap（軸の上に手動配置） ----
    ax.text(0.5, 1.30, short_name, ha="center", va="bottom", fontsize=8,
             color="#111111", fontweight="bold", transform=ax.transAxes, clip_on=False)

    cost_color = COST_OVER_COLOR if (cost_gap is not None and cost_gap > COST_GAP_THRESHOLD) else NEUTRAL_COLOR
    cost_text = f"cost {cost_gap*100:.2f}%" if cost_gap is not None else "cost ---"
    ax.text(0.5, 1.16, cost_text, ha="center", va="bottom", fontsize=7,
             color=cost_color, transform=ax.transAxes, clip_on=False)

    time_color = TIME_OVER_COLOR if (ms_gap is not None and ms_gap > TIME_GAP_THRESHOLD) else NEUTRAL_COLOR
    time_text = f"time {ms_gap*100:.2f}%" if ms_gap is not None else "time ---"
    ax.text(0.5, 1.03, time_text, ha="center", va="bottom", fontsize=7,
             color=time_color, transform=ax.transAxes, clip_on=False)

    ax.set_xticks([]); ax.set_yticks([])
    for sp in ax.spines.values():
        sp.set_edgecolor("#dddddd")


def save_grid(bd, cond, gap_map, instances, out_path, title_suffix):
    ncols, nrows = 6, 8
    fig, axes = plt.subplots(nrows, ncols, figsize=(ncols * 2.6, nrows * 2.5))

    for idx, prefix in enumerate(instances):
        ax = axes[idx // ncols][idx % ncols]
        draw_panel(ax, bd, prefix, cond, gap_map)

    # 使わないパネル（instances<48のとき）を消す
    for idx in range(len(instances), nrows * ncols):
        axes[idx // ncols][idx % ncols].axis("off")

    fig.tight_layout(rect=[0, 0, 1, 0.90])

    handles = [plt.Line2D([0], [0], marker="*", color="w", markerfacecolor="#111111",
                           markersize=9, label="MIP")]
    for method, sty in SERIES_STYLE.items():
        handles.append(plt.Line2D([0], [0], marker=sty["marker"], color="w",
                                   markerfacecolor=sty["color"], markersize=7, label=sty["label"]))
    fig.legend(handles=handles, loc="upper center", ncol=4, fontsize=10, frameon=False,
               bbox_to_anchor=(0.5, 0.975))
    fig.suptitle(
        f"Front overview — {cond}{title_suffix}\n"
        f"each panel: MaxShiftDur cost gap (matched-makespan, red if >{COST_GAP_THRESHOLD*100:.0f}%) "
        f"and time gap (matched-cost, orange if >{TIME_GAP_THRESHOLD*100:.0f}%)",
        fontsize=10.5, y=0.995)

    fig.savefig(out_path, dpi=130, bbox_inches="tight")
    plt.close(fig)
    print(f"[SAVED] {out_path}")


def build_overview(bd, cond, gap_map, out_dir):
    instances = [f"j30{x}_1" for x in range(1, 49)]
    out_path = os.path.join(out_dir, f"overview_{cond}.png")
    save_grid(bd, cond, gap_map, instances, out_path, title_suffix="")


def build_overview_sorted_by_timegap(bd, cond, gap_map, out_dir):
    """工期ギャップ(ms_gap_mean)降順で並べた変種。除外/欠損は末尾に回す。"""
    instances = [f"j30{x}_1" for x in range(1, 49)]

    def sort_key(prefix):
        _, ms_gap, excluded = gap_map.get((prefix, cond, "MaxShiftDur"), (None, None, True))
        if excluded or ms_gap is None:
            return (1, 0.0)  # 除外/欠損は末尾
        return (0, -ms_gap)  # 降順

    instances_sorted = sorted(instances, key=sort_key)
    out_path = os.path.join(out_dir, f"overview_{cond}_sorted_by_timegap.png")
    save_grid(bd, cond, gap_map, instances_sorted, out_path,
              title_suffix="  (sorted by time gap, descending)")


def main():
    bd = "."
    gap_csv = os.path.join(bd, "analysis", "gap48", "gap_summary.csv")
    gap_map = load_gap_map(gap_csv)
    out_dir = os.path.join(bd, "figures", "FrontOverview")
    for cond in CONDITIONS:
        build_overview(bd, cond, gap_map, out_dir)
        build_overview_sorted_by_timegap(bd, cond, gap_map, out_dir)


if __name__ == "__main__":
    main()
