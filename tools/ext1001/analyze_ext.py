import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
#!/usr/bin/env python3
"""
analyze_ext.py -- 拡張コスト表(costs_ext)での P1 比較集計（ゼミ 2026-10-01 用）

公式 tools/gap48_analysis.py の関数（有効点ロード・前線・HV・C-metric）をそのまま再利用し、
  root=old : results/FUN_MIP , results/FUN_ENC           （旧コスト表）
  root=ext : results/EXT_COST/FUN_MIP , results/EXT_COST/FUN_ENC （拡張コスト表）
の両方で同じ指標を計算する。

指標
  hv_ratio  : 公式と同一定義。MIP∪手法 の点で min/max 正規化、ref=(1.1,1.1)、HV_手法/HV_MIP
  cost_gap  : 公式と同一（matched-makespan、平均）
  ms_gap    : 公式と同一（matched-cost、平均）
  hv_common : 3手法比較用。その(instance,cond)で得られた全前線（MIPがあれば含む）の和集合で
              正規化した HV。MIP が無い（j60 など）場合も 3手法を同じ物差しで比べられる。
"""
import os, sys, csv, itertools
BD = _os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, _os.path.join(ROOT, "tools"))
os.chdir(BD)
import gap48_analysis as G  # noqa: E402

CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
METHODS = G.METHODS  # MaxShiftDur / MaxShift / SchedObj
ROOTS = {"old": "results", "ext": os.path.join("results", "EXT_COST")}


def instances(size):
    return [f"{size}{x}_1" for x in range(1, 49)]


def load_front(root, kind, prefix, cond, method=None, size="j30"):
    sm = os.path.join(f"{size}.sm", f"{prefix}.sm")
    if kind == "MIP":
        p = os.path.join(ROOTS[root], "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
    else:
        p = os.path.join(ROOTS[root], "FUN_ENC", prefix,
                         "SCHED_ENC_" + METHODS[method].format(prefix=prefix, cond=cond) + ".txt")
    pts, st = G.load_valid_points(p, sm)
    if pts is None or st.get("missing"):
        return None
    return G.pareto_front(pts)


def hv_norm(front, lo_hi):
    ms_lo, ms_hi, c_lo, c_hi = lo_hi
    return G.hypervolume_2d(G.normalize_front(front, ms_lo, ms_hi, c_lo, c_hi), (1.1, 1.1))


def bounds(fronts):
    pts = [p for f in fronts for p in f]
    return (min(p[0] for p in pts), max(p[0] for p in pts),
            min(p[1] for p in pts), max(p[1] for p in pts))


def mean(v):
    return sum(v) / len(v) if v else None


def main():
    out = []
    for size in ["j30", "j60"]:
        for prefix in instances(size):
            for cond in CONDS:
                for root in (["old", "ext"] if size == "j30" else ["ext"]):
                    mip = load_front(root, "MIP", prefix, cond, size=size) if size == "j30" else None
                    if mip is not None and len(mip) == 0:
                        mip = None   # MIP 空前線（解けなかった条件）
                    fr = {m: load_front(root, "ENC", prefix, cond, m, size) for m in METHODS}
                    have = {m: f for m, f in fr.items() if f}
                    if len(have) < 3:
                        continue  # 3手法そろっていない（未実行など）
                    common = bounds(list(have.values()) + ([mip] if mip else []))
                    hvc = {m: hv_norm(f, common) for m, f in have.items()}
                    hv_mip_common = hv_norm(mip, common) if mip else None
                    for m, f in have.items():
                        row = dict(size=size, instance=prefix, cond=cond, root=root, method=m,
                                   n_front=len(f), hv_common=hvc[m],
                                   hv_mip_common=hv_mip_common, has_mip=int(mip is not None),
                                   hv_ratio="", cost_gap="", ms_gap="", c_mip_over="", reach=""
                                   )
                        if mip:
                            b = bounds([mip, f])
                            hm, hn = hv_norm(mip, b), hv_norm(f, b)
                            row["hv_ratio"] = hn / hm if hm > 0 else ""
                            cg = [(G.min_cost_at_or_before(f, ms) - c) / c for ms, c in mip
                                  if G.min_cost_at_or_before(f, ms) is not None]
                            mg = [(G.min_ms_at_or_below_cost(f, c) - ms) / ms for ms, c in mip
                                  if G.min_ms_at_or_below_cost(f, c) is not None]
                            row["cost_gap"] = mean(cg) if cg else ""
                            row["ms_gap"] = mean(mg) if mg else ""
                            cm = G.c_metric(mip, f)
                            row["c_mip_over"] = "" if cm is None else cm
                            rc = G.c_metric(f, mip)   # MIP 前線の点のうち NSGA が弱支配した（到達した）割合
                            row["reach"] = "" if rc is None else rc
                        out.append(row)
    os.makedirs(_os.path.join(ROOT, "cmake-build-release", "analysis", "ext1001"), exist_ok=True)
    path = _os.path.join(ROOT, "cmake-build-release", "analysis", "ext1001", "rows.csv")
    with open(path, "w", newline="", encoding="utf-8") as fp:
        w = csv.DictWriter(fp, fieldnames=list(out[0].keys()))
        w.writeheader(); w.writerows(out)
    print(f"[rows] {len(out)} -> {path}")


if __name__ == "__main__":
    main()
