"""
init_j120.py -- j120 の初期集団の分布（10/14 指摘「初期解の分布をグラフで」の 120 作業版）
初期解の半分を優先規則にした実行（t1016_pr_r*）の init 行から、ランダム／優先規則の作業リストの工期を
容量一定の条件（RR000_RV0）で PSPLIB 最良既知値と比べる。
出力: figures/Init_EXT/init_j120.png と標準出力
"""
import os, glob, statistics as S
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
bks = {l.split()[0]: int(l.split()[1]) for l in open(os.path.join(HERE, "psplib_j120hrs.txt"), encoding="utf-8")
       if l.strip() and not l.startswith("#")}
ratio = {"random": [], "pr": []}; best = {"random": [], "pr": []}; ninf = {"random": [0, 0], "pr": [0, 0]}
for x in range(1, 21):
    p = f"j120{x}_1"
    for f in glob.glob(os.path.join(BD, "results", "EXT_COST", "TRACE", p, f"TRACE_{p}_RR000_RV0_S*_t1016_pr_r*.csv")):
        v = {"random": [], "pr": []}
        for ln in open(f):
            q = ln.split(",")
            if q[0] == "init" and q[2] in v:
                ninf[q[2]][1] += 1
                if float(q[3]) >= 1e8:
                    ninf[q[2]][0] += 1
                if float(q[5]) < 1e8:
                    v[q[2]].append(float(q[5]) / bks[p])
        for o in v:
            if v[o]:
                ratio[o] += v[o]; best[o].append(min(v[o]))
fig, axs = plt.subplots(1, 2, figsize=(10, 3.8))
for o, col, lab in [("random", "#9aa0a6", "ランダム（現在）"), ("pr", "#e8710a", "4規則混合")]:
    if not ratio[o]:
        continue
    axs[0].hist(ratio[o], bins=np.arange(1.0, 2.2, 0.025), color=col, alpha=.6, density=True, label=f"{lab}（{len(ratio[o])} 個体）")
    axs[1].hist(best[o], bins=np.arange(1.0, 1.6, 0.02), color=col, alpha=.6, label=f"{lab}（{len(best[o])} 集団）")
    print(f"{o}: median {S.median(ratio[o]):.3f}, best-in-pop median {S.median(best[o]):.3f}, "
          f"infeasible {ninf[o][0]}/{ninf[o][1]}")
axs[0].set_xlabel("作業リストの工期 ÷ PSPLIB 最良既知値"); axs[0].set_ylabel("割合"); axs[0].set_title("初期個体1つ1つ")
axs[1].set_xlabel("集団の中の最良 ÷ 最良既知値"); axs[1].set_ylabel("集団の数"); axs[1].set_title("戦略ごとの初期集団の最良")
for a in axs:
    a.legend(fontsize=8); a.grid(alpha=.3)
fig.suptitle("j120・容量一定の20条件：初期解の作業リストの質（窓幅0で復号）")
fig.tight_layout(); fig.savefig(os.path.join(BD, "figures", "Init_EXT", "init_j120.png"), dpi=130)
