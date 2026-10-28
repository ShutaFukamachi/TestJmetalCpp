"""
mid_diagnose.py -- 前線の中間部の差は「作業リスト」と「窓幅（max_shift）」のどちらの問題か（10/1 指摘6 への入口）

仮説
  H-W: 中間部の差は窓幅の決め方の問題。NSGA の作業リストのまま窓幅だけ最適化すれば MIP に近づく。
  H-L: 作業リストの問題。窓幅を最適化しても縮まらない（MIP の作業リストを使えば縮む）。
方法（MIP 前線の中間の点 = 前線位置 0.2〜0.8 の各点、工期の上限 = その MIP 点の工期）
  1. NSGA の実際: 改善版（isrgp r1）の前線で、工期 <= 上限 の最小コスト
  2. NSGA の作業リスト＋窓幅だけ最適化: 改善版の前線の各点（工期が上限以下の点のうち上限に近い5点）の
     作業リストを固定し、窓幅を座標降下法で最適化（工期 <= 上限 のもとでコスト最小）
  3. 対照: MIP の解の作業リスト＋同じ窓幅最適化（最適化手順そのものの力の確認）
  窓幅の候補は、各作業で「窓に初めてより安い時刻が入る点」（配置が変わる点）だけ。上限は本番と同じ T/2。
出力: analysis/ext1014/mid_diagnose.csv と要約
使い方: python mid_diagnose.py [インスタンス数=8]
"""
import os, sys, csv, statistics as S, time
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "ext1001")); sys.path.insert(0, os.path.join(HERE, ".."))
from msd_decoder import Case, BD, start_order  # noqa: E402
import gap48_analysis as G  # noqa: E402

NI = int(sys.argv[1]) if len(sys.argv) > 1 else 8
TAG = "_t1016_isrgp_r1"
OUT = os.path.join(BD, "analysis", "ext1014")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]


def decode_full(cs, seq, win):
    """win: 作業ごとの窓幅（スロット）。返り値 (start, ms, cost, tmak)"""
    return cs.decode(seq, win, record_tmak=True)


def change_points(cs, seq, win, j, cap):
    """作業 j の配置が変わる窓幅の候補（現在のスケジュールの手前までの配置は固定とみなして計算）"""
    # j より前の作業だけを置いた状態の資源使用量を作る
    n, R, T = cs.n, cs.R, cs.T
    usage = np.zeros((R, T), dtype=np.int64); finish = np.zeros(n, dtype=np.int64)
    res = cs.decode(seq[:seq.index(j)], win)
    if res is None:
        return [0]
    st = res[0]
    for k in seq[:seq.index(j)]:
        d = int(cs.dur[k]); finish[k] = st[k] + d
        if d > 0:
            usage[:, st[k]:st[k] + d] += cs.dem[k][:, None]
    d = int(cs.dur[j])
    if d <= 0:
        return [0]
    est = max((finish[p] for p in cs.preds[j]), default=0)
    ok = np.all(usage + cs.dem[j][:, None] <= cs.cap_t, axis=0)
    c = np.concatenate([[0], np.cumsum(~ok)])
    tt = np.arange(T - d + 1); can = (c[tt + d] - c[tt]) == 0
    cand = np.nonzero(can[est:])[0]
    if len(cand) == 0:
        return [0]
    tmak = est + int(cand[0])
    pts = [0]; best = cs.jobcost[j, tmak]
    for t in range(tmak + 1, min(T - d, tmak + cap) + 1):
        if can[t] and cs.jobcost[j, t] < best:
            best = cs.jobcost[j, t]; pts.append(t - tmak)
    return pts


def optimize_windows(cs, seq, ms_cap, win0=None, passes=3):
    cap = int(round(0.5 * cs.T))
    win = np.zeros(cs.n, dtype=int) if win0 is None else np.minimum(np.array(win0, dtype=int), cap)
    r = cs.decode(seq, win)
    if r is None or r[1] > ms_cap:
        win = np.zeros(cs.n, dtype=int); r = cs.decode(seq, win)
        if r is None or r[1] > ms_cap:
            return None
    best = r[2]
    for _ in range(passes):
        improved = False
        for j in seq:
            if cs.dur[j] <= 0:
                continue
            for w in change_points(cs, seq, win, j, cap):
                if w == win[j]:
                    continue
                old = win[j]; win[j] = w
                rr = cs.decode(seq, win)
                if rr is not None and rr[1] <= ms_cap and rr[2] < best - 1e-9:
                    best = rr[2]; improved = True
                else:
                    win[j] = old
        if not improved:
            break
    return best


def windows_from_sched(cs, starts):
    """スケジュール（開始時刻）から 作業リスト = 開始順、窓幅 = 開始 - t_mak を作る（再現の表現）"""
    seq = start_order(starts)
    w = np.zeros(cs.n, dtype=int)
    for _ in range(2):   # t_mak は前の作業の配置に依存するので2回まわす
        r = cs.decode(seq, w, record_tmak=True)
        if r is None:
            break
        w = np.maximum(0, np.array(starts) - r[3])
    return seq, w


# 中間部の差が大きい順にインスタンスを選ぶ（改善版 r1 の中間部の工期差）
rows_rep = list(csv.DictReader(open(os.path.join(OUT, "repeats_j30_rows.csv"))))
score = {}
for r in rows_rep:
    if r["isrgp_r1_mid"]:
        score.setdefault(r["instance"], []).append(float(r["isrgp_r1_mid"]))
insts = [p for p, _ in sorted(score.items(), key=lambda kv: -S.mean(kv[1]))][:NI]
print("instances:", insts, flush=True)

out = []; t0 = time.time()
for p in insts:
    for c in CONDS:
        mp = os.path.join(BD, "results", "EXT_COST", "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt")
        npth = os.path.join(BD, "results", "EXT_COST", "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur{TAG}.txt")
        if not (os.path.exists(mp) and os.path.exists(npth)):
            continue
        sm = os.path.join(BD, "j30.sm", p + ".sm"); cost = os.path.join(BD, "costs_ext", f"costs_{p}.csv")
        mcs = Case(mp, sm, cost); ncs = Case(npth, sm, cost)
        mf = G.pareto_front([(s[0], s[1]) for s in mcs.sols if s[0] < 1e8])
        if len(mf) < 3:
            continue
        lo, hi = mf[0][0], mf[-1][0]
        if hi <= lo:
            continue
        nsols = sorted([s for s in ncs.sols if s[0] < 1e8], key=lambda s: s[0])
        nf = G.pareto_front([(s[0], s[1]) for s in nsols])
        for ms, mc in mf:
            pos = (ms - lo) / (hi - lo)
            if not (0.2 <= pos <= 0.8):
                continue
            nact = G.min_cost_at_or_before(nf, ms)
            if nact is None:
                continue
            # NSGA の作業リスト（工期が上限以下の点のうち上限に近い5点）
            cands = [s for s in nsols if s[0] <= ms][-5:]
            ls_best = None
            for s in cands:
                seq, w = windows_from_sched(ncs, s[2])
                b = optimize_windows(ncs, seq, ms, w)
                if b is not None and (ls_best is None or b < ls_best):
                    ls_best = b
            # 対照: MIP の解そのものの作業リスト
            msol = min([s for s in mcs.sols if s[0] == ms], key=lambda s: s[1])
            seq, w = windows_from_sched(mcs, msol[2])
            ctrl = optimize_windows(mcs, seq, ms, None)
            out.append(dict(instance=p, cond=c, pos=round(pos, 3), mip_ms=ms, mip_cost=mc, nsga_cost=nact,
                            nsga_list_winopt=ls_best, mip_list_winopt=ctrl))
        print(f"{p} {c} done ({time.time()-t0:.0f}s, points {len(out)})", flush=True)

with open(os.path.join(OUT, "mid_diagnose.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.DictWriter(fp, fieldnames=list(out[0].keys())); w.writeheader(); w.writerows(out)

ok = [r for r in out if r["nsga_list_winopt"] is not None and r["nsga_cost"] > r["mip_cost"] + 1e-6]
gap_n = [(r["nsga_cost"] - r["mip_cost"]) / r["mip_cost"] for r in ok]
gap_ls = [(min(r["nsga_list_winopt"], r["nsga_cost"]) - r["mip_cost"]) / r["mip_cost"] for r in ok]
closed = [(r["nsga_cost"] - min(r["nsga_list_winopt"], r["nsga_cost"])) / (r["nsga_cost"] - r["mip_cost"]) for r in ok]
ctrl = [(r["mip_list_winopt"] - r["mip_cost"]) / r["mip_cost"] for r in out if r["mip_list_winopt"] is not None]
print(f"中間の点 {len(out)}（NSGA が MIP より高い点 {len(ok)}）")
print(f"同じ工期でのコスト差（MIP 比）: NSGA の実際 {S.mean(gap_n):.2%} → NSGA の作業リストで窓幅だけ最適化 {S.mean(gap_ls):.2%}"
      f"（差の {S.mean(closed):.0%} が縮む、中央値 {S.median(closed):.0%}）")
print(f"対照（MIP の作業リストで窓幅だけ最適化）: MIP 比 {S.mean(ctrl):+.2%}（0 に近いほど最適化手順が十分）")
