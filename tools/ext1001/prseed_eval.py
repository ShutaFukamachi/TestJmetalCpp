"""
prseed_eval.py -- 指摘1対策の評価: 優先規則（LFT）で作った作業リストを初期集団に入れた MaxShiftDur（prseed）を、
公式設定の実行（base）とその再実行（rp1 = ノイズの基準）と比べる。

  ・工期最小端: 各手法の前線の最小メイクスパンと、MIP の最小メイクスパンの差（%）
  ・前線全体  : base / rp1 / prseed（と MIP）を合わせて正規化した共通 HV
出力: analysis/ext1001/prseed_eval.csv と標準出力の要約
"""
import os, sys, csv, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402

E = os.path.join("results", "EXT_COST")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
TAGS = {"base": "", "rp1": "_rp1", "prseed": "_prseed"}


def front(path, sm):
    pts, _ = G.load_valid_points(path, sm)
    return G.pareto_front(pts) if pts else []


rows = []
for x in range(1, 49):
    p = f"j30{x}_1"; sm = os.path.join("j30.sm", p + ".sm")
    for c in CONDS:
        F = {t: front(os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur{s}.txt"), sm) for t, s in TAGS.items()}
        if not all(F.values()):
            continue
        mip = front(os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt"), sm)
        pts = [q for f in F.values() for q in f] + mip
        b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
        hv = {t: G.hypervolume_2d(G.normalize_front(f, *b), (1.1, 1.1)) for t, f in F.items()}
        mm = min(q[0] for q in mip) if mip else None
        row = dict(instance=p, cond=c, mip_min_ms=mm)
        for t, f in F.items():
            row[f"{t}_min_ms"] = min(q[0] for q in f)
            row[f"{t}_ms_gap"] = (row[f"{t}_min_ms"] - mm) / mm if mm else None
            row[f"{t}_hv"] = hv[t]
        rows.append(row)
with open(os.path.join(BD, "analysis", "ext1001", "prseed_eval.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)


def stat(v):
    m = S.mean(v); sd = S.stdev(v) if len(v) > 1 else 0
    return f"mean {m:+.4f} (SE {sd/len(v)**0.5:.4f}), better {sum(1 for x in v if x>1e-9)}, worse {sum(1 for x in v if x<-1e-9)}"


print(f"conditions: {len(rows)} ({len({r['instance'] for r in rows})} instances)")
withmip = [r for r in rows if r["mip_min_ms"]]
for t in TAGS:
    g = [r[f"{t}_ms_gap"] for r in withmip]
    print(f"[{t:6s}] makespan-end gap vs MIP: median {S.median(g):.1%}, mean {S.mean(g):.1%}, exact {sum(1 for x in g if x<=1e-9)}, >5% {sum(1 for x in g if x>0.05)}, >10% {sum(1 for x in g if x>0.10)}")
ref = [(r["base_hv"] + r["rp1_hv"]) / 2 for r in rows]
print("HV noise (rp1 - base):", stat([r["rp1_hv"] - r["base_hv"] for r in rows]))
print("HV prseed - mean(base,rp1):", stat([r["prseed_hv"] - m for r, m in zip(rows, ref)]))
worst = sorted(withmip, key=lambda r: -r["base_ms_gap"])[:10]
print("10 conditions with the largest base makespan-end gap:")
for r in worst:
    print(f"  {r['instance']:8s} {r['cond']}  MIP {r['mip_min_ms']:.0f}  base {r['base_min_ms']:.0f}  rp1 {r['rp1_min_ms']:.0f}  prseed {r['prseed_min_ms']:.0f}")
