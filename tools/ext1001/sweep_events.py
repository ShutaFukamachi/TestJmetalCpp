"""
sweep_events.py -- 指摘5「max_shift を変えると急にメイクスパンが増えることがある。一番後ろの作業が動くからでは？」

MIP 解を再現する窓幅を基準に、1活動 jj の窓幅だけを 0..上限 で動かし、配置が変わった瞬間（イベント）ごとに記録する:
  dms            : メイクスパンの変化
  dcost_total    : 全体コストの変化
  dcost_own      : 動かした活動 jj 自身のコストの変化
  dcost_others   : 他の活動のコストの変化（= total - own）。連鎖で押し出された活動の分
  n_moved_others : 開始時刻が変わった他の活動の数
  own_is_last    : 変化後、jj 自身がいちばん遅く終わる活動か（「一番後ろの作業が動いた」）
  last_job_changed: いちばん遅く終わる活動が入れ替わったか
出力: analysis/ext1001/sweep_events.csv
"""
import os, sys, csv
import numpy as np
from msd_decoder import load, start_order, BD

CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
insts = sys.argv[1].split(",") if len(sys.argv) > 1 else ["j309_1", "j3011_1", "j3026_1", "j3017_1"]
out = os.path.join(BD, "analysis", "ext1001", "sweep_events.csv")
fp = open(out, "w", newline="", encoding="utf-8"); W = None


def jobcosts(cs, st):
    return np.array([cs.jobcost[j, st[j]] if cs.dur[j] > 0 else 0.0 for j in range(cs.n)])


for prefix in insts:
    for cond in CONDS:
        try:
            cs = load(prefix, cond)
        except (FileNotFoundError, IndexError, ValueError):
            continue
        sols = sorted(cs.sols, key=lambda x: x[0])
        if not sols:
            continue  # MIP 前線が空の条件（解けなかった条件）
        Wmax = int(round(0.5 * cs.T))
        for pos_name, (ms0, c0, s) in [("ms_end", sols[0]), ("middle", sols[len(sols) // 2]), ("cost_end", sols[-1])]:
            seq = start_order(s)
            base = cs.decode(seq, lambda j, t, s=s: s[j] - t, record_tmak=True)
            wneed = {j: s[j] - base[3][j] for j in range(cs.n)}
            for jj in [j for j in range(cs.n) if cs.dur[j] > 0]:
                prev = None
                for w in range(Wmax + 1):
                    r = cs.decode(seq, lambda j, t, jj=jj, w=w: (w if j == jj else wneed[j]))
                    if r is None:
                        prev = None; continue
                    st, ms, tc = r
                    if prev is not None and not np.array_equal(st, prev[0]):
                        pst, pms, ptc = prev
                        jc, pjc = jobcosts(cs, st), jobcosts(cs, pst)
                        fin = st + cs.dur; pfin = pst + cs.dur
                        last = int(np.argmax(np.where(cs.dur > 0, fin, -1))); plast = int(np.argmax(np.where(cs.dur > 0, pfin, -1)))
                        row = dict(instance=prefix, cond=cond, base=pos_name, job=jj, d=int(cs.dur[jj]), w=w,
                                   dms=int(ms - pms), dcost_total=round(float(tc - ptc), 3),
                                   dcost_own=round(float(jc[jj] - pjc[jj]), 3),
                                   dcost_others=round(float((tc - ptc) - (jc[jj] - pjc[jj])), 3),
                                   n_moved_others=int(sum(1 for j in range(cs.n) if j != jj and st[j] != pst[j])),
                                   own_shift=int(st[jj] - pst[jj]),
                                   own_is_last=int(fin[jj] == ms), last_job_changed=int(last != plast))
                        if W is None:
                            W = csv.DictWriter(fp, fieldnames=list(row.keys())); W.writeheader()
                        W.writerow(row)
                    prev = (st, ms, tc)
            fp.flush()
        print(prefix, cond, "done", flush=True)
fp.close()
