"""
diag_children.py -- 実験1：元の NSGA-II（base）と工期島（isrgp）で、工期側の親と子がどれだけ作られ、
集団の最良メイクスパンを更新したかを数える（TRACE の child 行。--trace-children で記録）。

child 行: child,gen,evals,parents,parents_near5pct,children,child_better,child_list_better,child_near5pct,migrant_better
  child_better      : 子の実際のメイクスパンが、その世代の集団の最良より短かった回数
  child_list_better : 子の作業リストを窓幅0で復号すると、集団の最良より短かった回数
  migrant_better    : 工期島からの移民が集団の最良より短かった回数
出力: analysis/ext1014/diag_children.csv、図 figures/Seminar1028/D1_children.png、標準出力
"""
import os, glob, csv, statistics as S
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
OUTF = os.path.join(BD, "figures", "Seminar1028"); os.makedirs(OUTF, exist_ok=True)

res = {}
strict = {}
for m in ["base", "isrgp", "base2"]:
    tag = {"base": "t1023d_base", "isrgp": "t1023d_isrgp", "base2": "t1023d2_base"}[m]
    tot = np.zeros(7); early = np.zeros(7); runs = 0; st = 0
    for f in glob.glob(os.path.join(BD, "results", "EXT_COST", "TRACE", "j30*", f"TRACE_*_{tag}.csv")):
        runs += 1
        for ln in open(f):
            q = ln.strip().split(",")
            if q[0] == "child" and len(q) in (10, 11):
                if len(q) == 11:
                    st += float(q[10])
                v = np.array([float(x) for x in q[3:10]])
                tot += v
                if int(q[1]) <= 50:
                    early += v
    if runs == 0:
        continue
    res[m] = dict(runs=runs, tot=tot, early=early); strict[m] = st
    P, PN, C, CB, CLB, CN, MB = tot
    print(f"== {m}: {runs} 実行（各4戦略のうち1戦略ぶんの trace ではなく全戦略の合計）")
    print(f"  親のうち工期最小から5%以内: {PN/P:.1%}（{PN:.0f}/{P:.0f}）")
    print(f"  子のうち工期最小から5%以内: {CN/C:.1%}")
    print(f"  子が集団の最良メイクスパンを更新: {CB:.0f} 回（子100万体あたり {CB/C*1e6:.1f}）")
    print(f"  子の作業リスト（窓幅0）なら更新していた: {CLB:.0f} 回（子100万体あたり {CLB/C*1e6:.1f}）")
    print(f"  工期島の移民が更新: {MB:.0f} 回")
    if st:
        print(f"  子の作業リストが「集団の最良工期」と「集団の作業リストの最良」の両方より短い: {st:.0f} 回（子100万体あたり {st/C*1e6:.1f}）")
with open(os.path.join(BD, "analysis", "ext1014", "diag_children.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.writer(fp)
    w.writerow(["method", "runs", "parents", "parents_near", "children", "child_better", "child_list_better", "child_near", "migrant_better"])
    for m, r in res.items():
        w.writerow([m, r["runs"]] + [int(x) for x in r["tot"]])

fig, axs = plt.subplots(1, 3, figsize=(13, 3.8))
lab = {"base": "現在の手法（1集団）", "isrgp": "工期島あり（本体の数）"}
col = {"base": "#80868b", "isrgp": "#188038"}
for i, m in enumerate(["base", "isrgp"]):
    P, PN, C, CB, CLB, CN, MB = res[m]["tot"]
    axs[0].bar(i, PN / P * 100, color=col[m]); axs[0].text(i, PN / P * 100 + .5, f"{PN/P:.1%}", ha="center")
    axs[1].bar([i - .2, i + .2], [CB / C * 1e6, CLB / C * 1e6], .38, color=[col[m], "#fbbc04"])
    axs[1].text(i - .2, CB / C * 1e6, f"{CB/C*1e6:.0f}", ha="center", va="bottom", fontsize=8)
    axs[1].text(i + .2, CLB / C * 1e6, f"{CLB/C*1e6:.0f}", ha="center", va="bottom", fontsize=8)
axs[0].set_xticks([0, 1]); axs[0].set_xticklabels([lab["base"], lab["isrgp"]], fontsize=8)
axs[0].set_ylabel("%"); axs[0].set_title("親のうち、工期最小から5%以内の個体の割合", fontsize=10)
axs[1].set_xticks([0, 1]); axs[1].set_xticklabels([lab["base"], lab["isrgp"]], fontsize=8)
axs[1].set_ylabel("子100万体あたりの回数"); axs[1].set_title("集団の最良工期を更新した子（灰/緑）と\n作業リストだけなら更新していた子（黄）", fontsize=10)
P, PN, C, CB, CLB, CN, MB = res["isrgp"]["tot"]
axs[2].bar([0, 1], [res["base"]["tot"][3], CB + MB], color=[col["base"], col["isrgp"]])
axs[2].bar([1], [MB], color="#a142f4", label="うち工期島からの移民")
axs[2].set_xticks([0, 1]); axs[2].set_xticklabels([lab["base"], "工期島あり（本体＋移民）"], fontsize=8)
axs[2].set_ylabel("回数（48インスタンス×6条件×4戦略の合計）"); axs[2].set_title("集団の最良工期が更新された回数", fontsize=10)
axs[2].legend(fontsize=8)
for a in axs:
    a.grid(axis="y", alpha=.3)
fig.suptitle("実験1：元の NSGA-II では工期側で何が起きているか（j30 全条件）")
fig.tight_layout(); fig.savefig(os.path.join(OUTF, "D1_children.png"), dpi=130)

# ---- 図 D1b: 元の NSGA-II で「生まれたのに見逃された」工期改善（厳密版）----
if "base2" in res:
    P, PN, C, CB, CLB, CN, MB = res["base2"]["tot"]
    fig, ax = plt.subplots(figsize=(7.5, 3.8))
    vals = [CB / C * 1e6, strict["base2"] / C * 1e6]
    ax.bar([0, 1], vals, color=["#80868b", "#fbbc04"])
    for i, v in enumerate(vals):
        ax.text(i, v, f"{v:.1f}", ha="center", va="bottom", fontsize=10)
    ax.set_xticks([0, 1])
    ax.set_xticklabels(["実際に集団の最良工期を\n更新した子", "作業リスト（窓幅0）なら\n集団の最良（工期・作業リストとも）を\n更新していた子"], fontsize=9)
    ax.set_ylabel("子100万体あたりの数")
    ax.set_title(f"元の NSGA-II（j30 全条件）：良い作業リストを持つ子は {vals[1]/vals[0]:.0f} 倍生まれているが、\n受け継いだ窓幅で工期が伸びて見逃されている", fontsize=10)
    ax.grid(axis="y", alpha=.3); fig.tight_layout()
    fig.savefig(os.path.join(OUTF, "D1b_missed_lists.png"), dpi=130)
    print("D1b saved")
