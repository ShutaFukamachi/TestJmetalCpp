"""
mid_chain_diag.py -- 前線中間部の工期を「最後の作業から逆にたどった鎖」に沿って分解する（mid_delay_diag.py の続き）
工期 = 鎖上の作業の所要時間 + 鎖上の遅らせ（開始 − t_mak）+ 鎖上の資源待ち（t_mak − 最早開始）+ 先行待ちの隙間
鎖のたどり方: 作業 j の開始の直前要因を
  遅らせ（delay>0）なら j 自身の遅らせ分を数えて、t_mak の要因へ
  t_mak > 最早開始（資源待ち）なら、t_mak に終了した作業（同じ資源を使うもの優先）へ
  それ以外は最早開始を決めた先行作業（終了 = 最早開始）へ
MIP の中間点と、同じコスト以下で工期最小の NSGA 点（mid_delay_diag.py と同じ対応）で比べる。
出力: analysis/ext1024/mid_chain.csv
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


def case(path, p):
    return M.Case(path, os.path.join(BD, "j30.sm", p + ".sm"), os.path.join(BD, "costs_ext", f"costs_{p}.csv"))


def chain(C, starts):
    seq = M.start_order(starts)
    r = C.decode(seq, lambda j, t: max(0, starts[j] - t), record_tmak=True)
    if r is None:
        return None
    st, ms, cost, tmak = r
    fin = st + C.dur
    est = np.array([max((fin[p] for p in C.preds[j]), default=0) for j in range(C.n)])
    j = int(np.argmax(fin)); parts = dict(dur=0, delay=0, rwait=0, gap=0, n_chain=0, n_delayed=0)
    onchain = []
    t = int(fin[j]); seen = set()
    while True:
        if j in seen:
            break
        seen.add(j); onchain.append(j)
        parts["dur"] += int(C.dur[j]); parts["n_chain"] += 1
        dl = int(st[j] - tmak[j]); rw = int(tmak[j] - est[j])
        if dl > 0:
            parts["delay"] += dl; parts["n_delayed"] += 1
        t0 = int(tmak[j])
        if rw > 0:
            parts["rwait"] += 0  # 資源待ちは、t_mak に終わった作業へ鎖をつなぐので隙間としては数えない
            cand = [k for k in range(C.n) if fin[k] == t0 and k != j and C.dur[k] > 0]
            if not cand:
                parts["rwait"] += rw
                t0 = int(est[j])
                cand = [p for p in C.preds[j] if fin[p] == t0]
            else:
                share = [k for k in cand if np.any((C.dem[k] > 0) & (C.dem[j] > 0))]
                cand = share or cand
        else:
            cand = [p for p in C.preds[j] if fin[p] == t0]
        if t0 == 0 or not cand:
            parts["gap"] += t0
            break
        j = max(cand, key=lambda k: (C.dur[k] > 0, st[k]))
    parts["ms"] = int(ms)
    parts["check"] = parts["dur"] + parts["delay"] + parts["rwait"] + parts["gap"] - int(ms)
    # 遅らせの効率（1コマあたりの節約）を鎖の上と外で
    on = set(onchain); eff_on, eff_off, dl_off = [], [], 0
    for k in range(C.n):
        dl = int(st[k] - tmak[k])
        if dl <= 0 or C.dur[k] <= 0:
            continue
        e = float(C.jobcost[k][tmak[k]] - C.jobcost[k][st[k]]) / dl
        (eff_on if k in on else eff_off).append(e)
        if k not in on:
            dl_off += dl
    parts["save_on"] = round(sum(float(C.jobcost[k][tmak[k]] - C.jobcost[k][st[k]]) for k in on if C.dur[k] > 0), 2)
    parts["save_all"] = round(sum(float(C.jobcost[k][tmak[k]] - C.jobcost[k][st[k]]) for k in range(C.n) if C.dur[k] > 0), 2)
    parts["eff_on_med"] = round(float(np.median(eff_on)), 2) if eff_on else ""
    parts["eff_on_min"] = round(float(np.min(eff_on)), 2) if eff_on else ""
    parts["delay_off"] = dl_off
    return parts


rows = []
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
            A = chain(Cm, st_m); B = chain(Cn, st_n)
            if A is None or B is None:
                continue
            for who, R in [("MIP", A), ("NSGA", B)]:
                rows.append(dict(key=f"{p}|{c}|{ms_m:.0f}", inst=p, cond=c, pos=round(pos, 3), who=who, **R))
    print(p, len(rows), flush=True)
with open(os.path.join(BD, "analysis", "ext1024", "mid_chain.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
print("saved", len(rows))
