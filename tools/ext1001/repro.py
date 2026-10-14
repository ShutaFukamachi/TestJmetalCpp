"""
repro.py -- 指摘5「再現性で SchedObj と MaxShift を比較する」

MIP 厳密解（拡張コスト表）を正解とし、各活動について「その活動を MIP と同じ時刻 s* に置けるか」を
3エンコーディングそれぞれのデコーダ規則で厳密に判定する（msd_decoder で MIP と同じ配置を
強制しながら順に進めるので、資源の使用状況は MIP 解と一致する）。

共通の量:
  t_mak   : EST 以降で最初に配置可能な時刻
  w_need  : s* - t_mak
  rank1   : [t_mak, s*) に cost(s*) 以下の実行可能スロットが無い（strict '<' の最早優先で s* が選ばれる条件）
  W_up    : s* より後で cost(s*) より真に安い最初の実行可能スロットの直前まで（窓の右端の許容上限）
  -> 窓幅 w で s* に置ける  <=>  rank1 かつ w_need <= w <= W_up

エンコーディングごとの判定:
  MaxShiftDur : 窓幅は遺伝子（rho, 1000刻み）で 0..round(0.5T) の任意の整数 -> rank1 かつ w_need <= round(0.5T)
  MaxShift    : 窓幅は遺伝子（整数）0..T//2（strategy4 の上限）          -> rank1 かつ w_need <= T//2
  SchedObj    : 遺伝子は {0:最早, 1:窓内最安} の2値。窓幅は遺伝子ではなく評価ごとの乱数
                - w_need == 0 なら遺伝子0で確実に再現（決定的）
                - w_need > 0 なら遺伝子1 かつ 乱数の窓幅 m が [w_need, W_up] に入ったときだけ再現（運次第）
                  確率は strategy2（各活動独立に一様 [1, T/2]）で p = |[w_need, min(W_up,T/2)]| / (T/2)
スケジュール全体の再現: 全活動が再現できること。SchedObj は log10(Π p) も出す。
"""
import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import csv, math, os, sys
import numpy as np
from msd_decoder import load, start_order

OUT = _os.path.join(ROOT, "cmake-build-release", "analysis", "ext1001")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]


def analyse(cs, s):
    n, T = cs.n, cs.T
    usage = np.zeros((cs.R, T), dtype=np.int64); finish = np.zeros(n, dtype=np.int64)
    half = T // 2; effw = int(round(0.5 * T))
    jobs = []
    for j in start_order(s):
        d = int(cs.dur[j])
        est = max((finish[p] for p in cs.preds[j]), default=0)
        if d <= 0:
            finish[j] = est; continue
        ok = np.all(usage + cs.dem[j][:, None] <= cs.cap_t, axis=0)
        c = np.concatenate([[0], np.cumsum(~ok)])
        tt = np.arange(T - d + 1); can = (c[tt + d] - c[tt]) == 0
        t_mak = est + int(np.nonzero(can[est:])[0][0])
        tgt = s[j]; w = tgt - t_mak
        cst = cs.jobcost[j]; c0 = cst[tgt]
        before = np.nonzero(can[t_mak:tgt])[0] + t_mak
        rank1 = not np.any(cst[before] <= c0 + 1e-9)
        after = np.nonzero(can[tgt + 1:])[0] + tgt + 1
        cheaper = after[cst[after] < c0 - 1e-9]
        W_up = (int(cheaper[0]) - 1 - t_mak) if len(cheaper) else (T - d - t_mak)
        msd = rank1 and w <= effw
        ms = rank1 and w <= half
        if w == 0:
            so_det, p = True, 1.0
        else:
            so_det = False
            lo, hi = max(1, w), min(W_up, half)
            p = (hi - lo + 1) / half if (rank1 and hi >= lo) else 0.0
        jobs.append(dict(job=j, w_need=w, rank1=rank1, W_up=W_up, msd=msd, ms=ms, so_det=so_det, so_p=p))
        usage[:, tgt:tgt + d] += cs.dem[j][:, None]; finish[j] = tgt + d
    return jobs


def run(prefixes):
    rows_s, rows_j = [], []
    for prefix in prefixes:
        for cond in CONDS:
            try:
                cs = load(prefix, cond)
            except (FileNotFoundError, IndexError, ValueError):
                continue
            for k, (msv, cost, s) in enumerate(cs.sols):
                J = analyse(cs, s)
                logp = sum(math.log10(x["so_p"]) if x["so_p"] > 0 else -math.inf for x in J)
                rows_s.append(dict(instance=prefix, cond=cond, k=k, ms=msv, cost=cost, n_jobs=len(J),
                                   msd_all=int(all(x["msd"] for x in J)), ms_all=int(all(x["ms"] for x in J)),
                                   so_det_all=int(all(x["so_det"] for x in J)),
                                   so_log10p=logp, n_w0=sum(1 for x in J if x["w_need"] == 0),
                                   n_rank1=sum(1 for x in J if x["rank1"])))
                for x in J:
                    rows_j.append(dict(instance=prefix, cond=cond, k=k, **x))
    for name, rows in [("repro_sched.csv", rows_s), ("repro_jobs.csv", rows_j)]:
        with open(os.path.join(OUT, name), "w", newline="", encoding="utf-8") as fp:
            w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    return rows_s, rows_j


if __name__ == "__main__":
    base = _os.path.join(ROOT, "cmake-build-release", "results", "EXT_COST", "FUN_MIP")
    pre = sorted(os.listdir(base))
    S, J = run(pre)
    print("instances:", len(pre), "schedules:", len(S), "jobs:", len(J))
    f = lambda key, R: sum(r[key] for r in R) / len(R)
    print("job-level  : MaxShiftDur %.4f  MaxShift %.4f  SchedObj(det) %.4f  SchedObj(p>0) %.4f  rank1 %.4f  w_need=0 %.4f" % (
        f("msd", J), f("ms", J), f("so_det", J), sum(1 for x in J if x["so_p"] > 0) / len(J),
        f("rank1", J), sum(1 for x in J if x["w_need"] == 0) / len(J)))
    print("sched-level: MaxShiftDur %.4f  MaxShift %.4f  SchedObj(det) %.4f" % (
        f("msd_all", S), f("ms_all", S), f("so_det_all", S)))
    lp = sorted(r["so_log10p"] for r in S)
    print("SchedObj log10 P(whole schedule by luck): median %.1f  p10 %.1f  p90 %.1f" % (
        lp[len(lp) // 2], lp[len(lp) // 10], lp[9 * len(lp) // 10]))
