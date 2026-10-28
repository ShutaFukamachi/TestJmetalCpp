"""
figs_onepop.py -- 最終設定どうしの比較（1集団 fin1・fin1c vs 2集団 prbwisrgp）と、計算時間の図
出力: figures/Seminar1028/D3_onepop_vs_twopop.png, D4_timing.png
"""
import os, json, csv, statistics as S
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
A = os.path.join(BD, "analysis", "ext1014"); OUT = os.path.join(BD, "figures", "Seminar1028")
J = json.load(open(os.path.join(A, "e2_eval_isrgp_fin1c_fin1.json"), encoding="utf-8"))
K = {"isrgp": "2集団（工期島）\n最終設定", "fin1": "1集団：工期側の子を窓幅0、\n改善のみ受入（止めない）", "fin1c": "1集団：同左＋\n工期島と同じ止め方"}
C = {"isrgp": "#d93025", "fin1": "#188038", "fin1c": "#81c995"}
ORD = ["isrgp", "fin1", "fin1c"]
fig, axs = plt.subplots(1, 3, figsize=(14, 4.2))
for ax, key, yl, ttl in [(axs[0], "exact", "工期端が MIP と一致（/240）", "工期最小端"),
                         (axs[1], "mid", "中間部の工期差（%）", "前線の中間部（小さいほど良い）")]:
    for i, m in enumerate(ORD):
        v = J[m][key]
        ax.scatter([i] * len(v), v, color=C[m], s=40, zorder=3)
        ax.plot([i - .25, i + .25], [S.mean(v)] * 2, color=C[m], lw=2.5)
        ax.text(i + .28, S.mean(v), f"{S.mean(v):.2f}" if key == "mid" else f"{S.mean(v):.1f}", va="center", fontsize=9)
    ax.set_xticks(range(3)); ax.set_xticklabels([K[m] for m in ORD], fontsize=8); ax.set_xlim(-.5, 2.8)
    ax.set_ylabel(yl); ax.set_title(f"j30：{ttl}（点は各回、5回）", fontsize=10); ax.grid(axis="y", alpha=.3)
hv = [J["hv"]["fin1-isrgp"], J["hv"]["fin1c-isrgp"]]
j120 = json.load(open(os.path.join(A, "size_j120_prbwisrgp_fin1_base.json"), encoding="utf-8"))
axs[2].bar([0, 1], [h["mean"] for h in hv], yerr=[h["se2"] for h in hv], color=[C["fin1"], C["fin1c"]], capsize=4, label="j30")
axs[2].bar([2], [j120["fin1"]["hv_diff"]], yerr=[j120["fin1"]["hv_2se"]], color=C["fin1"], hatch="//", capsize=4, label="j120（20インスタンス×3回）")
for i, (v, b, w) in enumerate([(hv[0]["mean"], hv[0]["better"], hv[0]["worse"]), (hv[1]["mean"], hv[1]["better"], hv[1]["worse"]),
                               (j120["fin1"]["hv_diff"], j120["fin1"]["better"], j120["fin1"]["worse"])]):
    axs[2].text(i, (v if v > 0 else 0) + .0006, f"{v:+.4f}\n{b}/{w}", ha="center", fontsize=8)
axs[2].axhline(0, color="k", lw=.8)
axs[2].set_xticks([0, 1, 2]); axs[2].set_xticklabels(["止めない\n(j30)", "工期島と同じ\n止め方 (j30)", "止めない\n(j120)"], fontsize=8)
axs[2].set_ylabel("共通 HV の差（1集団 − 2集団、±2SE）"); axs[2].set_title("前線全体（改善/悪化の条件数）", fontsize=10)
axs[2].legend(fontsize=7); axs[2].grid(axis="y", alpha=.3)
fig.suptitle("1集団で工期島と同じことができるか（最終設定どうし：優先規則初期解＋部分再構築＋ブロック窓は共通）")
fig.tight_layout(); fig.savefig(os.path.join(OUT, "D3_onepop_vs_twopop.png"), dpi=130); plt.close(fig)

# 計算時間
TF = "timing_1024.csv" if os.path.exists(os.path.join(A, "timing_1024.csv")) else "timing_1023.csv"  # 1024 は手法を交互に測り直した値
rows = list(csv.DictReader(open(os.path.join(A, TF), encoding="utf-8")))
inst = []
for r in rows:
    if r["instance"] not in inst:
        inst.append(r["instance"])
meth = [(m, l, c) for m, l, c in [("base", "現在の手法", "#80868b"), ("isrgp", "工期島＋部分再構築", "#188038"), ("prbwisrgp", "10/21 の最終設定（2集団）", "#d93025")]
        if any(r["method"] == m for r in rows)]
if any(r["method"] == "prtbw3" for r in rows):
    meth.append(("prtbw3", "新しい最終設定（時間ブロック）", "#e8710a"))
fig, ax = plt.subplots(figsize=(8, 3.8))
w = .8 / len(meth)
for i, (m, lab, col) in enumerate(meth):
    v = [float(next(r["seconds"] for r in rows if r["instance"] == p and r["method"] == m)) for p in inst]
    ax.bar(np.arange(len(inst)) + (i - (len(meth) - 1) / 2) * w, v, w, color=col, label=lab)
    for k, vv in enumerate(v):
        ax.text(k + (i - (len(meth) - 1) / 2) * w, vv + 3, f"{vv:.0f}", ha="center", fontsize=7)
ax.set_xticks(range(len(inst))); ax.set_xticklabels([f"{p}\n（{'作業数' + p[1:p.index('_')][:-1 if len(p[1:p.index('_')]) > 2 else None]}）" if False else p for p in inst], fontsize=8)
ax.set_ylabel("秒（6条件×4戦略×各5万評価）")
ax.set_title("計算時間（他の計算を止め、手法を交互に1インスタンスずつ測定）" if TF == "timing_1024.csv" else "計算時間（他の計算を止めて1インスタンスずつ測定）：評価回数が同じなので手法による差は小さい", fontsize=10)
ax.legend(fontsize=8); ax.grid(axis="y", alpha=.3); fig.tight_layout()
fig.savefig(os.path.join(OUT, "D4_timing.png"), dpi=130)
print("saved")
