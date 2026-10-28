"""
eval_j120.py -- 10/14 指摘「作業数120になったらどうする？」への確認（MIP なし）

比べるもの（j120 の最初の10インスタンス × 6条件、拡張コスト表、MaxShiftDur P1）:
  b2, b3 : 現在の手法を2回（その差がノイズ）
  rg     : 部分再構築変異
  isrg   : 工期島＋部分再構築変異
基準:
  ・RR000_RV0 は容量一定＝標準の RCPSP なので、PSPLIB の最良既知メイクスパン（j120hrs）と比べられる
  ・全条件: 全手法の中の最小メイクスパンとの差、全手法を合わせて正規化した共通 HV
出力: analysis/ext1014/j120_rows.csv と標準出力の要約
"""
import os, sys, csv, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402

E = os.path.join("results", "EXT_COST")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
TAGS = {"b2": "_t1014base", "b3": "_t1014b3", "rg": "_t1014rg", "isrg": "_t1014isrg"}
for a in sys.argv[1:]:
    k, v = a.split("="); TAGS[k] = v
BKS = {}
for ln in open(os.path.join(HERE, "psplib_j120hrs.txt"), encoding="utf-8"):
    if ln.strip() and not ln.startswith("#"):
        k, v = ln.split(); BKS[k] = int(v)

rows = []
for x in range(1, 61):
    p = f"j120{x}_1"; sm = os.path.join("j120.sm", p + ".sm")
    for c in CONDS:
        F = {}
        for t, s in TAGS.items():
            path = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur{s}.txt")
            if os.path.exists(path):
                pts, _ = G.load_valid_points(path, sm)
                if pts:
                    F[t] = G.pareto_front(pts)
        if len(F) != len(TAGS):
            continue
        pts = [q for f in F.values() for q in f]
        b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
        best = b[0]
        row = dict(instance=p, cond=c, best_all=best, bks=BKS.get(p) if c == "RR000_RV0" else None)
        for t, f in F.items():
            row[f"{t}_min_ms"] = f[0][0]
            row[f"{t}_min_cost"] = min(q[1] for q in f)
            row[f"{t}_gap_best"] = (f[0][0] - best) / best
            row[f"{t}_gap_bks"] = (f[0][0] - row["bks"]) / row["bks"] if row["bks"] else None
            row[f"{t}_hv"] = G.hypervolume_2d(G.normalize_front(f, *b), (1.1, 1.1))
        rows.append(row)
os.makedirs(os.path.join(BD, "analysis", "ext1014"), exist_ok=True)
with open(os.path.join(BD, "analysis", "ext1014", "j120_rows.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)

print(f"j120 conditions: {len(rows)} ({len({r['instance'] for r in rows})} instances)")
bk = [r for r in rows if r["bks"]]
for t in TAGS:
    g = [r[f"{t}_gap_bks"] for r in bk]
    gb = [r[f"{t}_gap_best"] for r in rows]
    print(f"[{t:5s}] RR000_RV0 vs PSPLIB best known: mean {S.mean(g):.1%} (min {min(g):.1%}, max {max(g):.1%})"
          f" | all conds vs best of all methods: mean {S.mean(gb):.1%}, is best in {sum(1 for x in gb if x <= 1e-9)}")
ref = lambda r: (r["b2_hv"] + r["b3_hv"]) / 2  # noqa: E731
print("--- HV difference from mean(b2, b3); noise = b2 - b3 ---")
d = [r["b2_hv"] - r["b3_hv"] for r in rows]
print(f"[noise] {S.mean(d):+.4f} (SE {S.stdev(d)/len(d)**0.5:.4f})")
for t in TAGS:
    if t in ("b2", "b3"):
        continue
    d = [r[f"{t}_hv"] - ref(r) for r in rows]
    print(f"[{t:5s}] {S.mean(d):+.4f} (SE {S.stdev(d)/len(d)**0.5:.4f})  better {sum(1 for x in d if x > 0)}  worse {sum(1 for x in d if x < 0)}")
    mc = [(r[f"{t}_min_cost"] - (r["b2_min_cost"] + r["b3_min_cost"]) / 2) / r["b2_min_cost"] for r in rows]
    print(f"        min-cost end change: {S.mean(mc):+.2%}")
