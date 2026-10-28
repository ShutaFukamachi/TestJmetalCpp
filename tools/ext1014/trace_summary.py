"""
trace_summary.py -- NSGA の世代ごとの記録（TRACE_*.csv）から、工期側の探索の様子をまとめる。

各 (instance, cond, strategy) について:
  init_best      : 初期集団の最小メイクスパン
  final_best     : 最終世代の最小メイクスパン
  last_improve   : 最小メイクスパンが最後に改善した評価回数（50,000 評価中）
  list_gap       : 窓幅0で復号したときの作業リストの最小工期と、実際の最小工期の差（毎記録の平均）
                   → 0 なら「窓幅が良い作業リストを隠している」ことはない（仮説 H2 の検証）
  near_mean      : 工期最小から5%以内の個体数の平均（100体中）
  near_lists     : その中の異なる作業リストの数の平均
使い方: python trace_summary.py <tag> [size=j30]
出力: analysis/ext1014/trace_<tag>.csv と要約
"""
import os, sys, glob, csv, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
tag = sys.argv[1]; size = sys.argv[2] if len(sys.argv) > 2 else "j30"
rows = []
for f in sorted(glob.glob(os.path.join(BD, "results", "EXT_COST", "TRACE", f"{size}*", f"TRACE_*_{tag}.csv"))):
    name = os.path.basename(f)[6:-4 - len(tag) - 1]       # <prefix>_<cond>_S<k>
    prefix, cond, strat = name.rsplit("_", 3)[0], "_".join(name.rsplit("_", 3)[1:3]), name.rsplit("_", 1)[1]
    init, gens = [], []
    for ln in open(f):
        p = ln.strip().split(",")
        if p[0] == "init":
            init.append((p[2], float(p[3]), float(p[5])))
        elif p[0] == "gen" and len(p) in (9, 10):    # 実行中のファイルの途中行は除く
            gens.append(tuple(float(x) for x in p[1:]))
    if not gens:
        continue
    best = gens[0][2]; last = gens[0][1]
    for g in gens:
        if g[2] < best:
            best = g[2]; last = g[1]
    rows.append(dict(instance=prefix, cond=cond, strategy=strat,
                     init_best=min(x[1] for x in init), init_best_list=min(x[2] for x in init),
                     final_best=gens[-1][2], last_improve=last,
                     list_gap=S.mean(g[2] - g[3] for g in gens),
                     masked=S.mean(1.0 if g[2] > g[3] else 0.0 for g in gens),
                     window_helps=S.mean(1.0 if g[2] < g[3] else 0.0 for g in gens),
                     near_mean=S.mean(g[5] for g in gens[10:]), near_lists=S.mean(g[6] for g in gens[10:])))
out = os.path.join(BD, "analysis", "ext1014", f"trace_{tag}.csv")
with open(out, "w", newline="", encoding="utf-8") as fp:
    w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
li = [r["last_improve"] for r in rows]
print(f"{tag} {size}: runs {len(rows)}")
print(f"  last improvement of min makespan (evals): median {S.median(li):.0f}, "
      f"<=5000: {sum(1 for x in li if x <= 5000)/len(li):.0%}, <=10000: {sum(1 for x in li if x <= 10000)/len(li):.0%}")
print(f"  final == initial best: {sum(1 for r in rows if r['final_best'] >= r['init_best'])/len(rows):.0%}")
print(f"  list_gap (actual - list-only): mean {S.mean(r['list_gap'] for r in rows):+.3f};  share of records where "
      f"windows hide a shorter list: {S.mean(r['masked'] for r in rows):.1%}, windows make it shorter: "
      f"{S.mean(r['window_helps'] for r in rows):.1%}")
print(f"  individuals within 5% of min ms: mean {S.mean(r['near_mean'] for r in rows):.1f} / 100, "
      f"distinct lists among them: {S.mean(r['near_lists'] for r in rows):.1f}")
