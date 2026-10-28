"""
figs_mid_profile.py -- 前線中間部の工期差を、前線上の位置と条件（休暇なし RV0 / 休暇あり RV1）ごとに見る（j30、各設定の1回目）
入力: analysis/ext1024/mid_delay_sols_{prtbw3,dl,dlcal}.csv（mid_delay_diag.py の出力）
出力: figures/Seminar1028b/M3_profile.png
"""
import os
import pandas as pd
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
A = os.path.join(BD, "analysis", "ext1024")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
S = [("prtbw3", "新しい最終設定", "#e8710a"), ("dl", "＋目標工期（採用）", "#188038"), ("dlcal", "＋暦つき目標工期（不採用）", "#9aa0a6")]
bins = [0.2, 0.35, 0.5, 0.65, 0.8]
fig, axs = plt.subplots(1, 2, figsize=(12, 4))
for name, lab, col in S:
    f = os.path.join(A, f"mid_delay_sols_{name}.csv")
    if not os.path.exists(f):
        continue
    d = pd.read_csv(f); n = d[d.who == "NSGA"].copy()
    n["bin"] = pd.cut(n.pos, bins, include_lowest=True)
    v = n.groupby("bin", observed=True).gap_ms.mean()
    axs[0].plot([(a + b) / 2 for a, b in zip(bins, bins[1:])], v.values, marker="o", color=col, label=lab, ls="--" if name == "dlcal" else "-")
    n["rv"] = n.key.str.split("|").str[1].str[-3:]
    w = n.groupby("rv").gap_ms.mean()
    off = {"prtbw3": -.25, "dl": 0, "dlcal": .25}[name]
    axs[1].bar([0 + off, 1 + off], [w["RV0"], w["RV1"]], .25, color=col, label=lab)
    for x, y in [(0 + off, w["RV0"]), (1 + off, w["RV1"])]:
        axs[1].text(x, y + .1, f"{y:.1f}", ha="center", fontsize=8)
axs[0].set_xlabel("前線上の位置（0 = 工期最小側、1 = コスト最小側。MIP の前線の工期で正規化）")
axs[0].set_ylabel("同じコストでの工期差（%）"); axs[0].set_title("位置ごと", fontsize=10); axs[0].grid(alpha=.3); axs[0].legend(fontsize=8)
axs[1].set_xticks([0, 1]); axs[1].set_xticklabels(["休暇なし（RV0）", "休暇あり（RV1）"])
axs[1].set_ylabel("同じコストでの工期差（%）"); axs[1].set_title("条件ごと", fontsize=10); axs[1].grid(axis="y", alpha=.3); axs[1].legend(fontsize=8)
fig.suptitle("前線中間部の工期差（j30 全48×6条件、各設定の1回目、MIP の中間点と同じコスト以下の点を対にして比較）")
fig.tight_layout(); fig.savefig(os.path.join(BD, "figures", "Seminar1028b", "M3_profile.png"), dpi=130)
print("saved")
