"""
rv1_diag.py -- 休暇あり（RV1）の前線中間部で、鎖上の遅らせが休暇日をまたいでいるか（課題1の診断）
mid_chain_diag.py と同じ対（MIP の中間点と、同じコスト以下で工期最小の NSGA 点）について、鎖上の遅らせた作業ごとに
  cross = 遅らせ区間 [t_mak, 開始) に休暇日（全資源の容量0）が入っているか
  jump  = 遅らせ区間の中の休暇日の数
  save  = 遅らせで得たコストの節約、eff = save / 遅らせ
を集計する。使い方: python rv1_diag.py [tag]（既定 t1024_dl_r1）
出力: analysis/ext1028/rv1_chainjobs.csv
"""
import os, sys, csv
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools", "ext1001"))
import msd_decoder as M  # noqa: E402
TAG = sys.argv[1] if len(sys.argv) > 1 else "t1024_dl_r1"
E = os.path.join(BD, "results", "EXT_COST")
OUT = os.path.join(BD, "analysis", "ext1028"); os.makedirs(OUT, exist_ok=True)


def chain_jobs(C, starts):
    seq = M.start_order(starts)
    r = C.decode(seq, lambda j, t: max(0, starts[j] - t), record_tmak=True)
    if r is None:
        return None
    st, ms, cost, tmak = r
    fin = st + C.dur
    est = np.array([max((fin[p] for p in C.preds[j]), default=0) for j in range(C.n)])
    vac = np.all(C.cap_t == 0, axis=0)  # 全資源の容量が0の時刻
    j = int(np.argmax(fin)); seen = set(); out = []
    while j not in seen:
        seen.add(j)
        dl = int(st[j] - tmak[j])
        if C.dur[j] > 0:
            a, b = int(tmak[j]), int(st[j])
            out.append(dict(job=j, dur=int(C.dur[j]), delay=dl, jump=int(vac[a:b].sum()) if b > a else 0,
                            save=float(C.jobcost[j][a] - C.jobcost[j][b]), rwait=int(tmak[j] - est[j]),
                            rwait_vac=int(vac[int(est[j]):a].sum()) if a > est[j] else 0))
        t0 = int(tmak[j])
        if tmak[j] > est[j]:
            cand = [k for k in range(C.n) if fin[k] == t0 and k != j and C.dur[k] > 0]
            if not cand:
                t0 = int(est[j]); cand = [p for p in C.preds[j] if fin[p] == t0]
        else:
            cand = [p for p in C.preds[j] if fin[p] == t0]
        if t0 == 0 or not cand:
            break
        j = max(cand, key=lambda k: (C.dur[k] > 0, st[k]))
    return ms, out


rows = []
for x in range(1, 49):
    p = f"j30{x}_1"
    for c in ["RR000_RV1", "RR025_RV1", "RR050_RV1", "RR000_RV0", "RR025_RV0", "RR050_RV0"]:
        fm = os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt")
        fn = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur_{TAG}.txt")
        if not (os.path.exists(fm) and os.path.exists(fn)):
            continue
        Cm, Cn = (M.Case(f, os.path.join(BD, "j30.sm", p + ".sm"), os.path.join(BD, "costs_ext", f"costs_{p}.csv")) for f in (fm, fn))
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
            for who, C, st in [("MIP", Cm, st_m), ("NSGA", Cn, st_n)]:
                r = chain_jobs(C, st)
                if r is None:
                    continue
                ms, js = r
                for q in js:
                    rows.append(dict(key=f"{p}|{c}|{ms_m:.0f}", rv=c[-3:], pos=round(pos, 3), who=who, ms=ms, **q))
    print(p, len(rows), flush=True)
with open(os.path.join(OUT, "rv1_chainjobs.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
print("saved", len(rows))
