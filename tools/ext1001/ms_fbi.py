"""
ms_fbi.py -- 指摘1の検証用プロトタイプ: 前向き・後ろ向き改善（FBI / double justification）でメイクスパンを縮められるか。

時間変動容量に対応した形:
  後ろ向き（右寄せ）: 終了の遅い順に、後続の開始時刻 - d 以下で、容量を満たす最も遅い時刻に置き直す
  前向き（左寄せ）  : 開始の早い順に、先行の終了時刻以降で、容量を満たす最も早い時刻に置き直す
  （= 前向きは MaxShiftDur で窓幅0の Serial SGS と同じ。得られた開始順が新しい作業リストになる）
改善が止まるまで繰り返す。開始点は A2（単目的GA）の最良順列とランダム順列。

使い方: python ms_fbi.py j309_1 RR050_RV0 [restarts=200]
"""
import os, sys, random
import numpy as np
from msd_decoder import load, start_order, BD

prefix, cond = sys.argv[1], sys.argv[2]
restarts = int(sys.argv[3]) if len(sys.argv) > 3 else 200
random.seed(2)
cs = load(prefix, cond)
n, T, R = cs.n, cs.T, cs.R
mip_min = min(s[0] for s in cs.sols)
ZERO = np.zeros(n, dtype=int)


def fits(usage, j, t):
    d = cs.dur[j]
    if t < 0 or t + d > T:
        return False
    return bool(np.all(usage[:, t:t + d] + cs.dem[j][:, None] <= cs.cap_t[:, t:t + d]))


def place(usage, j, t, sign=1):
    usage[:, t:t + cs.dur[j]] += sign * cs.dem[j][:, None]


def backward(start, ms):
    """右寄せ: 終了の遅い順に、できるだけ遅く（後続の開始より前、メイクスパン以内）"""
    usage = np.zeros((R, T), dtype=np.int64)
    new = start.copy()
    order = sorted(range(n), key=lambda j: -(start[j] + cs.dur[j]))
    for j in order:
        d = cs.dur[j]
        if d <= 0:
            new[j] = min([new[s] for s in cs.succ[j]], default=ms)
            continue
        lst = min([new[s] for s in cs.succ[j] if cs.dur[s] > 0] + [ms]) - d
        t = lst
        while t >= 0 and not fits(usage, j, t):
            t -= 1
        if t < 0:
            return None
        new[j] = t; place(usage, j, t)
    return new


def forward_from(start):
    """左寄せ: 開始の早い順の作業リストを窓幅0で Serial SGS 復号（= MaxShiftDur で rho=0）"""
    return cs.decode(start_order(list(start)), ZERO)


def fbi(seq, rounds=20):
    r = cs.decode(seq, ZERO)
    if r is None:
        return None, 10 ** 9
    st, ms, _ = r
    best = ms
    for _ in range(rounds):
        b = backward(st, ms)
        if b is None:
            break
        f = forward_from(b)
        if f is None:
            break
        st2, ms2, _ = f
        if ms2 >= ms:
            break
        st, ms = st2, ms2
        best = min(best, ms)
    return st, best


def a2_perm():
    p = os.path.join(BD, "results", "FUN_A2", prefix, f"PERM_A2_{prefix}_{cond}.txt")
    rows = [l.split() for l in open(p) if len(l.split()) == n + 2]
    return [list(map(int, v[2:])) for v in sorted(rows, key=lambda v: float(v[0]))]


def random_perm():
    indeg = [len(p) for p in cs.preds]; ready = [j for j in range(n) if indeg[j] == 0]; order = []
    while ready:
        j = ready.pop(random.randrange(len(ready))); order.append(j)
        for k in cs.succ[j]:
            indeg[k] -= 1
            if indeg[k] == 0:
                ready.append(k)
    return order


print(f"{prefix} {cond}: MIP min makespan {mip_min:.0f}")
for i, perm in enumerate(a2_perm()[:3]):
    print(f"  A2 perm #{i}: before {cs.decode(perm, ZERO)[1]} -> after FBI {fbi(perm)[1]}")
best = 10 ** 9; hist = {}
for r in range(restarts):
    _, ms = fbi(random_perm())
    hist[ms] = hist.get(ms, 0) + 1
    best = min(best, ms)
print(f"  random lists + FBI ({restarts}): best {best}; distribution(top) {sorted(hist.items())[:6]}")
print(f"RESULT {prefix} {cond}: FBI best {best} vs MIP {mip_min:.0f}")
