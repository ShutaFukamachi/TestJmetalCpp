"""
init_rule_compare.py -- 10/14 指摘「優先ルールで作業リストを組む（最遅完了時刻が遅いものから優先など）」の確認

作業リストの作り方ごとに、MIP がある j30 の全条件で N 本ずつ作り、窓幅0で復号した工期を MIP の最小工期と比べる。
  random      : 先行関係を守る一様ランダム（現在の初期解）
  LFT早い順   : 最遅完了時刻が早いものから（標準の LFT 規則）、ランダム性 a=3
  LFT遅い順   : 最遅完了時刻が遅いものから（先生の例）、ランダム性 a=3
  4規則混合   : LFT/LST/MTS/GRPW から毎回1つ（今回採用した作り方）、a=3
指標: 工期 / MIP 最小工期 の中央値、N 本中の最良が MIP と一致した条件の割合、多様性（位置の差の平均）
出力: analysis/ext1014/init_rule_compare.csv, figures/Init_EXT/init_rule_compare.png
使い方: python init_rule_compare.py [N=100]
"""
import os, sys, random, csv, statistics as S
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "ext1001")); sys.path.insert(0, HERE)
from msd_decoder import Case, BD  # noqa: E402
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]

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


def footrule(a, b):
    n = len(a); pa = np.empty(n, int); pb = np.empty(n, int)
    pa[a] = np.arange(n); pb[b] = np.arange(n)
    return np.abs(pa - pb).sum() / (n * n // 2)



N = int(sys.argv[1]) if len(sys.argv) > 1 else 100
rng = random.Random(20261020)
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]


def sample(cs, pr, a):
    indeg = [len(p) for p in cs.preds]; ready = [j for j in range(cs.n) if indeg[j] == 0]; order = []
    while ready:
        if pr is None:
            j = rng.choice(ready)
        else:
            p = pr() if callable(pr) else pr
            lo = min(p[k] for k in ready)
            j = rng.choices(ready, weights=[(p[k] - lo + 1) ** a for k in ready])[0]
        ready.remove(j); order.append(j)
        for k in cs.succ[j]:
            indeg[k] -= 1
            if indeg[k] == 0:
                ready.append(k)
    return order


NAMES = ["ランダム（現在）", "LFT 早い順（標準）", "LFT 遅い順（先生の例）", "4規則混合（採用）"]
res = {k: {"ratio": [], "best": [], "div": []} for k in NAMES}
rows = []
for x in range(1, 49):
    p = f"j30{x}_1"
    for c in CONDS:
        mp = os.path.join(BD, "results", "EXT_COST", "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt")
        if not os.path.exists(mp):
            continue
        cs = Case(mp, os.path.join(BD, "j30.sm", p + ".sm"), os.path.join(BD, "costs_ext", f"costs_{p}.csv"))
        if not cs.sols:
            continue
        mm = min(s[0] for s in cs.sols)
        tabs = prio_tables(cs)               # [LFT(-LF), LST, MTS, GRPW]、大きいほど優先
        late = [-v for v in tabs[0]]         # LF が大きいほど優先
        gens = {NAMES[0]: (None, 0), NAMES[1]: (tabs[0], 3.0), NAMES[2]: (late, 3.0),
                NAMES[3]: (None, 3.0)}
        Z = np.zeros(cs.n, dtype=int)
        for name, (pr, a) in gens.items():
            if name == NAMES[3]:
                lists = []
                for _ in range(N):
                    t = tabs[rng.randrange(4)]
                    lists.append(sample(cs, t, a))
            else:
                lists = [sample(cs, pr, a) for _ in range(N)]
            ms = [r[1] for r in (cs.decode(o, Z) for o in lists) if r is not None]
            if not ms:
                continue
            ratio = [m / mm for m in ms]
            div = S.mean(footrule(lists[i], lists[k]) for i, k in (rng.sample(range(N), 2) for _ in range(50)))
            res[name]["ratio"] += ratio; res[name]["best"].append(min(ratio)); res[name]["div"].append(div)
            rows.append(dict(instance=p, cond=c, method=name, mip_min_ms=mm, median_ratio=S.median(ratio),
                             best_ratio=min(ratio), diversity=div))
    print(p, "done", flush=True)

with open(os.path.join(BD, "analysis", "ext1014", "init_rule_compare.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
for k in NAMES:
    r = res[k]
    print(f"{k:14s} 工期/MIP 中央値 {S.median(r['ratio']):.3f}  最良が MIP と一致 {np.mean(np.array(r['best']) <= 1 + 1e-9):.1%}"
          f"  最良/MIP 平均 {S.mean(r['best']):.3f}  多様性 {S.mean(r['div']):.3f}")

fig, axs = plt.subplots(1, 2, figsize=(11, 4))
col = ["#9aa0a6", "#1a73e8", "#d93025", "#e8710a"]
axs[0].boxplot([res[k]["ratio"] for k in NAMES], showfliers=False, patch_artist=True,
               boxprops=dict(facecolor="#f1f3f4"))
axs[0].set_xticks(range(1, 5)); axs[0].set_xticklabels([k.replace("（", "\n（") for k in NAMES], fontsize=8)
axs[0].axhline(1.0, color="k", ls="--", lw=1); axs[0].set_ylabel("作業リストの工期 ÷ MIP の最小工期")
axs[0].set_title("初期解1本1本の質（箱は25〜75%、線は中央値）")
xs = [S.mean(res[k]["div"]) for k in NAMES]; ys = [float(np.mean(np.array(res[k]["best"]) <= 1 + 1e-9)) * 100 for k in NAMES]
for k, x_, y_, c_ in zip(NAMES, xs, ys, col):
    axs[1].scatter(x_, y_, s=90, color=c_); axs[1].annotate(k, (x_, y_), textcoords="offset points", xytext=(6, 4), fontsize=8)
axs[1].set_xlabel("多様性（作業リスト同士の位置の差の平均、大きいほどバラバラ）")
axs[1].set_ylabel(f"{N} 本の最良が MIP の最小工期と一致した条件（%）")
axs[1].set_title("質と多様性のバランス"); axs[1].grid(alpha=.3)
fig.suptitle(f"j30・MIP がある全条件：作業リストの作り方の比較（各条件 {N} 本、窓幅0で復号）")
fig.tight_layout(); fig.savefig(os.path.join(BD, "figures", "Init_EXT", "init_rule_compare.png"), dpi=130)
