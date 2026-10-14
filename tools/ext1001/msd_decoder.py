"""
msd_decoder.py -- RCPSP_Problem_MaxShiftDur::evaluate の忠実な Python 再現（分析専用）

src/problems/RCPSP_Problem_MaxShiftDur.cpp の手順をそのまま写す:
  EST -> t_mak（EST以降で最初に連続配置可能な時刻）-> [t_mak, min(T-d, t_mak+win)] で
  コスト最小（strict '<' で最早優先）に配置。
窓幅 win は呼び出し側が「スロット数」で直接与える（遺伝子 rho から換算する前の値）。
"""
import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import os
import numpy as np

BD = _os.path.join(ROOT, "cmake-build-release")


def parse_succ(sm_path, n):
    lines = open(sm_path).read().split("\n")
    i = 0
    while "PRECEDENCE RELATIONS" not in lines[i]:
        i += 1
    i += 2
    succ = [[] for _ in range(n)]
    for _ in range(n):
        p = lines[i].split()
        i += 1
        j = int(p[0]) - 1
        k = int(p[2])
        succ[j] = [int(x) - 1 for x in p[3:3 + k]]
    return succ


class Case:
    def __init__(self, sched_path, sm_path, cost_path):
        L = [ln for ln in open(sched_path).read().split("\n") if ln.strip()]
        n, R = map(int, L[0].split())
        self.n, self.R = n, R
        self.dur = np.array(list(map(int, L[1].split())))
        self.dem = np.array([list(map(int, L[2 + j].split())) for j in range(n)])
        cap = np.array(list(map(int, L[2 + n].split())))
        Tline = int(L[3 + n].split()[0])
        idx = 4 + n
        if Tline > 0:
            self.cap_t = np.array([list(map(int, L[idx + k].split())) for k in range(R)])
            idx += R
            self.T = max(int(self.dur.sum()), Tline)
        else:
            self.T = int(self.dur.sum())
            self.cap_t = np.repeat(cap[:, None], self.T, axis=1)
        K = int(L[idx].split()[0]); idx += 1
        self.sols = []
        for ln in L[idx:idx + K]:
            p = ln.split()
            self.sols.append((float(p[0]), float(p[1]), list(map(int, p[2:]))))
        self.succ = parse_succ(sm_path, n)
        self.preds = [[] for _ in range(n)]
        for a in range(n):
            for s in self.succ[a]:
                self.preds[s].append(a)
        # コスト表（拡張表は T_ext >= 地平線なのでクランプは起きない。念のため同じクランプを実装）
        C = [ln for ln in open(cost_path).read().split("\n") if ln.strip()]
        Rc, Tc = map(int, C[0].split())
        cost = np.array([list(map(float, C[1 + k].split())) for k in range(Rc)])
        tt = np.minimum(np.arange(self.T), Tc - 1)
        cost = cost[:, tt]                                   # R x T
        slot = self.dem @ cost                                # n x T : 1スロットのコスト
        cs = np.concatenate([np.zeros((n, 1)), np.cumsum(slot, axis=1)], axis=1)
        self.jobcost = np.full((n, self.T), np.inf)
        for j in range(n):
            d = self.dur[j]
            if d <= 0:
                self.jobcost[j, :] = 0.0
                continue
            m = self.T - d + 1
            self.jobcost[j, :m] = cs[j, d:d + m] - cs[j, :m]

    def decode(self, seq, win, record_tmak=False):
        n, R, T = self.n, self.R, self.T
        usage = np.zeros((R, T), dtype=np.int64)
        start = np.zeros(n, dtype=np.int64); finish = np.zeros(n, dtype=np.int64)
        tmak_arr = np.zeros(n, dtype=np.int64)
        total = 0.0
        for j in seq:
            d = int(self.dur[j])
            est = max((finish[p] for p in self.preds[j]), default=0)
            if d <= 0:
                start[j] = finish[j] = est; tmak_arr[j] = est
                continue
            okslot = np.all(usage + self.dem[j][:, None] <= self.cap_t, axis=0)  # T
            c = np.concatenate([[0], np.cumsum(~okslot)])
            # canPlace(t) <=> t+d<=T かつ [t,t+d) に不可スロットなし
            tt = np.arange(T - d + 1)
            can = (c[tt + d] - c[tt]) == 0
            cand = np.nonzero(can[est:])[0]
            if len(cand) == 0:
                return None
            t_mak = est + int(cand[0])
            tmak_arr[j] = t_mak
            w = win(j, t_mak) if callable(win) else int(win[j])
            latest = min(T - d, t_mak + max(0, w))
            placed = t_mak
            if w > 0 and latest > t_mak:
                seg = np.where(can[t_mak:latest + 1], self.jobcost[j, t_mak:latest + 1], np.inf)
                placed = t_mak + int(np.argmin(seg))   # argmin は最初の最小 = strict '<' の左詰め
            start[j] = placed; finish[j] = placed + d
            usage[:, placed:placed + d] += self.dem[j][:, None]
            total += self.jobcost[j, placed]
        ms = int(finish.max())
        if record_tmak:
            return start, ms, total, tmak_arr
        return start, ms, total


def load(prefix, cond, kind="MIP", method=None, root="EXT", size="j30"):
    rr = os.path.join(BD, "results", "EXT_COST") if root == "EXT" else os.path.join(BD, "results")
    if kind == "MIP":
        sp = os.path.join(rr, "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
    else:
        tag = {"MaxShiftDur": f"{prefix}_{cond}_MaxShiftDur", "MaxShift": f"{prefix}_{cond}_P1_MaxShift",
               "SchedObj": f"{prefix}_{cond}_P1_SchedObj"}[method]
        sp = os.path.join(rr, "FUN_ENC", prefix, f"SCHED_ENC_{tag}.txt")
    costdir = "costs_ext" if root == "EXT" else "costs"
    return Case(sp, os.path.join(BD, f"{size}.sm", f"{prefix}.sm"),
                os.path.join(BD, costdir, f"costs_{prefix}.csv"))


def start_order(starts):
    return sorted(range(len(starts)), key=lambda j: (starts[j], j))
