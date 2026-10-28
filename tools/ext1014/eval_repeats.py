"""
eval_repeats.py -- 事前登録（PREREG_20261016.md）どおりに複数回実行の結果を集計する。

  j30 : base / isrgp / isrg × 5回（t1016_<method>_r<k>）
  j120: base / isrgp × 3回（j1201_1〜j12020_1）
出力: analysis/ext1014/repeats_j30_rows.csv, repeats_j120_rows.csv と標準出力の判定
使い方: python eval_repeats.py [j30|j120|all]
"""
import os, sys, glob, csv, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402

E = os.path.join("results", "EXT_COST")
OUT = os.path.join(BD, "analysis", "ext1014")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
WHICH = sys.argv[1] if len(sys.argv) > 1 else "all"
M30 = sys.argv[2].split(",") if len(sys.argv) > 2 else ["base", "isrgp", "isrg"]
M120 = sys.argv[3].split(",") if len(sys.argv) > 3 else ["base", "isrgp"]


def front(path, sm):
    if not os.path.exists(path):
        return None
    pts, _ = G.load_valid_points(path, sm)
    return G.pareto_front(pts) if pts else None


def mid_gap(f, mip):
    g = [(G.min_ms_at_or_below_cost(f, c) - ms) / ms for ms, c in mip if G.min_ms_at_or_below_cost(f, c) is not None]
    return S.mean(g) if g else None


def init_best(p, c, tag):
    v = []
    for f in glob.glob(os.path.join(E, "TRACE", p, f"TRACE_{p}_{c}_S*_{tag}.csv")):
        for ln in open(f):
            q = ln.split(",")
            if q[0] == "init" and float(q[3]) < 1e8:
                v.append(float(q[3]))
    return min(v) if v else None


def ms2(v):
    return f"{S.mean(v):+.4f} ± {2 * S.stdev(v) / len(v) ** .5:.4f}"


def run(size, n_inst, methods, reps, mip_ok):
    rows = []
    for x in range(1, n_inst + 1):
        p = f"{size}{x}_1"; sm = os.path.join(f"{size}.sm", p + ".sm")
        for c in CONDS:
            F = {(m, r): front(os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_t1016_{m}_r{r}.txt"), sm)
                 for m in methods for r in range(1, reps + 1)}
            if not all(F.values()):
                continue
            mip = (front(os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt"), sm) or []) if mip_ok else []
            pts = [q for f in F.values() for q in f] + mip
            b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
            row = dict(instance=p, cond=c, mip_min_ms=mip[0][0] if mip else None)
            for (m, r), f in F.items():
                row[f"{m}_r{r}_min_ms"] = f[0][0]
                row[f"{m}_r{r}_hv"] = G.hypervolume_2d(G.normalize_front(f, *b), (1.1, 1.1))
                if mip:
                    row[f"{m}_r{r}_mid"] = mid_gap(f, mip)
                    row[f"{m}_r{r}_init"] = init_best(p, c, f"t1016_{m}_r{r}")
            for m in methods:
                row[f"{m}_hv"] = S.mean(row[f"{m}_r{r}_hv"] for r in range(1, reps + 1))
            rows.append(row)
    return rows


def report_j30(rows):
    methods, reps = M30, 5
    wm = [r for r in rows if r["mip_min_ms"]]
    print(f"== j30: {len(rows)} conditions ({len(wm)} with MIP), {reps} runs each")
    ex = {}
    for m in methods:
        e = [sum(1 for r in wm if r[f"{m}_r{k}_min_ms"] <= r["mip_min_ms"]) for k in range(1, reps + 1)]
        b10 = [sum(1 for r in wm if r[f"{m}_r{k}_min_ms"] > 1.10 * r["mip_min_ms"]) for k in range(1, reps + 1)]
        mid = [S.mean(r[f"{m}_r{k}_mid"] for r in wm if r[f"{m}_r{k}_mid"] is not None) for k in range(1, reps + 1)]
        reach = []
        for k in range(1, reps + 1):
            miss = [r for r in wm if r[f"{m}_r{k}_init"] and r[f"{m}_r{k}_init"] > r["mip_min_ms"]]
            reach.append(sum(1 for r in miss if r[f"{m}_r{k}_min_ms"] <= r["mip_min_ms"]) / len(miss))
        ex[m] = e
        print(f"[{m:5s}] MIP一致 {e} 平均 {S.mean(e):.1f} | 10%超 {b10} | 中間部の工期差 平均 {S.mean(mid):.2%}"
              f" | 初期で届かず探索で到達 {S.mean(reach):.0%} ({min(reach):.0%}〜{max(reach):.0%})")
    j = [r for r in wm if r["instance"] == "j309_1" and r["cond"] == "RR050_RV0"]
    if j:
        print("  j309_1 RR050_RV0 最小工期:", {m: [j[0][f"{m}_r{k}_min_ms"] for k in range(1, reps + 1)] for m in methods})
    for m in [x for x in methods if x != "base"]:
        d = [r[f"{m}_hv"] - r["base_hv"] for r in rows]
        print(f"  HV差 {m} - base（条件ごとに5回平均）: {ms2(d)}  改善 {sum(1 for v in d if v > 0)} 悪化 {sum(1 for v in d if v < 0)}")
    d = [r["isrgp_hv"] - r["base_hv"] for r in rows]
    a = S.mean(ex["isrgp"]) > max(ex["base"])
    bb = S.mean(d) + 2 * S.stdev(d) / len(d) ** .5 >= 0
    print(f"  判定 (a) 一致数の平均 {S.mean(ex['isrgp']):.1f} > base の最大 {max(ex['base'])}: {a}")
    print(f"  判定 (b) HV差の平均+2SE >= 0: {bb}")


def report_j120(rows):
    methods, reps = M120, 3
    bks = {}
    for ln in open(os.path.join(HERE, "psplib_j120hrs.txt"), encoding="utf-8"):
        if ln.strip() and not ln.startswith("#"):
            k, v = ln.split(); bks[k] = int(v)
    print(f"== j120: {len(rows)} conditions, {reps} runs each")
    gap = {}
    for m in methods:
        g = [S.mean((r[f"{m}_r{k}_min_ms"] - bks[r["instance"]]) / bks[r["instance"]] for r in rows if r["cond"] == "RR000_RV0")
             for k in range(1, reps + 1)]
        gap[m] = g
        print(f"[{m:5s}] RR000_RV0 最良既知との差（各回の平均）: {[f'{v:.1%}' for v in g]}  平均 {S.mean(g):.1%}")
    for m in [x for x in methods if x not in ("base", "isrgp")]:
        d2 = [r[f"{m}_hv"] - r["base_hv"] for r in rows]
        print(f"  HV差 {m} - base（条件ごとに3回平均）: {ms2(d2)}  改善 {sum(1 for v in d2 if v > 0)} 悪化 {sum(1 for v in d2 if v < 0)}")
    d = [r["isrgp_hv"] - r["base_hv"] for r in rows]
    print(f"  HV差 isrgp - base（条件ごとに3回平均）: {ms2(d)}  改善 {sum(1 for v in d if v > 0)} 悪化 {sum(1 for v in d if v < 0)}")
    print(f"  判定 (c) 最良既知との差 isrgp {S.mean(gap['isrgp']):.1%} < base {S.mean(gap['base']):.1%}: {S.mean(gap['isrgp']) < S.mean(gap['base'])}")
    print(f"  判定 (d) HV差の平均+2SE >= 0: {S.mean(d) + 2 * S.stdev(d) / len(d) ** .5 >= 0}")


for size, n, methods, reps, mip_ok, rep in [("j30", 48, M30, 5, True, report_j30),
                                             ("j120", 20, M120, 3, False, report_j120)]:
    if WHICH not in (size, "all"):
        continue
    rows = run(size, n, methods, reps, mip_ok)
    if not rows:
        print(f"{size}: まだ結果がありません"); continue
    with open(os.path.join(OUT, f"repeats_{size}_rows" + ("" if len(sys.argv) <= 2 else "_ext") + ".csv"), "w", newline="", encoding="utf-8") as fp:
        w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    rep(rows)
