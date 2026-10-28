"""
convergence_curve.py -- 「探索で解の質を高めているか」を時間の流れで見る図

各条件で、4戦略の集団を合わせた最小メイクスパンを評価回数ごとに追い、基準（j30 は MIP の最小工期、
j120 は RR000_RV0 の PSPLIB 最良既知値）との差（%）を条件・回で平均して描く。
  手法: base（現在）, pr（初期解だけ優先規則）, isrgp（探索を改善）, prisrgp（両方）
出力: figures/Search1014/fig6_convergence_<size>.png, analysis/ext1014/convergence_<size>.csv
使い方: python convergence_curve.py j30|j120
"""
import os, sys, glob, csv, statistics as S
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
size = sys.argv[1] if len(sys.argv) > 1 else "j30"
E = os.path.join(BD, "results", "EXT_COST")
METH = [("base", "現在の手法", "#80868b"), ("pr", "初期解だけ優先規則", "#e8710a"),
        ("isrgp", "探索を改善（工期島＋部分再構築）", "#188038"), ("prisrgp", "両方", "#1a73e8")]
GRID = np.array(sorted(set([100 * k for k in range(1, 51)] + [1000 * k for k in range(5, 51)])))

ref = {}
if size == "j30":
    for r in csv.DictReader(open(os.path.join(BD, "analysis", "ext1014", "repeats_j30_rows.csv"))):
        if r["mip_min_ms"]:
            ref[(r["instance"], r["cond"])] = float(r["mip_min_ms"])
    reps = 5; insts = [f"j30{x}_1" for x in range(1, 49)]
else:
    bks = {l.split()[0]: int(l.split()[1]) for l in open(os.path.join(HERE, "psplib_j120hrs.txt"), encoding="utf-8")
           if l.strip() and not l.startswith("#")}
    reps = 3; insts = [f"j120{x}_1" for x in range(1, 21)]
    for p in insts:
        ref[(p, "RR000_RV0")] = bks[p]


def curve(p, c, tag):
    """4戦略の trace から、評価回数ごとの集団最小メイクスパン（戦略をまたいだ最小）"""
    fs = glob.glob(os.path.join(E, "TRACE", p, f"TRACE_{p}_{c}_S*_{tag}.csv"))
    if len(fs) != 4:
        return None
    out = np.full(len(GRID), np.inf)
    for f in fs:
        ev, ms = [], []
        for ln in open(f):
            q = ln.split(",")
            if q[0] == "gen" and len(q) in (9, 10):  # 10列目 avg_window は 2026-10-24 から
                ev.append(float(q[2])); ms.append(float(q[3]))
        if not ev:
            return None
        ev = np.array(ev); ms = np.minimum.accumulate(np.array(ms))
        idx = np.searchsorted(ev, GRID, side="right") - 1
        v = np.where(idx >= 0, ms[np.clip(idx, 0, None)], np.inf)
        out = np.minimum(out, v)
    return out


res = {}
for m, _, _ in METH:
    cs = []
    for (p, c), rv in ref.items():
        for r in range(1, reps + 1):
            cv = curve(p, c, f"t1016_{m}_r{r}")
            if cv is not None and np.all(np.isfinite(cv[-5:])):
                cs.append((cv - rv) / rv * 100)
    if cs:
        res[m] = np.array(cs)
fig, ax = plt.subplots(figsize=(8, 4.2))
rows = []
for m, lab, col in METH:
    if m not in res:
        continue
    a = res[m]; mean = np.nanmean(np.where(np.isfinite(a), a, np.nan), axis=0)
    ax.plot(GRID, mean, color=col, lw=2, label=f"{lab}（{len(a)} 実行の平均、最後 {mean[-1]:.2f}%）")
    for g, v in zip(GRID, mean):
        rows.append(dict(method=m, evals=int(g), mean_gap_pct=v))
ax.set_xscale("log"); ax.set_xlabel("評価回数（1戦略あたり、対数目盛）")
ax.set_ylabel("最小メイクスパンの差（%）")
ttl = "MIP の最小工期との差（j30・MIP がある240条件）" if size == "j30" else "PSPLIB 最良既知値との差（j120・容量一定の20条件）"
ax.set_title(f"探索の進み方：{ttl}", fontsize=10)
ax.axvline(100, color="#bdc1c6", ls=":", lw=1); ax.text(105, ax.get_ylim()[1] * .95, "初期集団", fontsize=8, color="#5f6368")
ax.legend(fontsize=8); ax.grid(alpha=.3, which="both"); fig.tight_layout()
os.makedirs(os.path.join(BD, "figures", "Search1014"), exist_ok=True)
fig.savefig(os.path.join(BD, "figures", "Search1014", f"fig6_convergence_{size}.png"), dpi=130)
with open(os.path.join(BD, "analysis", "ext1014", f"convergence_{size}.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.DictWriter(fp, fieldnames=["method", "evals", "mean_gap_pct"]); w.writeheader(); w.writerows(rows)
for m in res:
    a = res[m]; mean = np.nanmean(np.where(np.isfinite(a), a, np.nan), axis=0)
    print(f"{m:8s} runs {len(a)}  初期 {mean[0]:.2f}%  5000評価 {mean[np.searchsorted(GRID, 5000)]:.2f}%  最終 {mean[-1]:.2f}%")
