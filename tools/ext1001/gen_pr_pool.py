"""
gen_pr_pool.py -- 指摘1対策: 優先規則（LFT）による偏りのあるランダム生成で、工期の短い作業リストを作る。

各 (instance, cond) で、LFT 優先（最遅終了時刻が早い作業ほど選ばれやすい、重み (優先度差+1)^alpha）で
作業リストを N 本生成し、窓幅0（最早配置）で復号したメイクスパンの短い順に、重複なく K 本を残す。
出力は NSGAMaxShiftDurCpp の --a2seed が読む形式（各行: ms cost perm...）。

  出力: cmake-build-release/results/EXT_COST/SEED_PR/<prefix>/PERM_A2_<prefix>_<cond>.txt
  使い方: python gen_pr_pool.py [N=3000] [K=12] [alpha=3]
"""
import os, sys, random, time
import numpy as np
from msd_decoder import Case, BD

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
K = int(sys.argv[2]) if len(sys.argv) > 2 else 12
ALPHA = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
OUT = os.path.join(BD, "results", "EXT_COST", "SEED_PR")
random.seed(12345)


def case_for(prefix, cond):
    # 容量・地平線は SCHED ファイルから取る（MIP が無い条件は NSGA の SCHED を使う）
    for p in [os.path.join(BD, "results", "EXT_COST", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt"),
              os.path.join(BD, "results", "EXT_COST", "FUN_ENC", prefix, f"SCHED_ENC_{prefix}_{cond}_MaxShiftDur.txt")]:
        if os.path.exists(p):
            return Case(p, os.path.join(BD, "j30.sm", prefix + ".sm"), os.path.join(BD, "costs_ext", f"costs_{prefix}.csv"))
    return None


def main():
    t0 = time.time(); rows = []
    for x in range(1, 49):
        prefix = f"j30{x}_1"
        for cond in CONDS:
            cs = case_for(prefix, cond)
            if cs is None:
                continue
            n = cs.n; Z = np.zeros(n, dtype=int)
            lft = [cs.T] * n
            for j in reversed(range(n)):          # PSPLIB は番号順がトポロジカル順
                for s in cs.succ[j]:
                    lft[j] = min(lft[j], lft[s] - cs.dur[s])
            pr = [-v for v in lft]
            seen = {}
            for _ in range(N):
                indeg = [len(p) for p in cs.preds]; ready = [j for j in range(n) if indeg[j] == 0]; order = []
                while ready:
                    vals = [pr[j] for j in ready]; lo = min(vals)
                    j = random.choices(ready, weights=[(v - lo + 1) ** ALPHA for v in vals])[0]
                    ready.remove(j); order.append(j)
                    for k in cs.succ[j]:
                        indeg[k] -= 1
                        if indeg[k] == 0:
                            ready.append(k)
                r = cs.decode(order, Z)
                if r is None:
                    continue
                key = tuple(order)
                if key not in seen:
                    seen[key] = (r[1], r[2])
            best = sorted(seen.items(), key=lambda kv: kv[1])[:K]
            d = os.path.join(OUT, prefix); os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, f"PERM_A2_{prefix}_{cond}.txt"), "w") as fp:
                for perm, (ms, cost) in best:
                    fp.write(f"{ms} {cost:.1f} " + " ".join(map(str, perm)) + "\n")
            mip_min = min((s[0] for s in cs.sols), default=None) if "FUN_MIP" in "" else None
            rows.append((prefix, cond, best[0][1][0] if best else None))
        print(f"{prefix} done ({time.time()-t0:.0f}s)", flush=True)
    with open(os.path.join(OUT, "_pool_best_makespan.csv"), "w") as fp:
        fp.write("instance,cond,pool_best_ms\n")
        for r in rows:
            fp.write(",".join(map(str, r)) + "\n")


if __name__ == "__main__":
    main()
