#!/usr/bin/env python3
"""
visualize_ext_comparison.py
──────────────────────────────────────────────────────────────────────────
拡張コスト表（costs_ext/）での Pareto フロント比較（2026-10-01 ゼミ用）。
既存の visualize_mip_comparison.py の拡張コスト表版。

1 枚 = 1 (instance, condition)。重ねるもの:
  - MIP（拡張コスト表）        黒の実線・丸
  - 旧 MIP（旧コスト表）        灰色の点線（参考。右側はコスト表の不具合による見かけの解を含む）
  - SchedObj / MaxShift / MaxShiftDur（拡張コスト表の NSGA-II）
右上に、その条件の MIP 比 HV（公式と同じ定義）を表示する。

出力:
  build_dir/figures/MIPComparison_EXT/ext_{prefix}_{cond}.png      （j30）
  build_dir/figures/Size_EXT/ext_{prefix}_{cond}.png                （j60: MIP なし、3手法のみ）

使い方:
  cd cmake-build-release
  python ../visualize_ext_comparison.py
  python ../visualize_ext_comparison.py --prefix j309_1 --cond RR000_RV1
"""
import argparse, os, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tools"))
import gap48_analysis as G  # noqa: E402

plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
plt.rcParams["axes.unicode_minus"] = False
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
STYLE = {"SchedObj": ("#9aa0a6", "v"), "MaxShift": ("#4c8bf5", "s"), "MaxShiftDur": ("#e8710a", "o")}


def load(bd, root, kind, prefix, cond, method=None, size="j30"):
    base = os.path.join(bd, "results", "EXT_COST") if root == "ext" else os.path.join(bd, "results")
    sm = os.path.join(bd, f"{size}.sm", f"{prefix}.sm")
    if kind == "MIP":
        p = os.path.join(base, "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
    else:
        p = os.path.join(base, "FUN_ENC", prefix,
                         "SCHED_ENC_" + G.METHODS[method].format(prefix=prefix, cond=cond) + ".txt")
    pts, st = G.load_valid_points(p, sm)
    return G.pareto_front(pts) if pts else []


def hv_ratio(mip, f):
    if not mip or not f:
        return None
    pts = mip + f
    b = (min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts), max(p[1] for p in pts))
    hm = G.hypervolume_2d(G.normalize_front(mip, *b), (1.1, 1.1))
    hn = G.hypervolume_2d(G.normalize_front(f, *b), (1.1, 1.1))
    return hn / hm if hm > 0 else None


def plot_one(bd, prefix, cond, size, out_dir):
    fr = {m: load(bd, "ext", "ENC", prefix, cond, m, size) for m in STYLE}
    if not any(fr.values()):
        return False
    mip = load(bd, "ext", "MIP", prefix, cond, size=size) if size == "j30" else []
    old = load(bd, "old", "MIP", prefix, cond, size=size) if size == "j30" else []
    fig, ax = plt.subplots(figsize=(7.0, 4.6))
    if old:
        ax.plot(*zip(*old), ":", color="#bdbdbd", lw=1.2, label="旧MIP（旧コスト表・参考）")
    if mip:
        ax.plot(*zip(*mip), "-o", color="#202124", ms=3.5, lw=1.4, label=f"MIP（拡張コスト表, {len(mip)}点）")
    txt = []
    for m, (c, mk) in STYLE.items():
        if fr[m]:
            ax.plot(*zip(*fr[m]), "-" + mk, color=c, ms=3, lw=1.0, alpha=0.9, label=f"{m}（{len(fr[m])}点）")
            r = hv_ratio(mip, fr[m])
            if r is not None:
                txt.append(f"{m}: HV比 {r:.3f}")
    if txt:
        ax.text(0.98, 0.97, "\n".join(txt), transform=ax.transAxes, ha="right", va="top", fontsize=8,
                bbox=dict(boxstyle="round", fc="white", ec="#dadce0"))
    ax.set_xlabel("メイクスパン"); ax.set_ylabel("総コスト")
    ax.set_title(f"{prefix}  {cond}（拡張コスト表）", fontsize=10)
    ax.legend(fontsize=7, loc="lower left")
    ax.grid(alpha=0.25)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f"ext_{prefix}_{cond}.png"), dpi=130)
    plt.close(fig)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=".")
    ap.add_argument("--prefix"); ap.add_argument("--cond")
    a = ap.parse_args()
    bd = a.build_dir
    n = 0
    for size, folder in [("j30", "MIPComparison_EXT"), ("j60", "Size_EXT")]:
        out = os.path.join(bd, "figures", folder); os.makedirs(out, exist_ok=True)
        for x in range(1, 49):
            p = f"{size}{x}_1"
            if a.prefix and p != a.prefix:
                continue
            for c in CONDS:
                if a.cond and c != a.cond:
                    continue
                n += plot_one(bd, p, c, size, out)
    print(f"[visualize_ext_comparison] {n} figures")


if __name__ == "__main__":
    main()
