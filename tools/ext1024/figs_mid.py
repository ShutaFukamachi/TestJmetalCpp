"""
figs_mid.py -- 前線中間部の診断と目標工期の遺伝子の結果（2026-10-24〜25）
  M1_chain.png : 中間部の工期を「鎖」に沿って分解（MIP / 新しい最終設定 / 目標工期つき）
  M2_deadline.png : j30 の中間部（各回）・工期端・HV、j120 の HV（目標工期つき − 新しい最終設定）
入力: analysis/ext1024/mid_chain_prtbw3.csv, mid_chain_dl.csv, analysis/ext1014/e2_eval_base_prtbw3_dl.json,
      analysis/ext1024/dl_j120.json（あれば）
"""
import os, json
import pandas as pd
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
A = os.path.join(BD, "analysis", "ext1024")
OUT = os.path.join(BD, "figures", "Seminar1028b"); os.makedirs(OUT, exist_ok=True)
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]

a = pd.read_csv(os.path.join(A, "mid_chain_prtbw3.csv")); b = pd.read_csv(os.path.join(A, "mid_chain_dl.csv"))
m = a[a.who == "MIP"].set_index("key"); a = a[a.who == "NSGA"].set_index("key"); b = b[b.who == "NSGA"].set_index("key")
k = a.index.intersection(b.index).intersection(m.index)
parts = [("dur", "鎖上の作業の所要時間", "#9aa0a6"), ("delay", "鎖上の遅らせ（わざと遅らせた分）", "#d93025"), ("rwait", "鎖上の資源待ち", "#fbbc04")]
labs = ["MIP（厳密解）", "新しい最終設定", "＋目標工期"]
fig, axs = plt.subplots(1, 2, figsize=(13, 4.4), gridspec_kw=dict(width_ratios=[1.3, 1]))
left = [0, 0, 0]
for col, lab, c in parts:
    v = [m.loc[k, col].mean(), a.loc[k, col].mean(), b.loc[k, col].mean()]
    axs[0].barh(labs, v, left=left, color=c, label=lab)
    for i, (l, vv) in enumerate(zip(left, v)):
        if vv > 8:
            axs[0].text(l + vv / 2, i, f"{vv:.0f}", ha="center", va="center", fontsize=9, color="white" if col == "delay" else "black")
    left = [l + vv for l, vv in zip(left, v)]
for i, l in enumerate(left):
    axs[0].text(l + 2, i, f"工期 {l:.0f}", va="center", fontsize=9)
axs[0].invert_yaxis(); axs[0].set_xlim(0, max(left) * 1.15)
axs[0].set_xlabel("コマ（中間部の点の平均）")
axs[0].set_title(f"中間部の工期の内訳：最後の作業から逆にたどった鎖（j30、{len(k)}組、同じコスト以下で比較）", fontsize=10)
axs[0].legend(fontsize=8, loc="upper center", bbox_to_anchor=(0.5, -0.14), ncol=3)
v_on = [m.loc[k, "delay"].mean(), a.loc[k, "delay"].mean(), b.loc[k, "delay"].mean()]
v_off = [m.loc[k, "delay_off"].mean(), a.loc[k, "delay_off"].mean(), b.loc[k, "delay_off"].mean()]
x = range(3)
axs[1].bar([i - .2 for i in x], v_on, .4, color="#d93025", label="鎖の上（工期が伸びる）")
axs[1].bar([i + .2 for i in x], v_off, .4, color="#1a73e8", label="鎖の外（余裕の中、工期は伸びない）")
for i in x:
    axs[1].text(i - .2, v_on[i] + 5, f"{v_on[i]:.0f}", ha="center", fontsize=8)
    axs[1].text(i + .2, v_off[i] + 5, f"{v_off[i]:.0f}", ha="center", fontsize=8)
axs[1].set_xticks(list(x)); axs[1].set_xticklabels(labs, fontsize=9)
axs[1].set_ylabel("遅らせの合計（コマ）"); axs[1].set_title("遅らせはどこに置かれているか", fontsize=10)
axs[1].legend(fontsize=8); axs[1].grid(axis="y", alpha=.3)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "M1_chain.png"), dpi=130); plt.close(fig)

J = json.load(open(os.path.join(BD, "analysis", "ext1014", "e2_eval_base_prtbw3_dl.json"), encoding="utf-8"))
K = os.path.join(A, "dl_j120.json")
K = json.load(open(K, encoding="utf-8")) if os.path.exists(K) else None
fig, axs = plt.subplots(1, 3, figsize=(13, 4))
for i, (mm, lab, col) in enumerate([("prtbw3", "新しい最終設定", "#e8710a"), ("dl", "＋目標工期", "#188038")]):
    v = J[mm]["mid"]
    axs[0].scatter([i] * len(v), v, color=col, s=45, zorder=3)
    axs[0].plot([i - .25, i + .25], [sum(v) / len(v)] * 2, color=col, lw=2.5)
    axs[0].text(i + .28, sum(v) / len(v), f"{sum(v) / len(v):.2f}%", va="center", fontsize=9)
    e = J[mm]["exact"]
    axs[1].scatter([i] * len(e), e, color=col, s=45, zorder=3)
    axs[1].plot([i - .25, i + .25], [sum(e) / len(e)] * 2, color=col, lw=2.5)
    axs[1].text(i + .28, sum(e) / len(e), f"{sum(e) / len(e):.1f}", va="center", fontsize=9)
for ax, t, yl in [(axs[0], "j30：前線中間部の工期差（点は各回、5回）", "%"), (axs[1], "j30：工期端が MIP と一致した条件（/240）", "条件数")]:
    ax.set_xticks([0, 1]); ax.set_xticklabels(["新しい最終設定", "＋目標工期"]); ax.set_xlim(-.5, 1.9)
    ax.set_title(t, fontsize=10); ax.set_ylabel(yl); ax.grid(axis="y", alpha=.3)
hv = [("j30（5回）", J.get("hv_dl_prtbw3"))]
if K:
    hv.append(("j120（20×3回）", K))
vals = [h[1]["mean"] for h in hv if h[1]]; errs = [h[1]["se2"] for h in hv if h[1]]
axs[2].bar(range(len(vals)), vals, yerr=errs, capsize=4, color="#188038")
for i, h in enumerate([h for h in hv if h[1]]):
    axs[2].text(i, vals[i] + errs[i] + 0.0003, f"{vals[i]:+.4f}\n{h[1]['better']}/{h[1]['better'] + h[1]['worse']}", ha="center", fontsize=8)
axs[2].axhline(0, color="k", lw=.8); axs[2].set_ylim(min(0, min(v - e for v, e in zip(vals, errs))) * 1.3, max(v + e for v, e in zip(vals, errs)) * 1.35)
axs[2].set_xticks(range(len(vals))); axs[2].set_xticklabels([h[0] for h in hv if h[1]])
axs[2].set_title("前線全体：HV の差（＋目標工期 − 新しい最終設定、±2SE）", fontsize=10); axs[2].grid(axis="y", alpha=.3)
fig.suptitle("目標工期の遺伝子：窓の右端を「目標工期から逆算した最遅開始」で打ち切る")
fig.tight_layout(); fig.savefig(os.path.join(OUT, "M2_deadline.png"), dpi=130); plt.close(fig)
print("saved")
