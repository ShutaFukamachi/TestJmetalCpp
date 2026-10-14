#!/usr/bin/env python3
"""
plot_prseed.py -- 指摘1の結果図: 工期最小端が MIP から最も離れていた10条件で、
MIP・公式設定の MaxShiftDur（base）・優先規則の作業リストを初期集団に入れた MaxShiftDur（prseed）の前線を重ねる。
出力: cmake-build-release/figures/MakespanEnd_EXT/prseed_<instance>_<cond>.png と一覧図 prseed_worst10.png
"""
import os, sys, csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
E = os.path.join("results", "EXT_COST")


def front(path, sm):
    pts, _ = G.load_valid_points(path, sm)
    return G.pareto_front(pts) if pts else []


rows = list(csv.DictReader(open(os.path.join("analysis", "ext1001", "prseed_eval.csv"), encoding="utf-8")))
rows = [r for r in rows if r["mip_min_ms"] not in ("", "None")]
worst = sorted(rows, key=lambda r: -float(r["base_ms_gap"]))[:10]
out = os.path.join("figures", "MakespanEnd_EXT"); os.makedirs(out, exist_ok=True)
fig, axes = plt.subplots(2, 5, figsize=(22, 8))
for ax, r in zip(axes.flat, worst):
    p, c = r["instance"], r["cond"]; sm = os.path.join("j30.sm", p + ".sm")
    mip = front(os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt"), sm)
    base = front(os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur.txt"), sm)
    seed = front(os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_prseed.txt"), sm)
    for a in (ax,):
        a.plot(*zip(*mip), "-o", color="#202124", ms=3, lw=1.3, label=f"MIP（最小工期 {float(r['mip_min_ms']):.0f}）")
        a.plot(*zip(*base), "-s", color="#9aa0a6", ms=2.5, lw=1, label=f"MaxShiftDur 公式設定（{float(r['base_min_ms']):.0f}）")
        a.plot(*zip(*seed), "-^", color="#e8710a", ms=2.5, lw=1, label=f"+優先規則の作業リスト（{float(r['prseed_min_ms']):.0f}）")
        a.set_title(f"{p} {c}", fontsize=9); a.grid(alpha=0.25); a.legend(fontsize=7)
        a.set_xlabel("メイクスパン")
    f2, a2 = plt.subplots(figsize=(7, 4.6))
    a2.plot(*zip(*mip), "-o", color="#202124", ms=3.5, lw=1.4, label=f"MIP（最小工期 {float(r['mip_min_ms']):.0f}）")
    a2.plot(*zip(*base), "-s", color="#9aa0a6", ms=3, lw=1, label=f"MaxShiftDur 公式設定（最小工期 {float(r['base_min_ms']):.0f}）")
    a2.plot(*zip(*seed), "-^", color="#e8710a", ms=3, lw=1, label=f"+優先規則の作業リストを初期集団に（最小工期 {float(r['prseed_min_ms']):.0f}）")
    a2.set_title(f"{p} {c}：工期最小端の改善", fontsize=10); a2.grid(alpha=0.25); a2.legend(fontsize=8)
    a2.set_xlabel("メイクスパン"); a2.set_ylabel("総コスト")
    f2.tight_layout(); f2.savefig(os.path.join(out, f"prseed_{p}_{c}.png"), dpi=130); plt.close(f2)
axes[0][0].set_ylabel("総コスト"); axes[1][0].set_ylabel("総コスト")
fig.suptitle("工期最小端が MIP から最も離れていた10条件：優先規則（LFT）で作った作業リストを初期集団に入れた効果", fontsize=11)
fig.tight_layout(); fig.savefig(os.path.join(out, "prseed_worst10.png"), dpi=120); plt.close(fig)
print("figures ->", out)
