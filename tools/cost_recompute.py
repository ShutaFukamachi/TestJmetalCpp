#!/usr/bin/env python3
"""
cost_recompute.py — SCHED ファイルの記録コストを、指定コスト表で再計算して照合する。

SCHED 形式:
  line1: n nRes
  line2: durations (n)
  next n lines: demand rows (n x nRes)
  remaining lines: "<makespan> <cost> <start_0> ... <start_{n-1}>"  (各パレート解)

コスト表 CSV 形式:
  line1: R T
  next R lines: T 個のコスト値

再計算コスト = Σ_{j: dur>0} Σ_{tau=start_j..start_j+dur_j-1} Σ_k cost[k][min(tau,T-1)] * demand[j][k]
  (t>=T は T-1 にクランプ = resourceCost の挙動に一致)

使い方:
  python cost_recompute.py --sched <SCHED.txt> --cost <costs.csv> [--tol 1.0]
戻り値: 一致件数/総件数 を表示、全一致なら exit 0、不一致あれば exit 1。
"""
import argparse, sys


def read_sched(path):
    with open(path) as f:
        toks = f.read().split('\n')
    # 行単位で読む
    lines = [ln for ln in toks]
    idx = 0
    hdr = lines[idx].split(); idx += 1
    n, nRes = int(hdr[0]), int(hdr[1])
    dur = list(map(int, lines[idx].split())); idx += 1
    demand = []
    for _ in range(n):
        demand.append(list(map(int, lines[idx].split()))); idx += 1
    sols = []
    for ln in lines[idx:]:
        p = ln.split()
        if len(p) < 2 + n:
            continue
        ms = float(p[0]); cost = float(p[1])
        starts = list(map(int, p[2:2 + n]))
        sols.append((ms, cost, starts))
    return n, nRes, dur, demand, sols


def read_cost_csv(path):
    with open(path) as f:
        hdr = f.readline().split()
        R, T = int(hdr[0]), int(hdr[1])
        table = []
        for _ in range(R):
            row = list(map(float, f.readline().split()))
            table.append(row)
    return R, T, table


def recompute(starts, dur, demand, nRes, table, T):
    total = 0.0
    n = len(dur)
    for j in range(n):
        d = dur[j]
        if d <= 0:
            continue
        s = starts[j]
        for tau in range(s, s + d):
            tt = tau if tau < T else T - 1
            if tt < 0:
                tt = 0
            for k in range(nRes):
                total += table[k][tt] * demand[j][k]
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sched', required=True)
    ap.add_argument('--cost', required=True)
    ap.add_argument('--tol', type=float, default=1.0)
    args = ap.parse_args()

    n, nRes, dur, demand, sols = read_sched(args.sched)
    R, T, table = read_cost_csv(args.cost)

    match = 0
    worst = 0.0
    firstbad = None
    for i, (ms, cost, starts) in enumerate(sols):
        rc = recompute(starts, dur, demand, nRes, table, T)
        diff = abs(rc - cost)
        if diff < args.tol:
            match += 1
        else:
            worst = max(worst, diff)
            if firstbad is None:
                firstbad = (i, cost, rc, diff)
    print(f"sched={args.sched}")
    print(f"cost ={args.cost}  (R={R} T={T})")
    print(f"match {match}/{len(sols)}  worst_diff={worst:.1f}")
    if firstbad is not None:
        i, c, rc, d = firstbad
        print(f"  first mismatch: sol#{i} recorded={c:.1f} recompute={rc:.1f} diff={d:.1f}")
    sys.exit(0 if match == len(sols) else 1)


if __name__ == '__main__':
    main()
