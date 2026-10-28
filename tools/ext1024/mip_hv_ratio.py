"""
mip_hv_ratio.py -- j30 全48×6条件で、NSGA の前線の HV ÷ MIP の前線の HV（2つの前線を合わせて正規化、参照点 (1.1, 1.1)）
各手法 5回の平均。1.0 なら MIP と同じ前線。出力: analysis/ext1024/mip_hv_ratio.json
"""
import os, sys, json, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402
E = os.path.join("results", "EXT_COST")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
M = {"base": "t1016_base", "prtbw3": "t1016_prtbw3", "dl": "t1024_dl"}
res = {m: [] for m in M}; byrv = {m: {"RV0": [], "RV1": []} for m in M}
for x in range(1, 49):
    p = f"j30{x}_1"; sm = os.path.join("j30.sm", p + ".sm")
    for c in CONDS:
        fm = os.path.join(E, "FUN_MIP", p, f"FUN_MIP_{p}_{c}.txt")
        if not os.path.exists(fm):
            continue
        mip = G.pareto_front([tuple(map(float, l.split()[:2])) for l in open(fm) if l.strip()])
        if len(mip) < 2:
            continue
        for m, t in M.items():
            rs = []
            for r in range(1, 6):
                f = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_{t}_r{r}.txt")
                if not os.path.exists(f):
                    continue
                pts, _ = G.load_valid_points(f, sm)
                nf = G.pareto_front(pts)
                allp = mip + nf
                b = (min(q[0] for q in allp), max(q[0] for q in allp), min(q[1] for q in allp), max(q[1] for q in allp))
                hm = G.hypervolume_2d(G.normalize_front(mip, *b), (1.1, 1.1)); hn = G.hypervolume_2d(G.normalize_front(nf, *b), (1.1, 1.1))
                if hm > 0:
                    rs.append(hn / hm)
            if rs:
                res[m].append(S.mean(rs)); byrv[m][c[-3:]].append(S.mean(rs))
    print(p, flush=True)
out = {m: dict(mean=S.mean(v), median=S.median(v), n=len(v), rv0=S.mean(byrv[m]["RV0"]), rv1=S.mean(byrv[m]["RV1"])) for m, v in res.items()}
json.dump(out, open(os.path.join("analysis", "ext1024", "mip_hv_ratio.json"), "w", encoding="utf-8"), indent=1)
print(json.dumps(out, indent=1))
