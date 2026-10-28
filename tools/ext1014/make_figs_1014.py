"""
make_figs_1014.py -- 10/14 ゼミ指摘への対応結果の図（ゼミ資料用）
  fig1_end_exact.png      : j30 工期最小端が MIP と一致した条件数・10%以上ずれた条件数（手法別）
  fig2_search_contrib.png : 初期集団が MIP の最小工期に届かなかった条件で、探索がどれだけ詰めたか
  fig3_hv_diff.png        : 共通 HV の差（基準との差、平均±2SE、ノイズ基準つき）j30 と j120
  fig4_j120_bks.png       : j120 RR000_RV0（標準 RCPSP）の工期最小端と PSPLIB 最良既知値の差
出力: figures/Search1014/
前提: eval_variants.py（is, isrg, isrgp 付き）と eval_j120.py（isrgp 付き）を先に実行しておく。
"""
import os, sys, csv, glob, statistics as S
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
OUT = os.path.join(BD, "figures", "Search1014"); os.makedirs(OUT, exist_ok=True)
A = os.path.join(BD, "analysis", "ext1014")
rows = list(csv.DictReader(open(os.path.join(A, "variants_rows.csv"))))
wm = [r for r in rows if r["mip_min_ms"]]
LAB = {"b2": "現在の手法", "pr": "初期解：優先規則", "rg": "探索：部分再構築", "prrg": "優先規則\n＋部分再構築",
       "is": "探索：工期島", "isrg": "工期島\n＋部分再構築", "isrgp": "工期島(停滞で停止)\n＋部分再構築"}
ORDER = [t for t in ["b2", "pr", "rg", "prrg", "is", "isrg", "isrgp"] if f"{t}_hv" in rows[0]]
COL = {"b2": "#80868b", "pr": "#e8710a", "rg": "#1a73e8", "prrg": "#f9ab00", "is": "#a142f4", "isrg": "#d93025", "isrgp": "#188038"}

# fig1
fig, ax = plt.subplots(figsize=(8.5, 3.8))
base3 = [sum(1 for r in wm if float(r[f"{t}_gap_end"]) <= 1e-9) for t in ("base", "rp1", "b2")]
x = np.arange(len(ORDER))
ex = [sum(1 for r in wm if float(r[f"{t}_gap_end"]) <= 1e-9) for t in ORDER]
bad = [sum(1 for r in wm if float(r[f"{t}_gap_end"]) > 0.10) for t in ORDER]
ax.bar(x, ex, color=[COL[t] for t in ORDER])
for i, (e, b) in enumerate(zip(ex, bad)):
    ax.text(i, e + 2, f"{e}\n(10%超 {b})", ha="center", fontsize=8)
ax.axhspan(min(base3), max(base3), color="#80868b", alpha=.15, label=f"現在の手法を3回走らせた幅（{min(base3)}〜{max(base3)}）")
ax.set_xticks(x); ax.set_xticklabels([LAB[t] for t in ORDER], fontsize=8)
ax.set_ylim(150, len(wm) + 15); ax.set_ylabel(f"MIP と一致した条件数（/{len(wm)}）")
ax.set_title("j30：工期最小端が MIP の最小工期と一致した条件数（初期解はすべてランダム、優先規則のみ例外）")
ax.legend(fontsize=8, loc="upper left"); ax.grid(axis="y", alpha=.3); fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig1_end_exact.png"), dpi=130); plt.close(fig)

# fig2（search_contribution.py と同じ計算）
TAGOF = {"b2": "t1014base", "pr": "t1014pr", "rg": "t1014rg", "prrg": "t1014prrg", "is": "t1014is", "isrg": "t1014isrg", "isrgp": "t1014isrgp"}
reach, closed, nmiss = [], [], []
for t in ORDER:
    n = rc = 0; cl = []
    for r in wm:
        p, c = r["instance"], r["cond"]; mm = float(r["mip_min_ms"]); init = []
        for f in glob.glob(os.path.join(BD, "results", "EXT_COST", "TRACE", p, f"TRACE_{p}_{c}_S*_{TAGOF[t]}.csv")):
            for ln in open(f):
                q = ln.split(",")
                if q[0] == "init" and float(q[3]) < 1e8:
                    init.append(float(q[3]))
        if not init or min(init) <= mm:
            continue
        fin = float(r[f"{t}_min_ms"]); n += 1; rc += fin <= mm; cl.append((min(init) - fin) / (min(init) - mm))
    reach.append(rc / n); closed.append(S.mean(cl)); nmiss.append(n)
fig, ax = plt.subplots(figsize=(8.5, 3.8))
w = .38
ax.bar(x - w / 2, [v * 100 for v in reach], w, color=[COL[t] for t in ORDER], label="最終的に MIP の最小工期に到達した割合")
ax.bar(x + w / 2, [v * 100 for v in closed], w, color=[COL[t] for t in ORDER], alpha=.45, label="初期集団との差のうち探索で詰めた割合（平均）")
for i in range(len(ORDER)):
    ax.text(i - w / 2, reach[i] * 100 + 2, f"{reach[i]:.0%}", ha="center", fontsize=8)
    ax.text(i + w / 2, closed[i] * 100 + 2, f"{closed[i]:.0%}", ha="center", fontsize=8)
ax.set_xticks(x); ax.set_xticklabels([f"{LAB[t]}\n(n={n})" for t, n in zip(ORDER, nmiss)], fontsize=7.5)
ax.set_ylim(0, 110); ax.set_ylabel("%")
ax.set_title("j30：初期集団では MIP の最小工期に届かなかった条件で、探索がどれだけ工期を詰めたか")
ax.legend(fontsize=8, loc="upper left"); ax.grid(axis="y", alpha=.3); fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig2_search_contrib.png"), dpi=130); plt.close(fig)

# fig3
r120 = list(csv.DictReader(open(os.path.join(A, "j120_rows.csv"))))
fig, axs = plt.subplots(1, 2, figsize=(10, 3.8))
for ax, data, refk, tags, title in [
        (axs[0], rows, ("base", "rp1"), ["b2"] + [t for t in ORDER if t != "b2"], "j30（48インスタンス・276条件）"),
        (axs[1], r120, ("b2", "b3"), ["b3"] + [t for t in ["rg", "isrg", "isrgp"] if f"{t}_hv" in r120[0]], "j120（10インスタンス・60条件）")]:
    m, e, lab, col = [], [], [], []
    for t in tags:
        if t == tags[0]:      # ノイズ: 同じ設定どうしの差
            d = [float(r[f"{t}_hv"]) - float(r[f"{refk[0]}_hv"]) for r in data] if t == "b3" else \
                [float(r["b2_hv"]) - (float(r["base_hv"]) + float(r["rp1_hv"])) / 2 for r in data]
            lab.append("ノイズ（同じ設定の再実行）"); col.append("#bdc1c6")
        else:
            d = [float(r[f"{t}_hv"]) - (float(r[f"{refk[0]}_hv"]) + float(r[f"{refk[1]}_hv"])) / 2 for r in data]
            lab.append(LAB[t]); col.append(COL[t])
        m.append(S.mean(d)); e.append(2 * S.stdev(d) / len(d) ** .5)
    y = np.arange(len(tags))
    ax.barh(y, m, xerr=e, color=col, capsize=3); ax.axvline(0, color="k", lw=.8)
    ax.set_yticks(y); ax.set_yticklabels([l.replace("\n", "") for l in lab], fontsize=8); ax.invert_yaxis()
    ax.set_xlabel("共通 HV の差（現在の手法との差、平均±2SE）"); ax.set_title(title, fontsize=10); ax.grid(axis="x", alpha=.3)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "fig3_hv_diff.png"), dpi=130); plt.close(fig)

# fig4
bk = [r for r in r120 if r["bks"]]
tags = ["b2", "b3"] + [t for t in ["rg", "isrg", "isrgp"] if f"{t}_gap_bks" in r120[0]]
fig, ax = plt.subplots(figsize=(8.5, 3.8))
inst = [r["instance"] for r in bk]; xx = np.arange(len(bk)); w = .8 / len(tags)
for i, t in enumerate(tags):
    ax.bar(xx + (i - (len(tags) - 1) / 2) * w, [float(r[f"{t}_gap_bks"]) * 100 for r in bk], w,
           color=COL.get(t, "#bdc1c6"), label=(LAB.get(t, "").replace(chr(10), "") if t != "b3" else "現在の手法（2回目）")
           + f"  平均 {S.mean(float(r[f'{t}_gap_bks']) for r in bk):.1%}")
ax.set_xticks(xx); ax.set_xticklabels(inst, fontsize=8)
ax.set_ylabel("最小メイクスパンと最良既知値の差（%）")
ax.set_title("j120 RR000_RV0（容量一定＝標準 RCPSP）：PSPLIB の最良既知メイクスパンとの差")
ax.legend(fontsize=8); ax.grid(axis="y", alpha=.3); fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig4_j120_bks.png"), dpi=130); plt.close(fig)
print("saved to", OUT)

# fig5: j120 の前線の例（現在の手法 2 回 vs 改善版）
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..")))
import gap48_analysis as G  # noqa: E402
os.chdir(BD)
for p, c in [("j1201_1", "RR000_RV0"), ("j1206_1", "RR050_RV0"), ("j1208_1", "RR025_RV1")]:
    fig, ax = plt.subplots(figsize=(7.5, 4.5))
    for suf, col, lab in [("_t1014base", "#80868b", "現在の手法"), ("_t1014b3", "#bdc1c6", "現在の手法（2回目）"),
                          ("_t1014isrgp", "#188038", "工期島(停滞で停止)＋部分再構築")]:
        path = os.path.join("results", "EXT_COST", "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur{suf}.txt")
        if not os.path.exists(path):
            continue
        pts, _ = G.load_valid_points(path, os.path.join("j120.sm", p + ".sm"))
        f = G.pareto_front(pts)
        ax.plot(*zip(*f), ".-", color=col, lw=1.5, ms=4, label=f"{lab}（最小工期 {f[0][0]:.0f}）")
    bks = {r["instance"]: r["bks"] for r in r120 if r["bks"]}.get(p)
    if c == "RR000_RV0" and bks:
        ax.axvline(float(bks), color="k", ls="--", lw=1, label=f"PSPLIB 最良既知 {bks}")
    ax.set_xlabel("メイクスパン"); ax.set_ylabel("総コスト"); ax.set_title(f"{p} {c}（j120）：前線の比較")
    ax.legend(fontsize=8); ax.grid(alpha=.3); fig.tight_layout()
    fig.savefig(os.path.join(OUT, f"fig5_front_{p}_{c}.png"), dpi=130); plt.close(fig)
print("fig5 done")
