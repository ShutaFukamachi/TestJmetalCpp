"""features.py -- PSPLIB インスタンスの特徴量（NC, RF, RS）を .sm から直接計算する。
  NC : 非ダミー間を含む先行関係の弧数 / 活動数（ダミー含む）
  RF : 非ダミー活動が使う資源種類の割合の平均
  RS : 資源ごとに (容量 - 最大単独需要) / (ES スケジュールのピーク需要 - 最大単独需要) の平均（Kolisch の定義）
"""
import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import os, csv
import numpy as np
BD = _os.path.join(ROOT, "cmake-build-release")


def parse_sm(path):
    L = open(path).read().split("\n")
    def find(key):
        return next(i for i, l in enumerate(L) if key in l)
    i = find("PRECEDENCE RELATIONS") + 2
    succ = []
    while L[i].strip() and not L[i].startswith("*"):
        p = L[i].split(); succ.append([int(x) - 1 for x in p[3:3 + int(p[2])]]); i += 1
    n = len(succ)
    i = find("REQUESTS/DURATIONS") + 3
    dur, dem = [], []
    for _ in range(n):
        p = L[i].split(); dur.append(int(p[2])); dem.append(list(map(int, p[3:]))); i += 1
    i = find("RESOURCEAVAILABILITIES") + 2
    cap = list(map(int, L[i].split()))
    return succ, np.array(dur), np.array(dem), np.array(cap)


def features(path):
    succ, dur, dem, cap = parse_sm(path)
    n, K = dem.shape
    arcs = sum(len(s) for s in succ)
    NC = arcs / n
    real = [j for j in range(n) if dur[j] > 0]
    RF = float(np.mean([(dem[j] > 0).sum() / K for j in real]))
    es = np.zeros(n, dtype=int)
    preds = [[] for _ in range(n)]
    for a in range(n):
        for b in succ[a]:
            preds[b].append(a)
    for j in range(n):  # PSPLIB は番号順がトポロジカル順
        es[j] = max((es[p] + dur[p] for p in preds[j]), default=0)
    H = int((es + dur).max())
    prof = np.zeros((K, H + 1), dtype=int)
    for j in real:
        prof[:, es[j]:es[j] + dur[j]] += dem[j][:, None]
    rs = []
    for k in range(K):
        rmin = dem[:, k].max(); rmax = prof[k].max()
        rs.append((cap[k] - rmin) / (rmax - rmin) if rmax > rmin else 1.0)
    return dict(NC=round(NC, 3), RF=round(RF, 3), RS=round(float(np.mean(rs)), 3), sum_dur=int(dur.sum()))


if __name__ == "__main__":
    rows = []
    for size in ["j30", "j60"]:
        for x in range(1, 49):
            p = f"{size}{x}_1"
            rows.append(dict(size=size, instance=p, param_set=x, **features(os.path.join(BD, f"{size}.sm", p + ".sm"))))
    out = os.path.join(BD, "analysis", "ext1001", "features.csv")
    with open(out, "w", newline="") as fp:
        w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    for r in rows[:16]:
        print(r)
