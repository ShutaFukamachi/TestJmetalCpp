"""
repro_cases.py -- 指摘2・4「ソルバー解を再現できない具体的なケースはどれか」を1件ずつ書き出す。

repro.py と同じ判定（MIP と同じ配置を強制しながら順に進める）で、MaxShiftDur が再現できない活動について:
  原因A（窓幅の上限不足）: 必要な窓幅 w_need が上限 round(0.5T) を超える → 何スロット足りないか
  原因B（手前に同じか安い時刻）: [t_mak, s*) に cost(s*) 以下の実行可能時刻がある
      → その最安時刻と、s* とのコスト差（0 なら同コスト、正なら MIP が「あえて高い所」に置いた）
前線上の位置（0=工期最小端, 1=コスト最小端）も記録する。

出力: analysis/ext1001/repro_cases_jobs.csv（1行=再現できない活動）
      analysis/ext1001/repro_cases_sched.csv（1行=MIP解。再現できない活動数と原因）
"""
import os, csv
import numpy as np
from msd_decoder import load, start_order, BD

OUT = os.path.join(BD, "analysis", "ext1001")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]


def analyse(cs, s):
    n, T = cs.n, cs.T
    usage = np.zeros((cs.R, T), dtype=np.int64); finish = np.zeros(n, dtype=np.int64)
    effw = int(round(0.5 * T)); bad = []
    for j in start_order(s):
        d = int(cs.dur[j])
        est = max((finish[p] for p in cs.preds[j]), default=0)
        if d <= 0:
            finish[j] = est; continue
        ok = np.all(usage + cs.dem[j][:, None] <= cs.cap_t, axis=0)
        c = np.concatenate([[0], np.cumsum(~ok)])
        tt = np.arange(T - d + 1); can = (c[tt + d] - c[tt]) == 0
        t_mak = est + int(np.nonzero(can[est:])[0][0])
        tgt = s[j]; w = tgt - t_mak; cst = cs.jobcost[j]; c0 = cst[tgt]
        before = np.nonzero(can[t_mak:tgt])[0] + t_mak
        cheaper = before[cst[before] <= c0 + 1e-9] if len(before) else before
        if len(cheaper):
            tb = int(cheaper[np.argmin(cst[cheaper])])
            bad.append(dict(job=j, d=d, cause="B_cheaper_earlier", t_mak=t_mak, s_star=tgt, w_need=w, w_cap=effw,
                            short_by="", alt_t=tb, cost_s_star=round(float(c0), 3),
                            cost_diff=round(float(c0 - cst[tb]), 3)))
        elif w > effw:
            bad.append(dict(job=j, d=d, cause="A_window_cap", t_mak=t_mak, s_star=tgt, w_need=w, w_cap=effw,
                            short_by=w - effw, alt_t="", cost_s_star=round(float(c0), 3), cost_diff=""))
        usage[:, tgt:tgt + d] += cs.dem[j][:, None]; finish[j] = tgt + d
    return bad


def main():
    base = os.path.join(BD, "results", "EXT_COST", "FUN_MIP")
    jobs_rows, sched_rows = [], []
    for prefix in sorted(os.listdir(base)):
        for cond in CONDS:
            try:
                cs = load(prefix, cond)
            except (FileNotFoundError, IndexError, ValueError):
                continue
            sols = sorted(cs.sols, key=lambda x: x[0]); K = len(sols)
            for k, (ms, cost, s) in enumerate(sols):
                pos = k / (K - 1) if K > 1 else 0.0
                bad = analyse(cs, s)
                nA = sum(1 for b in bad if b["cause"] == "A_window_cap"); nB = len(bad) - nA
                sched_rows.append(dict(instance=prefix, cond=cond, front_pos=round(pos, 3), ms=ms, cost=cost,
                                       T=cs.T, n_bad=len(bad), n_window_cap=nA, n_cheaper_earlier=nB,
                                       reproducible=int(not bad), reproducible_if_no_cap=int(nB == 0)))
                for b in bad:
                    jobs_rows.append(dict(instance=prefix, cond=cond, front_pos=round(pos, 3), ms=ms, cost=cost, **b))
    for name, rows in [("repro_cases_jobs.csv", jobs_rows), ("repro_cases_sched.csv", sched_rows)]:
        with open(os.path.join(OUT, name), "w", newline="", encoding="utf-8") as fp:
            w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    print(f"schedules {len(sched_rows)}, non-reproducible jobs {len(jobs_rows)}")


if __name__ == "__main__":
    main()
