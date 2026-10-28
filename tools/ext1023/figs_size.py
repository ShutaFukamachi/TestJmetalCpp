"""
figs_size.py -- 10/21 指摘「j30 でやっているから 90, 120 でやるとどうなるか」
作業数 30・60・90・120 で、現在の手法（base）と最終設定（2集団 prbwisrgp）を比べる。
  左: 容量一定の条件（RR000_RV0 = 標準 RCPSP）で PSPLIB 最良既知値（j30 は最適値）との差
  右: 全6条件の共通 HV の差（最終設定 − 現在、±2SE）と改善/悪化の条件数
回数: j30 5回、j60 2回、j90 2回、j120 2回（60インスタンス）
出力: figures/Seminar1028/D5_size.png, analysis/ext1014/size_summary_1023.json
"""
import os, sys, json, statistics as S
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
SIZES = [("j30", 48, 5, "psplib_j30opt.txt"), ("j60", 48, 2, "psplib_j60hrs.txt"),
         ("j90", 48, 2, "psplib_j90hrs.txt"), ("j120", 60, 2, "psplib_j120hrs.txt")]
M = [m for m in (sys.argv[1].split(",") if len(sys.argv) > 1 else ["base", "prbwisrgp"])]
TAGS = {"dl": "t1024_dl"}  # 既定は t1016_<名前>
out = {}
for size, n, R, fn in SIZES:
    bks = {l.split()[0]: int(l.split()[1]) for l in open(os.path.join(ROOT, "tools", "ext1014", fn), encoding="utf-8")
           if l.strip() and not l.startswith("#")}
    gap = {m: [] for m in M}; d = {m: [] for m in M[1:]}; nc = 0
    for x in range(1, n + 1):
        p = f"{size}{x}_1"; sm = os.path.join(f"{size}.sm", p + ".sm")
        for c in CONDS:
            F = {}
            for m in M:
                for r in range(1, R + 1):
                    f = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_{TAGS.get(m, 't1016_' + m)}_r{r}.txt")
                    if os.path.exists(f):
                        pts, _ = G.load_valid_points(f, sm)
                        if pts:
                            F[(m, r)] = G.pareto_front(pts)
            if len(F) != len(M) * R:
                continue
            nc += 1
            pts = [q for f in F.values() for q in f]
            b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
            h = {k: G.hypervolume_2d(G.normalize_front(f, *b), (1.1, 1.1)) for k, f in F.items()}
            for m in M[1:]:
                d[m].append(S.mean(h[(m, r)] for r in range(1, R + 1)) - S.mean(h[("base", r)] for r in range(1, R + 1)))
            if c == "RR000_RV0":
                for m in M:
                    gap[m].append(S.mean((F[(m, r)][0][0] - bks[p]) / bks[p] * 100 for r in range(1, R + 1)))
    out[size] = dict(n_cond=nc, runs=R, n_gap=len(gap["base"]), gap={m: S.mean(gap[m]) for m in M},
                     hv={m: S.mean(d[m]) for m in M[1:]}, hv_2se={m: 2 * S.stdev(d[m]) / len(d[m]) ** .5 for m in M[1:]},
                     better={m: sum(v > 0 for v in d[m]) for m in M[1:]})
    print(size, out[size])
json.dump(out, open(os.path.join("analysis", "ext1014", "size_summary_" + "_".join(M) + ".json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
COLS = {"base": "#80868b", "prbwisrgp": "#d93025", "prtbw3": "#e8710a", "dl": "#188038"}
LABS = {"base": "現在の手法", "prbwisrgp": "最終設定（10/21：ブロック窓）", "prtbw3": "新しい最終設定（時間ブロック）", "dl": "＋目標工期（10/24 夜）"}
fig, axs = plt.subplots(1, 2, figsize=(13, 4.4))
xs = np.arange(len(SIZES)); w = .8 / len(M)
for i, m in enumerate(M):
    v = [out[sz[0]]["gap"][m] for sz in SIZES]
    axs[0].bar(xs + (i - (len(M) - 1) / 2) * w, v, w, color=COLS.get(m, "#999"), label=LABS.get(m, m))
    for k, vv in enumerate(v):
        axs[0].text(k + (i - (len(M) - 1) / 2) * w, vv + .15, f"{vv:.2f}", ha="center", fontsize=7)
axs[0].set_xticks(xs); axs[0].set_xticklabels([f"作業数{sz[0][1:]}\n（{out[sz[0]]['n_gap']}×{out[sz[0]]['runs']}回）" for sz in SIZES], fontsize=8)
axs[0].set_ylabel("PSPLIB 最良既知値との差（%）"); axs[0].set_title("工期最小端（容量一定の条件＝標準 RCPSP）", fontsize=10)
axs[0].legend(fontsize=8); axs[0].grid(axis="y", alpha=.3)
w2 = .8 / (len(M) - 1)
for i, m in enumerate(M[1:]):
    v = [out[sz[0]]["hv"][m] for sz in SIZES]; e = [out[sz[0]]["hv_2se"][m] for sz in SIZES]
    axs[1].bar(xs + (i - (len(M) - 2) / 2) * w2, v, w2, yerr=e, color=COLS.get(m, "#999"), capsize=3, label=LABS.get(m, m))
    for k, (vv, ee) in enumerate(zip(v, e)):
        axs[1].text(k + (i - (len(M) - 2) / 2) * w2, vv + ee + .0005, f"+{vv:.4f}\n{out[SIZES[k][0]]['better'][m]}/{out[SIZES[k][0]]['n_cond']}", ha="center", fontsize=7)
axs[1].axhline(0, color="k", lw=.8)
axs[1].set_xticks(xs); axs[1].set_xticklabels([f"作業数{sz[0][1:]}" for sz in SIZES], fontsize=8)
axs[1].set_ylabel("共通 HV の差（現在の手法との差、±2SE）"); axs[1].set_title("前線全体（全6条件、数字は改善した条件/全条件）", fontsize=10)
axs[1].legend(fontsize=8); axs[1].grid(axis="y", alpha=.3)
axs[1].set_ylim(0, max(out[sz[0]]["hv"][m] + out[sz[0]]["hv_2se"][m] for sz in SIZES for m in M[1:]) * 1.3)
fig.suptitle("作業数を増やしても効果は続くか（作業数30〜120）")
fig.tight_layout(); fig.savefig(os.path.join("figures", "Seminar1028", "D5_size" + ("" if len(M) == 2 else f"_{len(M)}") + ".png"), dpi=130)
print("saved")
