"""
init_alpha_sweep.py -- 10/14 指摘「初期解の分布をグラフで見る」「優先度にランダム性を持たせて
バリエーションが無くならないようにする」の確認。

作業リストの作り方ごとに N 本作り、窓幅0（最早配置）で復号したメイクスパン（＝作業リストの実力）の分布と、
作業リスト同士の違い（多様性）を測る。
  random : 先行関係を守る一様ランダム（現在の NSGA の初期解と同じ）
  PR a=1, 3, 10 : 優先規則＋ランダム性（重み (優先度差+1)^a、4規則から一様に選ぶ）。C++ の
                  RCPSP_Problem_MaxShift::sampleBiasedPriorityList と同じ手順
  PR det : 優先規則そのまま（同点のみ乱数）
多様性 = 2本の作業リストの位置の差の平均（Spearman footrule を最大値で割った値, 0=同一, 1=逆順）

出力: analysis/ext1014/init_alpha_sweep.csv, figures/Init_EXT/init_alpha_<inst>_<cond>.png
使い方: python init_alpha_sweep.py [N=1000]
"""
import os, sys, random, csv
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "ext1001"))
from msd_decoder import Case, BD  # noqa: E402
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]

N = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
CASES = [("j309_1", "RR050_RV0"), ("j309_1", "RR025_RV0"), ("j3011_1", "RR050_RV0"), ("j3014_1", "RR050_RV0"),
         ("j3026_1", "RR050_RV1"), ("j3037_1", "RR025_RV0")]
OUTF = os.path.join(BD, "figures", "Init_EXT"); os.makedirs(OUTF, exist_ok=True)
OUTA = os.path.join(BD, "analysis", "ext1014"); os.makedirs(OUTA, exist_ok=True)
rng = random.Random(20261014)


def prio_tables(cs):
    n = cs.n; dur = cs.dur
    ES = [0] * n; EF = [0] * n
    for j in range(n):                      # PSPLIB は番号順がトポロジカル順
        ES[j] = max((EF[p] for p in cs.preds[j]), default=0); EF[j] = ES[j] + dur[j]
    CP = max(EF); LF = [CP] * n
    for j in reversed(range(n)):
        LF[j] = min([CP] + [LF[s] - dur[s] for s in cs.succ[j]])
    mts = []
    for j in range(n):
        seen = set(); st = [j]
        while st:
            u = st.pop()
            for s in cs.succ[u]:
                if s not in seen:
                    seen.add(s); st.append(s)
        mts.append(len(seen))
    grpw = [dur[j] + sum(dur[s] for s in cs.succ[j]) for j in range(n)]
    return [[-v for v in LF], [-(LF[j] - dur[j]) for j in range(n)], mts, grpw]


def sample(cs, tabs, alpha):
    pr = tabs[rng.randrange(4)] if alpha is not None else None
    if alpha == "det":
        pr = tabs[rng.randrange(4)]
    indeg = [len(p) for p in cs.preds]; ready = [j for j in range(cs.n) if indeg[j] == 0]; order = []
    while ready:
        if alpha is None:
            j = rng.choice(ready)
        elif alpha == "det":
            best = max(pr[k] for k in ready); j = rng.choice([k for k in ready if pr[k] == best])
        else:
            lo = min(pr[k] for k in ready)
            j = rng.choices(ready, weights=[(pr[k] - lo + 1) ** alpha for k in ready])[0]
        ready.remove(j); order.append(j)
        for k in cs.succ[j]:
            indeg[k] -= 1
            if indeg[k] == 0:
                ready.append(k)
    return order


def footrule(a, b):
    n = len(a); pa = np.empty(n, int); pb = np.empty(n, int)
    pa[a] = np.arange(n); pb[b] = np.arange(n)
    return np.abs(pa - pb).sum() / (n * n // 2)


METHODS = [("ランダム（現在）", None), ("優先規則 a=1", 1.0), ("優先規則 a=3", 3.0),
           ("優先規則 a=10", 10.0), ("優先規則そのまま", "det")]
rows = []
for prefix, cond in CASES:
    cs = Case(os.path.join(BD, "results", "EXT_COST", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt"),
              os.path.join(BD, "j30.sm", prefix + ".sm"), os.path.join(BD, "costs_ext", f"costs_{prefix}.csv"))
    mip_min = min(s[0] for s in cs.sols)
    tabs = prio_tables(cs); Z = np.zeros(cs.n, dtype=int)
    res = {}
    for name, a in METHODS:
        lists = [sample(cs, tabs, a) for _ in range(N)]
        ms = np.array([cs.decode(o, Z)[1] for o in lists])
        idx = [rng.sample(range(N), 2) for _ in range(500)]
        div = float(np.mean([footrule(lists[i], lists[k]) for i, k in idx]))
        uniq = len({tuple(o) for o in lists})
        res[name] = ms
        rows.append(dict(instance=prefix, cond=cond, method=name, mip_min_ms=mip_min, best=int(ms.min()),
                         p10=float(np.percentile(ms, 10)), median=float(np.median(ms)),
                         frac_within5=float(np.mean(ms <= 1.05 * mip_min)), diversity=div, unique=uniq))
        print(f"{prefix} {cond} {name:10s} best {ms.min():4.0f} median {np.median(ms):6.1f} "
              f"within5% {np.mean(ms <= 1.05*mip_min):.3f} div {div:.3f} unique {uniq}/{N}", flush=True)
    fig, ax = plt.subplots(figsize=(7.5, 4.2))
    bins = np.arange(min(min(v) for v in res.values()) - 1, max(max(v) for v in res.values()) + 2, 2)
    for name, _ in METHODS:
        ax.hist(res[name], bins=bins, histtype="step", lw=1.6, label=name, density=True)
    ax.axvline(mip_min, color="k", ls="--", lw=1.2, label=f"MIP の最小工期 {mip_min:.0f}")
    ax.set_xlabel("作業リストの工期（窓幅0で復号したメイクスパン）"); ax.set_ylabel("割合")
    ax.set_title(f"{prefix} {cond}：作業リストの作り方ごとの初期解の分布（各 {N} 本）")
    ax.legend(fontsize=8); ax.grid(alpha=.3); fig.tight_layout()
    fig.savefig(os.path.join(OUTF, f"init_alpha_{prefix}_{cond}.png"), dpi=130); plt.close(fig)

with open(os.path.join(OUTA, "init_alpha_sweep.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
