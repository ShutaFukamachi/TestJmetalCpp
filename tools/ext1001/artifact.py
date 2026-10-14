"""artifact.py -- 指摘1: 旧コスト表（t>=T0 を最終値に固定）の MIP 前線が、正しい（拡張）コストで
どれだけ見かけの解だったかを、拡張 MIP が終わった全 (instance, cond) で数える。

各 (instance, cond) について:
  - 旧前線の各点について、スケジュールが t>=T0 の時刻を使っているか（クランプ領域に入るか）
  - 旧前線の各点を拡張コスト表で計算し直したコスト
  - その点が拡張 MIP 前線に（工期もコストも）支配されるか
  - 旧の最小コストと、拡張での最小コスト
"""
import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import os, csv, json
import numpy as np
from msd_decoder import Case, BD

CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
rows = []
ext_root = os.path.join(BD, "results", "EXT_COST", "FUN_MIP")
for p in sorted(os.listdir(ext_root)):
    for c in CONDS:
        so = os.path.join(BD, "results", "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt")
        se = os.path.join(ext_root, p, f"SCHED_MIP_{p}_{c}.txt")
        if not (os.path.exists(so) and os.path.exists(se)):
            continue
        sm = os.path.join(BD, "j30.sm", p + ".sm")
        try:
            old = Case(so, sm, os.path.join(BD, "costs_ext", f"costs_{p}.csv"))   # 旧スケジュールを拡張表で評価
            ext = Case(se, sm, os.path.join(BD, "costs_ext", f"costs_{p}.csv"))
        except Exception:
            continue
        if not old.sols or not ext.sols:
            continue
        T0 = int(open(os.path.join(BD, "costs", f"costs_{p}.csv")).readline().split()[1])
        efront = [(ms, cost) for ms, cost, _ in ext.sols]
        for ms, cost, s in old.sols:
            fin = max(s[j] + old.dur[j] for j in range(old.n))
            uses_clamp = int(fin > T0)
            recost = float(sum(old.jobcost[j, s[j]] for j in range(old.n) if old.dur[j] > 0))
            dom = int(any(em <= ms and ec <= recost and (em < ms or ec < recost - 1e-6) for em, ec in efront))
            rows.append(dict(instance=p, cond=c, T0=T0, ms=ms, old_cost=cost, recost_ext=recost,
                             rel_change=(recost - cost) / cost, uses_clamp=uses_clamp, dominated_by_ext=dom))
out = os.path.join(BD, "analysis", "ext1001", "artifact_rows.csv")
with open(out, "w", newline="") as fp:
    w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
n = len(rows); cl = [r for r in rows if r["uses_clamp"]]; ncl = [r for r in rows if not r["uses_clamp"]]
summ = dict(n_points=n, n_inst=len({r["instance"] for r in rows}), n_cond=len({(r["instance"], r["cond"]) for r in rows}),
            frac_uses_clamp=len(cl) / n,
            clamp_mean_rel_change=float(np.mean([r["rel_change"] for r in cl])) if cl else None,
            clamp_max_rel_change=float(np.max([r["rel_change"] for r in cl])) if cl else None,
            nonclamp_max_abs_rel_change=float(np.max([abs(r["rel_change"]) for r in ncl])) if ncl else None,
            clamp_dominated=sum(r["dominated_by_ext"] for r in cl), n_clamp=len(cl),
            conds_with_clamp_points=len({(r["instance"], r["cond"]) for r in cl}))
json.dump(summ, open(os.path.join(BD, "analysis", "ext1001", "artifact_summary.json"), "w"), indent=1)
print(json.dumps(summ, indent=1))
