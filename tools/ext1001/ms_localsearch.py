"""
ms_localsearch.py -- 指摘1の検証用プロトタイプ: 作業リストの局所探索でメイクスパン最小端に届くか。

窓幅はすべて 0（最早配置＝MaxShiftDur で rho=0）として Serial SGS で復号し、
作業リストだけを「挿入近傍」（1作業を別の位置へ移す。先行関係を壊す移動は除外）で改善する。
評価は (メイクスパン, 全作業の終了時刻の和) の辞書式（同じメイクスパンでも前進の手がかりを得るため）。
局所最適に達したら、ランダムに数回の挿入で揺さぶってから再開する（反復局所探索）。

使い方: python ms_localsearch.py j309_1 RR050_RV0 [start=a2|random] [iters=30]
"""
import os, sys, random, time
import numpy as np
from msd_decoder import load, start_order, BD

prefix, cond = sys.argv[1], sys.argv[2]
start = sys.argv[3] if len(sys.argv) > 3 else "a2"
iters = int(sys.argv[4]) if len(sys.argv) > 4 else 30
random.seed(1)
cs = load(prefix, cond)
mip_min = min(s[0] for s in cs.sols)
ZERO = np.zeros(cs.n, dtype=int)
pos_pred = cs.preds


def score(seq):
    r = cs.decode(seq, ZERO, record_tmak=False)
    if r is None:
        return (10 ** 9, 10 ** 9)
    st, ms, _ = r
    fin = st + cs.dur
    return (ms, int(fin.sum()))


def feasible_insert(seq, i, k):
    """seq[i] を位置 k に移しても先行関係（先行→後続の順）を保てるか"""
    j = seq[i]
    if k < i:
        between = seq[k:i]
        return not any(p in between for p in cs.preds[j])
    else:
        between = seq[i + 1:k + 1]
        return not any(s in between for s in cs.succ[j])


def move(seq, i, k):
    s = seq[:i] + seq[i + 1:]
    s.insert(k, seq[i])
    return s


def local_search(seq):
    cur = score(seq)
    improved = True
    while improved:
        improved = False
        n = len(seq)
        for i in range(1, n - 1):
            for k in range(1, n - 1):
                if k == i or not feasible_insert(seq, i, k):
                    continue
                cand = move(seq, i, k)
                sc = score(cand)
                if sc < cur:
                    seq, cur, improved = cand, sc, True
                    break
            if improved:
                break
    return seq, cur


def a2_start():
    p = os.path.join(BD, "results", "FUN_A2", prefix, f"PERM_A2_{prefix}_{cond}.txt")
    best = None
    for l in open(p):
        v = l.split()
        if len(v) == cs.n + 2:
            ms = float(v[0]); perm = list(map(int, v[2:]))
            if best is None or ms < best[0]:
                best = (ms, perm)
    return best[1]


def random_start():
    indeg = [len(p) for p in cs.preds]; ready = [j for j in range(cs.n) if indeg[j] == 0]; order = []
    while ready:
        j = ready.pop(random.randrange(len(ready))); order.append(j)
        for k in cs.succ[j]:
            indeg[k] -= 1
            if indeg[k] == 0:
                ready.append(k)
    return order


t0 = time.time()
seq = a2_start() if start == "a2" else random_start()
print(f"{prefix} {cond}: MIP min makespan {mip_min:.0f}; start ({start}) makespan {score(seq)[0]}")
best_seq, best = local_search(seq)
print(f"  after local search: {best[0]}  ({time.time()-t0:.0f}s)")
for it in range(iters):
    s = best_seq[:]
    for _ in range(3):  # 揺さぶり
        i, k = random.randrange(1, cs.n - 1), random.randrange(1, cs.n - 1)
        if i != k and feasible_insert(s, i, k):
            s = move(s, i, k)
    s, sc = local_search(s)
    if sc < best:
        best_seq, best = s, sc
        print(f"  iter {it}: improved to {best[0]}  ({time.time()-t0:.0f}s)")
    if best[0] <= mip_min:
        break
print(f"RESULT {prefix} {cond} start={start}: best makespan {best[0]} vs MIP {mip_min:.0f}  ({time.time()-t0:.0f}s)")
