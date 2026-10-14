"""
sweep.py -- 指摘3「max_shift を少し変えてもスケジュールは変わらず、ある値で急に変わる。
どこでどれだけ変わるか。小さい方が良い時と大きい方が良い時があるのでは」への厳密計測。

活動リストを固定し、窓幅（max_shift 相当、スロット数）w を 0 から Wmax まで1刻みで動かして
実デコーダ（msd_decoder, C++ 実装と 133/133 一致を検証済み）で復号する。

E1 一様掃引 : 全活動の窓幅を同じ w にする（遺伝子 rho を全活動で同じ値にした場合に相当）
E2 単一掃引 : 基準窓（MIP 解を再現する w_needed）から、1活動だけ w を動かす

Wmax = round(0.5*T)（本番の beta=0.5 で rho=1 のときの窓幅 eff≈beta*T）
"""
import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import sys, csv, os
import numpy as np
from msd_decoder import load, start_order

OUT = _os.path.join(ROOT, "cmake-build-release", "analysis", "ext1001")
os.makedirs(OUT, exist_ok=True)
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]


def pick3(sols):
    """前線の 工期最小端・中間・コスト最小端 の3解"""
    s = sorted(sols, key=lambda x: x[0])
    idx = sorted(set([0, len(s) // 2, len(s) - 1]))
    return [(i, s[i]) for i in idx]


def e1_uniform(cs, seq, Wmax):
    real = [j for j in range(cs.n) if cs.dur[j] > 0]
    curve = []
    for w in range(Wmax + 1):
        win = np.zeros(cs.n, dtype=int); win[real] = w
        r = cs.decode(seq, win)
        curve.append((w, r[1], r[2], hash(r[0].tobytes())) if r else (w, None, None, None))
    return curve


def summarize(curve):
    pts = [c for c in curve if c[1] is not None]
    ch = [i for i in range(1, len(pts)) if pts[i][3] != pts[i - 1][3]]      # スケジュールが変わった w
    distinct = len(set(p[3] for p in pts))
    costs = [p[2] for p in pts]; mss = [p[1] for p in pts]
    up = sum(1 for i in ch if pts[i][2] > pts[i - 1][2] + 1e-6)             # w を増やしてコストが上がった変化
    both_worse = sum(1 for i in ch if pts[i][2] >= pts[i - 1][2] - 1e-6 and pts[i][1] >= pts[i - 1][1]
                     and (pts[i][2] > pts[i - 1][2] + 1e-6 or pts[i][1] > pts[i - 1][1]))  # 工期もコストも悪化
    down = sum(1 for i in ch if pts[i][2] < pts[i - 1][2] - 1e-6)
    # w の値どうしで非支配な (ms,cost) の数（>1 なら「小さい w が良い点」と「大きい w が良い点」が両立）
    uniq = sorted(set((p[1], round(p[2], 3)) for p in pts))
    nd, best = [], float("inf")
    for m, c in uniq:
        if c < best - 1e-9:
            nd.append((m, c)); best = c
    wmin_cost = pts[int(np.argmin(costs))][0]
    jumps = [abs(pts[i][2] - pts[i - 1][2]) / pts[i - 1][2] for i in ch]
    plateau = np.diff([0] + [pts[i][0] for i in ch] + [pts[-1][0] + 1])
    return dict(n_w=len(pts), n_change=len(ch), n_distinct=distinct,
                first_change_w=(pts[ch[0]][0] if ch else ""),
                max_plateau=int(plateau.max()) if len(plateau) else "",
                median_plateau=float(np.median(plateau)) if len(plateau) else "",
                cost_up_changes=up, cost_down_changes=down, both_worse_changes=both_worse,
                cost_w0=costs[0], cost_min=min(costs), cost_wmax=costs[-1], w_at_cost_min=wmin_cost,
                ms_w0=mss[0], ms_at_cost_min=pts[int(np.argmin(costs))][1], ms_wmax=mss[-1],
                n_nondominated_w=len(nd),
                max_jump_rel=max(jumps) if jumps else 0.0)


def e2_single(cs, s, Wmax, jobs=None):
    seq = start_order(s)
    base = cs.decode(seq, lambda j, t, s=s: s[j] - t, record_tmak=True)
    st0, ms0, c0, tm = base
    wneed = {j: s[j] - tm[j] for j in range(cs.n)}
    rows = []
    if jobs is None:
        jobs = [j for j in range(cs.n) if cs.dur[j] > 0]
    for jj in jobs:
        prev = None; nch = 0; up = down = 0; same_as_base = 0; ms_up = 0
        for w in range(Wmax + 1):
            r = cs.decode(seq, lambda j, t, jj=jj, w=w: (w if j == jj else wneed[j]))
            key = hash(r[0].tobytes()) if r else None
            if r is not None and np.array_equal(r[0], st0):
                same_as_base += 1
            if prev is not None and key != prev[0]:
                nch += 1
                if r is not None and prev[1] is not None:
                    if r[1] > prev[2]: ms_up += 1
                    if r[2] > prev[1] + 1e-6: up += 1
                    if r[2] < prev[1] - 1e-6: down += 1
            prev = (key, r[2] if r else None, r[1] if r else None)
        rows.append(dict(job=jj, n_infeasible=sum(1 for _ in [])+0, d=int(cs.dur[jj]), w_needed=int(wneed[jj]), n_change=nch, cost_up=up,
                         cost_down=down, ms_up=ms_up, frac_same_as_base=same_as_base / (Wmax + 1)))
    return rows


def main(prefixes, root_kind="MIP", method=None, detail=None):
    s1 = open(os.path.join(OUT, f"sweep_E1_{root_kind}.csv"), "a", newline="", encoding="utf-8")
    w1 = None
    for prefix in prefixes:
        for cond in CONDS:
            try:
                cs = load(prefix, cond, kind=root_kind, method=method)
            except (FileNotFoundError, IndexError, ValueError):
                continue
            if not cs.sols:
                continue
            Wmax = int(round(0.5 * cs.T))
            for pos, (ms, cost, s) in pick3(cs.sols):
                curve = e1_uniform(cs, start_order(s), Wmax)
                row = dict(instance=prefix, cond=cond, base=root_kind, front_pos=pos,
                           base_ms=ms, base_cost=cost, T=cs.T, Wmax=Wmax, **summarize(curve))
                if w1 is None:
                    w1 = csv.DictWriter(s1, fieldnames=list(row.keys()))
                    if s1.tell() == 0:
                        w1.writeheader()
                w1.writerow(row); s1.flush()
                if detail and (prefix, cond, pos) == detail:
                    with open(os.path.join(OUT, f"sweep_curve_{prefix}_{cond}_pos{pos}.csv"), "w",
                              newline="") as fp:
                        cw = csv.writer(fp); cw.writerow(["w", "makespan", "cost"])
                        for w, m, c, _ in curve:
                            cw.writerow([w, m, c])
            print(prefix, cond, "done", flush=True)
    s1.close()


if __name__ == "__main__":
    kind = sys.argv[1]            # MIP or ENC
    prefixes = sys.argv[2].split(",")
    main(prefixes, root_kind=kind, method=("MaxShiftDur" if kind == "ENC" else None))


def main_e2(prefixes, root_kind="MIP", method=None):
    path = os.path.join(OUT, f"sweep_E2_{root_kind}.csv")
    fp = open(path, "a", newline="", encoding="utf-8"); w2 = None
    for prefix in prefixes:
        for cond in CONDS:
            try:
                cs = load(prefix, cond, kind=root_kind, method=method)
            except (FileNotFoundError, IndexError, ValueError):
                continue
            if not cs.sols:
                continue
            Wmax = int(round(0.5 * cs.T))
            for pos, (ms, cost, s) in pick3(cs.sols):
                for r in e2_single(cs, s, Wmax):
                    row = dict(instance=prefix, cond=cond, base=root_kind, front_pos=pos, Wmax=Wmax, **r)
                    if w2 is None:
                        w2 = csv.DictWriter(fp, fieldnames=list(row.keys()))
                        if fp.tell() == 0:
                            w2.writeheader()
                    w2.writerow(row)
            fp.flush(); print(prefix, cond, "E2 done", flush=True)
    fp.close()
