"""rho_analyze.py -- 指摘6 実験: 窓幅 win = round(rho^k * eff) の効果（拡張コスト表、j30、P1）

比較対象（すべて MaxShiftDur、同じ (instance,cond) で共通正規化した HV で比べる）
  base : 公式設定の拡張コスト表実行（k=1、評価 50000/戦略）
  rp1  : k=1 をもう一度（ノイズの大きさを測る対照）
  rp3, rp5 : k=3, k=5（評価 50000）
  rp1q, rp3q : k=1, k=3 を評価 12500（1/4 の時間）
"""
import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import os, sys, json, math, statistics as S
BD = _os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, _os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G

CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
TAGS = {"base": "", "rp1": "_rp1", "rp3": "_rp3", "rp5": "_rp5", "rp1q": "_rp1q", "rp3q": "_rp3q"}


def front(p, cond, suffix):
    path = os.path.join("results", "EXT_COST", "FUN_ENC", p, f"SCHED_ENC_{p}_{cond}_MaxShiftDur{suffix}.txt")
    pts, st = G.load_valid_points(path, os.path.join("j30.sm", p + ".sm"))
    return G.pareto_front(pts) if pts else None


def hv(f, b):
    return G.hypervolume_2d(G.normalize_front(f, *b), (1.1, 1.1))


rows = []
for x in range(1, 49):
    p = f"j30{x}_1"
    for c in CONDS:
        F = {t: front(p, c, s) for t, s in TAGS.items()}
        if any(F[t] is None for t in TAGS if t != "base"):
            continue
        pts = [q for f in F.values() if f for q in f]
        b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
        rows.append(dict(inst=p, cond=c, **{t: (hv(f, b) if f else None) for t, f in F.items()}))


def stat(v):
    m = S.mean(v); sd = S.stdev(v) if len(v) > 1 else 0.0
    return dict(n=len(v), mean=m, sd=sd, se=sd / math.sqrt(len(v)) if v else None,
                pos=sum(1 for x in v if x > 1e-9), neg=sum(1 for x in v if x < -1e-9))


out = {"n_pairs": len(rows), "n_inst": len({r["inst"] for r in rows})}
if rows:
    ref = [((r["base"] + r["rp1"]) / 2 if r["base"] is not None else r["rp1"]) for r in rows]
    nb = [r["rp1"] - r["base"] for r in rows if r["base"] is not None]
    out["noise_rp1_minus_base"] = stat(nb) if len(nb) > 1 else None
    out["rp3_minus_k1avg"] = stat([r["rp3"] - m for r, m in zip(rows, ref)])
    out["rp5_minus_k1avg"] = stat([r["rp5"] - m for r, m in zip(rows, ref)])
    out["rp3q_minus_rp1q"] = stat([r["rp3q"] - r["rp1q"] for r in rows])
    out["rp1q_minus_k1avg"] = stat([r["rp1q"] - m for r, m in zip(rows, ref)])
    out["rp3q_minus_k1avg"] = stat([r["rp3q"] - m for r, m in zip(rows, ref)])
    out["mean_hv"] = {t: S.mean(r[t] for r in rows if r[t] is not None) for t in TAGS if any(r[t] is not None for r in rows)}
    by = {}
    for c in CONDS:
        rr = [r for r in rows if r["cond"] == c]
        if rr:
            rf = [((r["base"] + r["rp1"]) / 2 if r["base"] is not None else r["rp1"]) for r in rr]
            by[c] = dict(n=len(rr), rp3=S.mean(r["rp3"] - m for r, m in zip(rr, rf)),
                         rp5=S.mean(r["rp5"] - m for r, m in zip(rr, rf)))
    out["by_cond"] = by
json.dump(out, open(os.path.join(BD, "analysis", "ext1001", "rho_summary.json"), "w"), indent=1)
print(json.dumps(out, indent=1))
