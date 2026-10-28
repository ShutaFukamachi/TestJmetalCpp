"""
e2_eval.py -- 実験2・3の評価（PREREG_20261023.md の判定どおり）。j30 全48インスタンス × 6条件。
使い方: python e2_eval.py name=tagprefix:runs ...
  例: python e2_eval.py base=t1016_base:5 isrgp=t1016_isrgp:5 rg=t1023_rg:3 zw=t1023_zw:3 ms=t1023_ms:3 mszw=t1023_mszw:3
指標（手法ごと、回ごと→平均）:
  exact   : 工期最小端が MIP の最小工期と一致した条件数（/240）
  over10  : 10%以上ずれた条件数
  mid     : 前線中間部（位置0.2〜0.8）の、同じコストでの工期の差（%）
  reach   : 初期集団で MIP の最小工期に届かなかった条件のうち、最終的に届いた割合
  costend : コスト最小端の MIP との差（%）
  HV      : 全手法・全回と MIP を合わせて正規化した共通 HV（条件ごとに回の平均）。base・isrgp との差 ±2SE
"""
import os, sys, glob, csv, json, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402
E = os.path.join("results", "EXT_COST")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
M = {}
S0 = {}
for a in sys.argv[1:]:
    k, v = a.split("="); f = v.split(":"); M[k] = (f[0], int(f[1])); S0[k] = int(f[2]) if len(f) > 2 else 1  # tag:回数[:開始の回]


def fr(path, sm):
    if not os.path.exists(path):
        return None
    pts, _ = G.load_valid_points(path, sm)
    return G.pareto_front(pts) if pts else None


def init_best(p, c, tag):
    v = []
    for f in glob.glob(os.path.join(E, "TRACE", p, f"TRACE_{p}_{c}_S*_{tag}.csv")):
        for ln in open(f):
            q = ln.split(",")
            if q[0] == "init" and float(q[3]) < 1e8:
                v.append(float(q[3]))
    return min(v) if v else None


per = {m: {k: [] for k in ["exact", "over10", "mid", "reach_n", "reach_k", "costend"]} for m in M}
for m, (t, R) in M.items():
    for k in ["exact", "over10", "mid", "reach_n", "reach_k", "costend"]:
        per[m][k] = [[] for _ in range(R)]
hv = {m: [] for m in M}; ncond = 0
for x in range(1, 49):
    p = f"j30{x}_1"; sm = os.path.join("j30.sm", p + ".sm")
    for c in CONDS:
        F = {(m, r): fr(os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_{t}_r{r}.txt"), sm)
             for m, (t, R) in M.items() for r in range(S0[m], S0[m] + R)}
        if not all(F.values()):
            continue
        mip = fr(os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt"), sm) or []
        pts = [q for f in F.values() for q in f] + mip
        b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
        ncond += 1
        for m, (t, R) in M.items():
            hv[m].append(S.mean(G.hypervolume_2d(G.normalize_front(F[(m, r)], *b), (1.1, 1.1)) for r in range(S0[m], S0[m] + R)))
        if not mip:
            continue
        mm, mc = mip[0][0], min(q[1] for q in mip)
        for m, (t, R) in M.items():
            for r in range(S0[m], S0[m] + R):
                f = F[(m, r)]; i = r - S0[m]
                per[m]["exact"][i].append(1 if f[0][0] <= mm else 0)
                per[m]["over10"][i].append(1 if f[0][0] > 1.10 * mm else 0)
                per[m]["costend"][i].append((min(q[1] for q in f) - mc) / mc * 100)
                if len(mip) >= 3 and mip[-1][0] > mm:
                    g = []
                    for ms, co in mip:
                        pos = (ms - mm) / (mip[-1][0] - mm)
                        if 0.2 <= pos <= 0.8:
                            h = G.min_ms_at_or_below_cost(f, co)
                            if h is not None:
                                g.append((h - ms) / ms * 100)
                    if g:
                        per[m]["mid"][i].append(S.mean(g))
                ib = init_best(p, c, f"{t}_r{r}")
                if ib is not None and ib > mm:
                    per[m]["reach_n"][i].append(1); per[m]["reach_k"][i].append(1 if f[0][0] <= mm else 0)

out = {"n_cond": ncond}
print(f"j30: {ncond} 条件")
for m, (t, R) in M.items():
    ex = [sum(v) for v in per[m]["exact"]]; o10 = [sum(v) for v in per[m]["over10"]]
    mid = [S.mean(v) for v in per[m]["mid"]]; ce = [S.mean(v) for v in per[m]["costend"]]
    rc = [sum(k) / len(n) if n else float("nan") for n, k in zip(per[m]["reach_n"], per[m]["reach_k"])]
    out[m] = dict(exact=ex, over10=o10, mid=mid, reach=rc, costend=ce)
    print(f"[{m:8s}] 工期端=MIP {ex} 平均 {S.mean(ex):.1f} | 10%超 {o10} | 中間部 {S.mean(mid):.2f}% | "
          f"探索で到達 {S.mean(rc):.0%}（{min(rc):.0%}〜{max(rc):.0%}）| コスト端の差 {S.mean(ce):.3f}%")
for ref in [r for r in ["base", "isrgp"] if r in M]:
    for m in M:
        if m == ref:
            continue
        d = [a - b for a, b in zip(hv[m], hv[ref])]
        se = 2 * S.stdev(d) / len(d) ** .5
        out.setdefault("hv", {})[f"{m}-{ref}"] = dict(mean=S.mean(d), se2=se, better=sum(v > 0 for v in d), worse=sum(v < 0 for v in d))
        print(f"  HV {m} − {ref}: {S.mean(d):+.4f} ± {se:.4f}（改善 {sum(v > 0 for v in d)} / 悪化 {sum(v < 0 for v in d)}）")
if "isrgp" in M:
    iex = min(out["isrgp"]["exact"]); irc = min(out["isrgp"]["reach"])
    print(f"判定基準: (a) 一致数平均 ≥ {iex}  (b) 到達割合平均 ≥ {irc:.0%}  (c) HV − isrgp の平均+2SE ≥ 0")
    for m in M:
        if m in ("base", "isrgp"):
            continue
        h = out["hv"].get(f"{m}-isrgp")
        a = S.mean(out[m]["exact"]) >= iex; b = S.mean(out[m]["reach"]) >= irc; c = h["mean"] + h["se2"] >= 0
        print(f"  {m:8s}: (a) {a}  (b) {b}  (c) {c}  → {'1集団で十分' if a and b and c else '不十分'}")
        out[m]["verdict"] = dict(a=a, b=b, c=c)
json.dump(out, open(os.path.join(BD, "analysis", "ext1014", "e2_eval_" + "_".join(M) + ".json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
