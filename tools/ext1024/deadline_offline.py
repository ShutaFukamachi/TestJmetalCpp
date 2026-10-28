"""
deadline_offline.py -- 「目標工期 D から逆算した最遅開始時刻で窓を打ち切る」案の事前確認（GA は回さない）
NSGA（新しい最終設定）の前線の各解について、作業リスト=開始順、窓幅=実際の遅らせ量 で、
  窓の上限を LST_j(D) = D − tail_j（tail_j = j から終端までの先行関係だけの最長路、所要時間込み）に打ち切って復号し直す。
D は MIP 中間点の工期。打ち切った解 (ms', cost') と、
  (a) MIP の点 (D, c_m)
  (b) NSGA の前線で工期 ≤ D の最小コスト（今の前線の実力）
を比べる。出力: analysis/ext1024/deadline_offline.csv
"""
import os, sys, csv
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools", "ext1001"))
import msd_decoder as M  # noqa: E402

TAG = sys.argv[1] if len(sys.argv) > 1 else "t1016_prtbw3_r1"
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
E = os.path.join(BD, "results", "EXT_COST")


def tails(C):
    order = []
    seen = [False] * C.n

    def dfs(j):
        seen[j] = True
        for s in C.succ[j]:
            if not seen[s]:
                dfs(s)
        order.append(j)
    for j in range(C.n):
        if not seen[j]:
            dfs(j)
    tail = np.zeros(C.n, dtype=int)
    for j in order:  # 後続が先に処理される
        tail[j] = int(C.dur[j]) + max((tail[s] for s in C.succ[j]), default=0)
    return tail


rows = []
sys.setrecursionlimit(10000)
for x in range(1, 49):
    p = f"j30{x}_1"
    for c in CONDS:
        fm = os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt")
        fn = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_{TAG}.txt")
        if not (os.path.exists(fm) and os.path.exists(fn)):
            continue
        Cm, Cn = (M.Case(f, os.path.join(BD, "j30.sm", p + ".sm"), os.path.join(BD, "costs_ext", f"costs_{p}.csv")) for f in (fm, fn))
        mip = sorted(Cm.sols); nsg = sorted(Cn.sols)
        if len(mip) < 3:
            continue
        tail = tails(Cn)
        m0, m1 = mip[0][0], mip[-1][0]
        for ms_m, c_m, _ in mip:
            pos = (ms_m - m0) / max(1e-9, m1 - m0)
            if not (0.2 <= pos <= 0.8):
                continue
            D = int(ms_m)
            own = [s[1] for s in nsg if s[0] <= D + 1e-9]
            own_c = min(own) if own else None
            best = None
            for ms_n, c_n, st in nsg:
                if ms_n <= D:
                    continue
                seq = M.start_order(st)
                r = Cn.decode(seq, lambda j, t: max(0, min(st[j] - t, D - int(tail[j]) - t)))
                if r is None:
                    continue
                _, ms2, c2 = r
                if ms2 <= D and (best is None or c2 < best[1]):
                    best = (ms2, c2, ms_n)
            rows.append(dict(inst=p, cond=c, pos=round(pos, 3), D=D, mip_cost=round(c_m, 2),
                             own_cost=(round(own_c, 2) if own_c is not None else ""),
                             clamp_cost=(round(best[1], 2) if best else ""), clamp_from_ms=(best[2] if best else "")))
    print(p, len(rows), flush=True)
with open(os.path.join(BD, "analysis", "ext1024", "deadline_offline.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
print("saved", len(rows))
