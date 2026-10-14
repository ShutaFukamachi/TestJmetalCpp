#!/usr/bin/env python3
"""
plot_p123_vs_nsga.py -- P1/P2/P3 それぞれで、ソルバー（MIP）と提案手法（NSGA-II MaxShiftDur）の前線を比べる。
（2026-10-01 ゼミ指摘「P2、P3 をソルバーで解いてグラフで比較したい」への回答。拡張コスト表）

入力（すべて results/EXT_COST/）:
  MIP  P1: FUN_MIP/j309_1/SCHED_MIP_j309_1_<cond>.txt
  MIP  P2: FUN_MIP_P2/j309_1/                （RR000_RV1 のみ。RV0 は容量が一定なので P2=P1）
  MIP  P3: FUN_MIP_P3SLOT/j309_1/             （新定式化。P1 解を初期解に使用。全点時間切れ＝暫定解）
  NSGA   : FUN_ENC/j309_1/SCHED_ENC_j309_1_<cond>[_P2|_P3]_MaxShiftDur_p23.txt
出力:
  cmake-build-release/figures/Splitting_EXT/p123_mip_vs_nsga_j309_1.png      MIP と NSGA（2条件×3モード）
  cmake-build-release/figures/Splitting_EXT/p123_nsga_only_j309_1.png        NSGA の P1/P2/P3（6条件）
  cmake-build-release/analysis/ext1001/p123_vs_nsga.csv                     数値（HV比・コストgap・到達率）
"""
import csv, os, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gap48_analysis as G  # noqa: E402

plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
plt.rcParams["axes.unicode_minus"] = False
E = os.path.join(BD, "results", "EXT_COST")
SM = os.path.join(BD, "j30.sm", "j309_1.sm")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
MODE_LABEL = {"P1": "P1（分割なし）", "P2": "P2（休暇時のみ中断）", "P3": "P3（任意中断）"}
COL = {"P1": "#202124", "P2": "#d93025", "P3": "#188038"}


METHOD_STYLE = {"SchedObj": ("#9aa0a6", "v"), "MaxShift": ("#4c8bf5", "^"), "MaxShiftDur": ("#e8710a", "s")}


def nsga(cond, mode, method="MaxShiftDur"):
    if method == "MaxShiftDur":
        tag = f"{cond}_MaxShiftDur_p23" if mode == "P1" else f"{cond}_{mode}_MaxShiftDur_p23"
    else:  # NSGAEncCpp の出力（P1 は同じ実行の結果を _p23 として退避済み）
        tag = f"{cond}_P1_{method}_p23" if mode == "P1" else f"{cond}_{mode}_{method}"
    pts, _ = G.load_valid_points(os.path.join(E, "FUN_ENC", "j309_1", f"SCHED_ENC_j309_1_{tag}.txt"), SM)
    return G.pareto_front(pts) if pts else []


def mip(cond, mode):
    d = {"P1": ("FUN_MIP", "MIP"), "P2": ("FUN_MIP_P2", "MIP_P2"), "P3": ("FUN_MIP_P3SLOT", "MIP_P3SLOT")}[mode]
    f = os.path.join(E, d[0], "j309_1", f"FUN_{d[1]}_j309_1_{cond}.txt")
    g = os.path.join(E, d[0], "j309_1", f"GAP_{d[1]}_j309_1_{cond}.csv")
    if not os.path.exists(f):
        return [], None
    pts = sorted(tuple(map(float, l.split()[:2])) for l in open(f) if l.strip())
    proven = None
    if os.path.exists(g):
        rows = list(csv.DictReader(open(g)))
        proven = (sum(1 for r in rows if r["optimal"] in ("1", "1.0")), len(rows))
    return G.pareto_front(pts), proven


def metrics(m, n):
    if not m or not n:
        return {}
    pts = m + n
    b = (min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts), max(p[1] for p in pts))
    hm = G.hypervolume_2d(G.normalize_front(m, *b), (1.1, 1.1)); hn = G.hypervolume_2d(G.normalize_front(n, *b), (1.1, 1.1))
    cg = [(G.min_cost_at_or_before(n, ms) - c) / c for ms, c in m if G.min_cost_at_or_before(n, ms) is not None]
    return dict(hv_ratio=hn / hm if hm > 0 else None, cost_gap=sum(cg) / len(cg) if cg else None,
                reach=G.c_metric(n, m), nsga_beats=sum(1 for a, c in m if any(x <= a and y < c - 1 for x, y in n)))


def main():
    out_fig = os.path.join(BD, "figures", "Splitting_EXT"); os.makedirs(out_fig, exist_ok=True)
    rows = []
    fig, axes = plt.subplots(2, 3, figsize=(15, 8.4))
    for r, cond in enumerate(["RR000_RV0", "RR000_RV1"]):
        for c, mode in enumerate(["P1", "P2", "P3"]):
            ax = axes[r][c]
            m, proven = mip(cond, mode)
            if mode == "P2" and cond == "RR000_RV0" and not m:
                m, proven = mip(cond, "P1")   # 容量一定・休暇なしでは P2 と P1 は同じ問題
                note = "（休暇も容量削減もないので P2=P1。MIP は P1 の前線）"
            else:
                note = ""
            if m:
                lab = "ソルバー（MIP）"
                if proven and proven[0] < proven[1]:
                    lab += f"・最適性証明 {proven[0]}/{proven[1]} 点"
                ax.plot(*zip(*m), "-o", color="#202124", ms=3.5, lw=1.4, label=lab)
            txt = []
            for meth, (colr, mk) in METHOD_STYLE.items():
                f = nsga(cond, mode, meth)
                if not f:
                    continue
                ax.plot(*zip(*f), "-" + mk, color=colr, ms=2.5, lw=1.0, alpha=0.9, label=f"{meth}（{len(f)}点）")
                met = metrics(m, f)
                if met:
                    txt.append(f"{meth}: HV比 {met['hv_ratio']:.3f}")
                    rows.append(dict(instance="j309_1", cond=cond, mode=mode, method=meth, mip_points=len(m),
                                     mip_proven=(f"{proven[0]}/{proven[1]}" if proven else ""), nsga_points=len(f), **met))
            if txt:
                ax.text(0.98, 0.97, "\n".join(txt), transform=ax.transAxes, ha="right", va="top", fontsize=7.5,
                        bbox=dict(boxstyle="round", fc="white", ec="#dadce0"))
            ax.set_title(f"{cond}  {MODE_LABEL[mode]}" + ("\n" + note if note else ""), fontsize=9)
            ax.set_xlabel("メイクスパン"); ax.grid(alpha=0.25); ax.legend(fontsize=7, loc="lower left")
            if c == 0:
                ax.set_ylabel("総コスト")
    fig.suptitle("j309_1：ソルバー（MIP）と NSGA-II 3手法（SchedObj / MaxShift / MaxShiftDur）の比較　P1・P2・P3（拡張コスト表）\n"
                 "P3 の MIP は全点が制限時間120秒で打ち切り（暫定解）。P1 解を初期解に与えているので P1 より悪くはならない",
                 fontsize=10)
    fig.tight_layout(); fig.savefig(os.path.join(out_fig, "p123_mip_vs_nsga_j309_1.png"), dpi=140); plt.close(fig)

    # NSGA のみ: 6 条件で P1/P2/P3 を重ねる（作業分割の効果）
    fig, axes = plt.subplots(2, 3, figsize=(15, 8))
    for ax, cond in zip(axes.flat, CONDS):
        for mode in ["P1", "P2", "P3"]:
            n = nsga(cond, mode)
            if n:
                ax.plot(*zip(*n), "-o", ms=2.5, lw=1.1, color=COL[mode], label=f"{MODE_LABEL[mode]}（{len(n)}点）")
        ax.set_title(f"j309_1 {cond}", fontsize=9); ax.grid(alpha=0.25); ax.legend(fontsize=7)
        ax.set_xlabel("メイクスパン"); ax.set_ylabel("総コスト")
    fig.suptitle("提案手法（MaxShiftDur）の前線：作業分割 P1・P2・P3 の違い（j309_1、拡張コスト表）", fontsize=10)
    fig.tight_layout(); fig.savefig(os.path.join(out_fig, "p123_nsga_only_j309_1.png"), dpi=140); plt.close(fig)

    keys = ["instance", "cond", "mode", "method", "mip_points", "mip_proven", "nsga_points", "hv_ratio", "cost_gap", "reach", "nsga_beats"]
    with open(os.path.join(BD, "analysis", "ext1001", "p123_vs_nsga.csv"), "w", newline="", encoding="utf-8") as fp:
        w = csv.DictWriter(fp, fieldnames=keys); w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, "") for k in keys})
    for r in rows:
        print(r)


if __name__ == "__main__":
    main()
