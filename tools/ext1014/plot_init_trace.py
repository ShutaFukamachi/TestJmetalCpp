"""
plot_init_trace.py -- 10/14 指摘「初期解の分布をグラフで見てみる」

NSGA が実際に使った初期集団（TRACE_*.csv の init 行）を図にする。
  図1 init_scatter_<inst>_<cond>.png : 初期集団（ランダム／優先規則＋ランダム性）を目的空間に打ち、
                                       MIP 前線と最終前線（b2 / 改善版）を重ねる
  図2 init_listms_pooled.png         : MIP がある全条件で、初期個体の作業リストの工期（窓幅0）を
                                       MIP の最小工期との比で集計した分布（ランダム vs 優先規則＋ランダム性）
使い方: python plot_init_trace.py [改善版のタグ=t1014isrg]
"""
import os, sys, glob
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

BEST = sys.argv[1] if len(sys.argv) > 1 else "t1014isrg"
E = os.path.join("results", "EXT_COST")
OUT = os.path.join("figures", "Init_EXT"); os.makedirs(OUT, exist_ok=True)
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]


def init_rows(prefix, cond, tag):
    rows = []
    for f in glob.glob(os.path.join(E, "TRACE", prefix, f"TRACE_{prefix}_{cond}_S*_{tag}.csv")):
        for ln in open(f):
            p = ln.strip().split(",")
            if p[0] == "init" and float(p[3]) < 1e8 and float(p[5]) < 1e8:   # 実行不能（罰則値 1e9）は除く
                rows.append((p[2], float(p[3]), float(p[4]), float(p[5])))
    return rows


def front(prefix, cond, suffix):
    path = os.path.join(E, "FUN_ENC", prefix, f"SCHED_ENC_{prefix}_{cond}_MaxShiftDur{suffix}.txt")
    if not os.path.exists(path):
        return []
    pts, _ = G.load_valid_points(path, os.path.join("j30.sm", prefix + ".sm"))
    return G.pareto_front(pts)


def mip_front(prefix, cond):
    path = os.path.join(E, "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
    if not os.path.exists(path):
        return []
    pts, _ = G.load_valid_points(path, os.path.join("j30.sm", prefix + ".sm"))
    return G.pareto_front(pts)


# ---- 図1: 代表条件の散布図 ----
for prefix, cond in [("j309_1", "RR050_RV0"), ("j309_1", "RR025_RV0"), ("j3011_1", "RR050_RV0"), ("j3014_1", "RR050_RV0")]:
    rr = init_rows(prefix, cond, "t1014pr")
    if not rr:
        continue
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    for org, col, lab in [("random", "#9aa0a6", "初期解：ランダム"), ("pr", "#e8710a", "初期解：優先規則＋ランダム性"),
                          ("ext_ms", "#1a73e8", "初期解：極端解（窓幅0）"), ("ext_cost", "#188038", "初期解：極端解（窓幅最大）")]:
        q = [r for r in rr if r[0] == org]
        if q:
            ax.scatter([r[1] for r in q], [r[2] for r in q], s=10, c=col, alpha=.55, label=lab, edgecolors="none")
    m = mip_front(prefix, cond)
    if m:
        ax.plot(*zip(*m), "k.-", lw=1.4, ms=5, label=f"MIP 前線（最小工期 {m[0][0]:.0f}）")
    for suf, col, lab in [("_t1014base", "#5f6368", "最終前線：現在の手法"), (f"_{BEST}", "#d93025", f"最終前線：改善版（{BEST}）")]:
        f = front(prefix, cond, suf)
        if f:
            ax.plot(*zip(*f), "-", color=col, lw=1.6, label=f"{lab}（最小工期 {f[0][0]:.0f}）")
    ax.set_xlabel("メイクスパン"); ax.set_ylabel("総コスト")
    ax.set_title(f"{prefix} {cond}：初期集団の分布（4戦略×100個体）と最終前線")
    ax.legend(fontsize=7.5); ax.grid(alpha=.3); fig.tight_layout()
    fig.savefig(os.path.join(OUT, f"init_scatter_{prefix}_{cond}.png"), dpi=130); plt.close(fig)

# ---- 図2: 全条件の集計（作業リストの工期 / MIP 最小工期）----
ratio = {"random": [], "pr": []}; best = {"random": [], "pr": []}
for x in range(1, 49):
    prefix = f"j30{x}_1"
    for cond in CONDS:
        m = mip_front(prefix, cond)
        rr = init_rows(prefix, cond, "t1014pr")
        if not m or not rr:
            continue
        mm = m[0][0]
        for org in ratio:
            v = [r[3] / mm for r in rr if r[0] == org]
            if v:
                ratio[org] += v; best[org].append(min(v))
fig, axs = plt.subplots(1, 2, figsize=(10, 4))
bins = np.arange(1.0, 1.8, 0.02)
for org, col, lab in [("random", "#9aa0a6", "ランダム"), ("pr", "#e8710a", "優先規則＋ランダム性 (a=3)")]:
    axs[0].hist(ratio[org], bins=bins, color=col, alpha=.6, density=True, label=f"{lab}（{len(ratio[org])} 個体）")
    axs[1].hist(best[org], bins=np.arange(1.0, 1.4, 0.01), color=col, alpha=.6, label=f"{lab}（{len(best[org])} 条件）")
axs[0].set_xlabel("初期個体の作業リストの工期 ÷ MIP の最小工期"); axs[0].set_ylabel("割合"); axs[0].set_title("初期個体1つ1つ")
axs[1].set_xlabel("初期集団の中の最良 ÷ MIP の最小工期"); axs[1].set_ylabel("条件数"); axs[1].set_title("条件ごとの最良（1.00 = MIP と一致）")
for a in axs:
    a.legend(fontsize=8); a.grid(alpha=.3)
fig.suptitle("j30・MIP がある条件：初期解の作業リストの質（窓幅0で復号）")
fig.tight_layout(); fig.savefig(os.path.join(OUT, "init_listms_pooled.png"), dpi=130); plt.close(fig)
for org in ratio:
    v = np.array(ratio[org]); b = np.array(best[org])
    print(f"{org:6s}: individuals {len(v)} median ratio {np.median(v):.3f}; best-in-pop median {np.median(b):.3f}, "
          f"best == MIP in {np.mean(b <= 1.0 + 1e-9):.1%} of conditions")
