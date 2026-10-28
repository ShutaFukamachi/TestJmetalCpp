"""
figs_timeblock.py -- max_shift の決め方（10/1 指摘6）への次の一歩：ブロックを「時間で区切る」と良くなるか
入力: analysis/ext1014/e2_eval_base_bw3_tbw3.json（j30、各5回）、size_j120_base_bw3_tbw3.json（j120、各3回）
出力: figures/Seminar1028/D6_timeblock.png
"""
import os, json, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
A = os.path.join(BD, "analysis", "ext1014")
J = json.load(open(os.path.join(A, "e2_eval_base_bw3_tbw3.json"), encoding="utf-8"))
K = json.load(open(os.path.join(A, "size_j120_base_bw3_tbw3.json"), encoding="utf-8"))
fig, axs = plt.subplots(1, 3, figsize=(13, 4))
for i, (m, lab, col) in enumerate([("bw3", "作業リストで\n隣り合う作業", "#a142f4"), ("tbw3", "開始時刻が\n近い作業", "#e8710a")]):
    v = J[m]["mid"]
    axs[0].scatter([i] * len(v), v, color=col, s=45, zorder=3)
    axs[0].plot([i - .25, i + .25], [S.mean(v)] * 2, color=col, lw=2.5)
    axs[0].text(i + .28, S.mean(v), f"{S.mean(v):.2f}%", va="center", fontsize=9)
axs[0].set_xticks([0, 1]); axs[0].set_xticklabels(["作業リストで\n隣り合う作業", "開始時刻が\n近い作業"], fontsize=9); axs[0].set_xlim(-.5, 1.9)
axs[0].set_ylabel("中間部の工期差（%）"); axs[0].set_title("j30：前線中間部（点は各回、5回）", fontsize=10); axs[0].grid(axis="y", alpha=.3)
h30 = J["hv"]["tbw3-base"]["mean"] - J["hv"]["bw3-base"]["mean"]
axs[1].bar([0, 1], [J["hv"]["bw3-base"]["mean"], J["hv"]["tbw3-base"]["mean"]],
           yerr=[J["hv"]["bw3-base"]["se2"], J["hv"]["tbw3-base"]["se2"]], color=["#a142f4", "#e8710a"], capsize=4)
axs[1].set_xticks([0, 1]); axs[1].set_xticklabels(["隣り合う作業", "時刻が近い作業"], fontsize=9)
axs[1].set_ylabel("共通 HV の差（現在の手法との差、±2SE）"); axs[1].set_title("j30：前線全体", fontsize=10); axs[1].grid(axis="y", alpha=.3)
axs[2].bar([0, 1], [K["bw3"]["hv_diff"], K["tbw3"]["hv_diff"]], yerr=[K["bw3"]["hv_2se"], K["tbw3"]["hv_2se"]],
           color=["#a142f4", "#e8710a"], capsize=4)
axs[2].set_xticks([0, 1]); axs[2].set_xticklabels(["隣り合う作業", "時刻が近い作業"], fontsize=9)
axs[2].set_ylabel("共通 HV の差（現在の手法との差、±2SE）"); axs[2].set_title("j120（20インスタンス×3回）：前線全体", fontsize=10); axs[2].grid(axis="y", alpha=.3)
fig.suptitle("窓幅をまとめて動かす作業の選び方（確率0.3、工期島＋部分再構築に追加）")
fig.tight_layout(); fig.savefig(os.path.join(BD, "figures", "Seminar1028", "D6_timeblock.png"), dpi=130)
print("saved", round(h30, 4))
