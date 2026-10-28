"""
mid_two_views.py -- 前線の位置ごとの MIP との差を、2つの見方で並べる（j30・MIP がある240条件・各5回）
  横の差: MIP の各点と同じかより安いコストで NSGA が出せる最小工期の差（%）
  縦の差: MIP の各点と同じかより短い工期で NSGA が出せる最小コストの差（%）
前線の位置 = (MIP 点の工期 − MIP 最小工期) / (MIP 最大工期 − MIP 最小工期)
出力: figures/Search1014/fig7_mid_two_views.png, analysis/ext1014/mid_two_views.csv
"""
import os, sys, csv, statistics as S
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
E = os.path.join("results", "EXT_COST")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
NB = 5
acc = {m: {"h": [[] for _ in range(NB)], "v": [[] for _ in range(NB)]} for m in ["base", "isrgp"]}
for x in range(1, 49):
    p = f"j30{x}_1"; sm = os.path.join("j30.sm", p + ".sm")
    for c in CONDS:
        mp = os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt")
        if not os.path.exists(mp):
            continue
        pts, _ = G.load_valid_points(mp, sm)
        M = G.pareto_front(pts) if pts else []
        if len(M) < 3 or M[-1][0] == M[0][0]:
            continue
        for m in acc:
            for r in range(1, 6):
                f = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_t1016_{m}_r{r}.txt")
                if not os.path.exists(f):
                    continue
                q, _ = G.load_valid_points(f, sm); N = G.pareto_front(q)
                for ms, co in M:
                    b = min(NB - 1, int((ms - M[0][0]) / (M[-1][0] - M[0][0]) * NB))
                    h = G.min_ms_at_or_below_cost(N, co)
                    v = G.min_cost_at_or_before(N, ms)
                    if h is not None:
                        acc[m]["h"][b].append((h - ms) / ms * 100)
                    if v is not None:
                        acc[m]["v"][b].append((v - co) / co * 100)
lab = [f"{k/NB:.1f}〜{(k+1)/NB:.1f}" for k in range(NB)]
fig, axs = plt.subplots(1, 2, figsize=(11, 4))
rows = []
for ax, key, title, yl in [(axs[0], "h", "横の差：同じコストで工期が何%長いか", "工期の差（%）"),
                           (axs[1], "v", "縦の差：同じ工期でコストが何%高いか", "コストの差（%）")]:
    w = .38
    for i, (m, col, name) in enumerate([("base", "#80868b", "現在の手法"), ("isrgp", "#188038", "改善版")]):
        vals = [S.mean(v) if v else np.nan for v in acc[m][key]]
        ax.bar(np.arange(NB) + (i - .5) * w, vals, w, color=col, label=name)
        for k, v in enumerate(vals):
            ax.text(k + (i - .5) * w, v, f"{v:.1f}" if key == "h" else f"{v:.2f}", ha="center", va="bottom", fontsize=7)
            rows.append(dict(view=key, method=m, position=lab[k], mean_gap_pct=v, n=len(acc[m][key][k])))
    ax.set_xticks(range(NB)); ax.set_xticklabels(lab, fontsize=8)
    ax.set_xlabel("前線の位置（0 = 工期最小端、1 = コスト最小端）"); ax.set_ylabel(yl); ax.set_title(title, fontsize=10)
    ax.legend(fontsize=8); ax.grid(axis="y", alpha=.3)
fig.suptitle("j30・MIP がある条件（各5回）：前線の位置ごとの MIP との差")
fig.tight_layout(); fig.savefig(os.path.join("figures", "Search1014", "fig7_mid_two_views.png"), dpi=130)
with open(os.path.join("analysis", "ext1014", "mid_two_views.csv"), "w", newline="", encoding="utf-8") as fp:
    wr = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); wr.writeheader(); wr.writerows(rows)
for r in rows:
    print(r)
