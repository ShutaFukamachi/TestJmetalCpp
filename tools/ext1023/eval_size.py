"""
eval_size.py -- MIP のない大きいサイズ（j60/j90/j120）の比較
  RR000_RV0（容量一定＝標準 RCPSP）: PSPLIB 最良既知値との差
  全条件: 全手法・全回で正規化した共通 HV（条件ごとに回の平均）、base との差 ±2SE
使い方: python eval_size.py <size> <n_inst> name=tagprefix:runs ...
  例: python eval_size.py j90 48 base=t1016_base:2 prbwisrgp=t1016_prbwisrgp:2
"""
import os, sys, json, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402
size, n = sys.argv[1], int(sys.argv[2])
M = {}
for a in sys.argv[3:]:
    k, v = a.split("="); t, r = v.split(":"); M[k] = (t, int(r))
fn = {"j60": "psplib_j60hrs.txt", "j90": "psplib_j90hrs.txt", "j120": "psplib_j120hrs.txt"}[size]
bks = {l.split()[0]: int(l.split()[1]) for l in open(os.path.join(ROOT, "tools", "ext1014", fn), encoding="utf-8")
       if l.strip() and not l.startswith("#")}
E = os.path.join("results", "EXT_COST")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
gap = {m: [] for m in M}; hv = {m: [] for m in M}; below = 0; best = {m: 0 for m in M}
for x in range(1, n + 1):
    p = f"{size}{x}_1"; sm = os.path.join(f"{size}.sm", p + ".sm")
    for c in CONDS:
        F = {}
        for m, (t, R) in M.items():
            for r in range(1, R + 1):
                f = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_{t}_r{r}.txt")
                if not os.path.exists(f):
                    f = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_{t}.txt") if r == 1 else f
                if os.path.exists(f):
                    pts, _ = G.load_valid_points(f, sm)
                    if pts:
                        F[(m, r)] = G.pareto_front(pts)
        if len(F) != sum(R for _, R in M.values()):
            continue
        pts = [q for f in F.values() for q in f]
        b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
        for m, (t, R) in M.items():
            hv[m].append(S.mean(G.hypervolume_2d(G.normalize_front(F[(m, r)], *b), (1.1, 1.1)) for r in range(1, R + 1)))
        mn = min(f[0][0] for f in F.values())
        for m, (t, R) in M.items():
            best[m] += S.mean(F[(m, r)][0][0] for r in range(1, R + 1)) <= mn + 1e-9
            if c == "RR000_RV0":
                g = S.mean((F[(m, r)][0][0] - bks[p]) / bks[p] * 100 for r in range(1, R + 1))
                gap[m].append(g)
                below += any(F[(m, r)][0][0] < bks[p] for r in range(1, R + 1))
nc = len(next(iter(hv.values())))
out = {"size": size, "n_cond": nc, "below_bks": below}
print(f"{size}: {nc} 条件、RR000_RV0 {len(next(iter(gap.values())))} 条件（最良既知値を下回った実行: {below} ← 0 でなければ表の読み込みミス）")
ref = next(iter(M))
for m in M:
    d = [a - b for a, b in zip(hv[m], hv[ref])]
    se = 2 * S.stdev(d) / len(d) ** .5 if len(d) > 1 else 0
    out[m] = dict(bks_gap=S.mean(gap[m]) if gap[m] else None, hv_diff=S.mean(d), hv_2se=se,
                  better=sum(v > 0 for v in d), worse=sum(v < 0 for v in d))
    print(f"[{m:10s}] 最良既知との差 {S.mean(gap[m]):.2f}% | HV − {ref}: {S.mean(d):+.4f} ± {se:.4f}（改善 {out[m]['better']} / 悪化 {out[m]['worse']}）")
json.dump(out, open(os.path.join(BD, "analysis", "ext1014", f"size_{size}_" + "_".join(M) + ".json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
