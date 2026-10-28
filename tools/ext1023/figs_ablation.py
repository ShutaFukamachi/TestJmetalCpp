"""
figs_ablation.py -- 10/21 指摘「工期島はなぜ効くのか」「1集団でできないか」の図
入力: analysis/ext1014/e2_eval_base_isrgp_rg_zw_ms_mszw_mate_matep_matef_matef0.json
出力: figures/Seminar1028/D2_ablation.png
"""
import os, json, statistics as S
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
J = json.load(open(os.path.join(BD, "analysis", "ext1014", "e2_eval_base_isrgp_rg_zw_ms_mszw_mate_matep_matef_matef0.json"), encoding="utf-8"))
ORDER = ["base", "rg", "ms", "zw", "mszw", "mate", "matep", "matef", "matef0", "isrgp"]
LAB = {"base": "現在の手法", "rg": "部分再構築\nのみ", "ms": "(A) 工期で\n親を選ぶ", "zw": "(B) 子の窓幅\nを0に", "mszw": "(A)+(B)",
       "mate": "工期側の両親\n→窓幅0の子", "matep": "同左＋\n停滞で停止", "matef": "同左＋停止\n＋改善のみ受入", "matef0": "工期側の両親→\n窓幅0、改善のみ受入",
       "isrgp": "工期島\n（2集団）"}
COL = {"base": "#80868b", "rg": "#9aa0a6", "ms": "#f9ab00", "zw": "#1a73e8", "mszw": "#a142f4", "mate": "#4285f4",
       "matep": "#8ab4f8", "matef": "#81c995", "matef0": "#188038", "isrgp": "#d93025"}
fig, axs = plt.subplots(3, 1, figsize=(12, 10), sharex=True)
x = np.arange(len(ORDER))
ex = [S.mean(J[m]["exact"]) for m in ORDER]; lo = [min(J[m]["exact"]) for m in ORDER]; hi = [max(J[m]["exact"]) for m in ORDER]
axs[0].bar(x, ex, color=[COL[m] for m in ORDER])
for i in range(len(ORDER)):
    axs[0].plot([i, i], [lo[i], hi[i]], color="k", lw=1.2); axs[0].text(i, hi[i] + 1, f"{ex[i]:.1f}", ha="center", fontsize=8)
axs[0].axhline(min(J["isrgp"]["exact"]), color="#d93025", ls="--", lw=1)
axs[0].set_ylim(180, 245); axs[0].set_ylabel("工期端が MIP と一致\n（/240）")
rc = [S.mean(J[m]["reach"]) * 100 for m in ORDER]
axs[1].bar(x, rc, color=[COL[m] for m in ORDER])
for i, v in enumerate(rc):
    axs[1].text(i, v + 1, f"{v:.0f}%", ha="center", fontsize=8)
axs[1].axhline(min(J["isrgp"]["reach"]) * 100, color="#d93025", ls="--", lw=1)
axs[1].set_ylabel("初期で届かなかった条件で\n探索が MIP に到達（%）")
hv = [J["hv"].get(f"{m}-isrgp", {"mean": 0, "se2": 0}) for m in ORDER]
axs[2].bar(x, [h["mean"] for h in hv], yerr=[h["se2"] for h in hv], color=[COL[m] for m in ORDER], capsize=3)
axs[2].axhline(0, color="k", lw=.8)
axs[2].set_ylabel("共通 HV の差\n（工期島との差、±2SE）")
axs[2].set_xticks(x); axs[2].set_xticklabels([LAB[m] for m in ORDER], fontsize=8)
for a in axs:
    a.grid(axis="y", alpha=.3)
fig.suptitle("工期島の何が効いているか：1つの集団で工期島の働きを一つずつ再現（j30 全条件、各3回／現在・工期島は5回）\n赤い破線 = 工期島の5回の最小値（1集団で十分と判定する基準）")
fig.tight_layout()
os.makedirs(os.path.join(BD, "figures", "Seminar1028"), exist_ok=True)
fig.savefig(os.path.join(BD, "figures", "Seminar1028", "D2_ablation.png"), dpi=130)
print("saved")
