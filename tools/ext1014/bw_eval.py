"""
bw_eval.py -- 事前登録（PREREG_20261016.md 追加分）どおりに、ブロック窓変異（bwisrgp）を isrgp と比べる。
 (e) j30 の中間部（前線位置 0.2〜0.8）の横の差（同じコストでの工期差）の5回平均 < isrgp の5回の最小値
 (f) HV 差（bwisrgp − isrgp）の平均 + 2SE >= 0（j30・j120）
使い方: python bw_eval.py [j30|j120|all]
"""
import os, sys, json, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402
E = os.path.join("results", "EXT_COST")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
WHICH = sys.argv[1] if len(sys.argv) > 1 else "all"
M = ["isrgp", "bwisrgp"]
out = {}


def fr(path, sm):
    if not os.path.exists(path):
        return None
    pts, _ = G.load_valid_points(path, sm)
    return G.pareto_front(pts) if pts else None


for size, n, reps in [("j30", 48, 5), ("j120", 20, 3)]:
    if WHICH not in (size, "all"):
        continue
    mid = {m: [[] for _ in range(reps)] for m in M}; hvd = []
    for x in range(1, n + 1):
        p = f"{size}{x}_1"; sm = os.path.join(f"{size}.sm", p + ".sm")
        for c in CONDS:
            F = {(m, r): fr(os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_t1016_{m}_r{r}.txt"), sm)
                 for m in M for r in range(1, reps + 1)}
            if not all(F.values()):
                continue
            mip = fr(os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt"), sm) if size == "j30" else None
            pts = [q for f in F.values() for q in f] + (mip or [])
            b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
            hv = {k: G.hypervolume_2d(G.normalize_front(f, *b), (1.1, 1.1)) for k, f in F.items()}
            hvd.append(S.mean(hv[("bwisrgp", r)] for r in range(1, reps + 1)) - S.mean(hv[("isrgp", r)] for r in range(1, reps + 1)))
            if mip and len(mip) >= 3 and mip[-1][0] > mip[0][0]:
                for (m, r), f in F.items():
                    for ms, co in mip:
                        pos = (ms - mip[0][0]) / (mip[-1][0] - mip[0][0])
                        if 0.2 <= pos <= 0.8:
                            h = G.min_ms_at_or_below_cost(f, co)
                            if h is not None:
                                mid[(m)][r - 1].append((h - ms) / ms * 100)
    se = 2 * S.stdev(hvd) / len(hvd) ** .5
    res = dict(n_cond=len(hvd), hv_diff=S.mean(hvd), hv_2se=se, better=sum(1 for v in hvd if v > 0), worse=sum(1 for v in hvd if v < 0))
    print(f"== {size}: {len(hvd)} 条件、各{reps}回")
    print(f"  HV差 bwisrgp - isrgp: {res['hv_diff']:+.4f} ± {se:.4f}  改善 {res['better']} 悪化 {res['worse']}  → (f) {res['hv_diff'] + se >= 0}")
    if size == "j30":
        mm = {m: [S.mean(v) for v in mid[m]] for m in M}
        res["mid_runs"] = mm
        print(f"  中間部（位置0.2〜0.8）の横の差: isrgp {['%.2f' % v for v in mm['isrgp']]}  bwisrgp {['%.2f' % v for v in mm['bwisrgp']]}")
        res["e"] = S.mean(mm["bwisrgp"]) < min(mm["isrgp"])
        print(f"  (e) bwisrgp 平均 {S.mean(mm['bwisrgp']):.2f}% < isrgp 最小 {min(mm['isrgp']):.2f}%: {res['e']}")
    out[size] = res
json.dump(out, open(os.path.join(BD, "analysis", "ext1014", f"bw_eval_{WHICH}.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
