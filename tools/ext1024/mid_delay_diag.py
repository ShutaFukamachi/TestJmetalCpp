"""
mid_delay_diag.py -- 前線中間部で、MIP と NSGA（新しい最終設定）は「どの作業を・どれだけ」遅らせているか（10/1 指摘「max_shift をどう決めるか」）
各解のスケジュール（開始時刻）を開始順の作業リストとして復号し直し、作業ごとに
  delay = 開始時刻 − t_mak（その時点で置ける最早時刻）
  save  = jobcost[t_mak] − jobcost[開始時刻]（遅らせて得たコストの節約）
  trend = その作業の「後ろに行くほど安いか」：jobcost[t_mak] − min(jobcost[t_mak .. t_mak+L])（L=T/10、遅らせたら得られる節約の上限の目安）
  k     = t_mak から開始時刻までに「それまでの最安値を更新した回数」（窓をコストが下がる地点ごとに数えた段数）
を記録する。中間部 = MIP 前線の工期で 0.2〜0.8 の位置。NSGA は各 MIP 中間点と同じコスト以下で工期最小の点を対応させる。
使い方: python mid_delay_diag.py [tag]   （tag 既定 t1016_prtbw3_r1）
出力: analysis/ext1024/mid_delay_jobs.csv（作業ごと）, mid_delay_sols.csv（解ごと）
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
OUT = os.path.join(BD, "analysis", "ext1024"); os.makedirs(OUT, exist_ok=True)


def case(path, p):
    return M.Case(path, os.path.join(BD, "j30.sm", p + ".sm"), os.path.join(BD, "costs_ext", f"costs_{p}.csv"))


def analyse(C, starts):
    """開始順に復号し直して作業ごとの delay 等を返す。再現できなければ None。"""
    n, T = C.n, C.T
    seq = M.start_order(starts)
    win = {}

    def wf(j, tmak):
        win[j] = tmak
        return max(0, starts[j] - tmak)
    r = C.decode(seq, wf, record_tmak=True)
    if r is None:
        return None
    st, ms, cost, tmak = r
    L = max(5, T // 10)
    rows = []
    for j in range(n):
        d = int(C.dur[j])
        if d <= 0:
            continue
        t0, s = int(tmak[j]), int(st[j])
        jc = C.jobcost[j]
        seg = jc[t0:min(T - d, t0 + L) + 1]
        k, best = 0, jc[t0]
        for t in range(t0 + 1, s + 1):
            if jc[t] < best:
                best = jc[t]; k += 1
        rows.append(dict(job=j, dur=d, start=s, tmak=t0, delay=s - t0, save=float(jc[t0] - jc[s]),
                         trend=float(jc[t0] - np.min(seg)), k=k, rel_start=s / max(1, ms)))
    return dict(ms=ms, cost=cost, same=bool(np.array_equal(st, np.array(starts))), jobs=rows)


jobs_out, sols_out = [], []
for x in range(1, 49):
    p = f"j30{x}_1"
    for c in CONDS:
        fm = os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt")
        fn = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_{TAG}.txt")
        if not (os.path.exists(fm) and os.path.exists(fn)):
            continue
        Cm, Cn = case(fm, p), case(fn, p)
        mip = sorted(Cm.sols); nsg = sorted(Cn.sols)
        if len(mip) < 3:
            continue
        m0, m1 = mip[0][0], mip[-1][0]
        for ms_m, c_m, st_m in mip:
            pos = (ms_m - m0) / max(1e-9, m1 - m0)
            if not (0.2 <= pos <= 0.8):
                continue
            cand = [s for s in nsg if s[1] <= c_m + 1e-6]
            if not cand:
                continue
            ms_n, c_n, st_n = min(cand, key=lambda s: (s[0], s[1]))
            A = analyse(Cm, st_m); B = analyse(Cn, st_n)
            if A is None or B is None:
                continue
            key = f"{p}|{c}|{ms_m:.0f}"
            for who, R, ms, cost in [("MIP", A, ms_m, c_m), ("NSGA", B, ms_n, c_n)]:
                js = R["jobs"]
                dl = [r for r in js if r["delay"] > 0]
                sols_out.append(dict(key=key, inst=p, cond=c, pos=round(pos, 3), who=who, ms=ms, cost=round(cost, 2),
                                     reproduced=R["same"], n_jobs=len(js), n_delayed=len(dl),
                                     sum_delay=sum(r["delay"] for r in js), sum_save=round(sum(r["save"] for r in js), 2),
                                     mean_k=round(np.mean([r["k"] for r in dl]), 3) if dl else 0.0,
                                     gap_ms=round((ms_n - ms_m) / ms_m * 100, 3)))
                for r in js:
                    jobs_out.append(dict(key=key, who=who, pos=round(pos, 3), **{k: (round(v, 3) if isinstance(v, float) else v) for k, v in r.items()}))
    print(p, len(sols_out), flush=True)

for fn, rows in [("mid_delay_sols.csv", sols_out), ("mid_delay_jobs.csv", jobs_out)]:
    with open(os.path.join(OUT, fn), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
print("saved", len(sols_out), len(jobs_out))
