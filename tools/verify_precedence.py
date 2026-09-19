#!/usr/bin/env python3
"""
verify_precedence.py — SCHED_ENC_*/SCHED_MIP_* の全解について、.sm の先行関係
(PRECEDENCE RELATIONS) との整合を検査する（.miss_memory/022, 029 再発防止用）。

verify_cost_consistency.py の先行制約チェックは dur<=0（ダミー）を両端とも除外して
いるため「実ジョブ同士」の違反しか見えない。本ツールはダミーを含む全ペアを検査し、
- 実ジョブ→実ジョブ
- 実ジョブ→ダミー / ダミー→実ジョブ
- ダミー→ダミー
を区別して集計する（③ダミー開始時刻0固定の記録上の問題を可視化するため）。

cost>=1e8（実行不能ペナルティ1e9）の解は、評価が早期return した時点までの
「部分的にしか埋まっていない」start 配列を持つため（.miss_memory/029）、
先行制約チェックの対象から除外する。これらは gap48_analysis.py 等の下流処理でも
常にフィルタされる解であり、ここでの「違反」は実運用上意味を持たないノイズになる。

使い方:
  python verify_precedence.py --build-dir . --instance j3025_1 --condition RR050_RV0 --series MaxShiftDur
  python verify_precedence.py --build-dir .                      # 48インスタンス全件スイープ
"""
import argparse
import glob
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location(
    "vcc", os.path.join(os.path.dirname(os.path.abspath(__file__)), "verify_cost_consistency.py"))
vcc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vcc)


def check_one_file(sched_path, sm_path, verbose_pairs=None):
    """1つの SCHED_*.txt を検査し、集計 dict と (a,b,是duty) の違反詳細リストを返す。"""
    n, nRes, dur, demand, sols = vcc.read_sched(sched_path)
    succ = vcc.parse_sm_successors(sm_path, n)
    if succ is None:
        return None, []

    cat_total = defaultdict(int)
    cat_violated_sols = defaultdict(set)   # category -> set(sol index) それぞれで1件以上違反
    pair_violation_count = defaultdict(int)
    n_sols_raw = len(sols)
    n_sols_checked = 0

    for i, (ms, cost, starts) in enumerate(sols):
        if cost >= 1e8:
            continue
        n_sols_checked += 1
        for a in range(n):
            for s in succ[a]:
                if not (0 <= s < n):
                    continue
                a_dummy = dur[a] <= 0
                s_dummy = dur[s] <= 0
                if a_dummy and s_dummy:
                    cat = "dummy-dummy"
                elif a_dummy:
                    cat = "dummy-real"
                elif s_dummy:
                    cat = "real-dummy"
                else:
                    cat = "real-real"
                cat_total[cat] += 1
                if starts[a] + dur[a] > starts[s]:
                    cat_violated_sols[cat].add(i)
                    pair_violation_count[(a, s, cat)] += 1

    result = {
        "n_sols": n_sols_checked,
        "n_sols_raw": n_sols_raw,
        "n_sols_infeasible_skipped": n_sols_raw - n_sols_checked,
        "n_pairs_checked_per_sol": sum(len(succ[a]) for a in range(n)),
        "violated_sols": {cat: len(v) for cat, v in cat_violated_sols.items()},
    }
    top_pairs = sorted(pair_violation_count.items(), key=lambda kv: -kv[1])[:10]
    return result, top_pairs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=".")
    ap.add_argument("--sm-dir", default="j30.sm")
    ap.add_argument("--instance", default=None, help="例: j3025_1（省略時は全48インスタンス）")
    ap.add_argument("--condition", default=None, help="例: RR050_RV0（省略時は全6条件）")
    ap.add_argument("--series", default=None,
                     help="例: MaxShiftDur / P1_MaxShift / P1_SchedObj / MIP（省略時は全系列）")
    args = ap.parse_args()
    bd = args.build_dir

    conds = [args.condition] if args.condition else \
        ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]

    if args.instance:
        instances = [args.instance]
    else:
        instances = [f"j30{x}_1" for x in range(1, 49)]

    grand = defaultdict(lambda: [0, 0])  # cat -> [violated_file_count, total_file_count]
    all_top_pairs = defaultdict(int)
    n_files_checked = 0

    for prefix in instances:
        sm_path = os.path.join(bd, args.sm_dir, f"{prefix}.sm")
        for cond in conds:
            candidates = []
            if args.series is None or args.series == "MIP":
                candidates.append(("MIP", os.path.join(
                    bd, "results", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")))
            for series_name, fname_part in [
                ("MaxShiftDur", f"{prefix}_{cond}_MaxShiftDur"),
                ("MaxShift", f"{prefix}_{cond}_P1_MaxShift"),
                ("SchedObj", f"{prefix}_{cond}_P1_SchedObj"),
            ]:
                if args.series and args.series not in (series_name, f"P1_{series_name}"):
                    continue
                candidates.append((series_name, os.path.join(
                    bd, "results", "FUN_ENC", prefix, f"SCHED_ENC_{fname_part}.txt")))

            for series_name, path in candidates:
                if not os.path.exists(path):
                    continue
                result, top_pairs = check_one_file(path, sm_path)
                if result is None:
                    continue
                n_files_checked += 1
                any_violation = False
                for cat, cnt in result["violated_sols"].items():
                    grand[cat][1] += 1
                    if cnt > 0:
                        grand[cat][0] += 1
                        any_violation = True
                if any_violation:
                    tag = f"{series_name:<12} {prefix:<10} {cond}"
                    print(f"[VIOLATION] {tag}  n_sols={result['n_sols']}  "
                          f"violated_by_cat={result['violated_sols']}")
                    for (a, s, cat), cnt in top_pairs:
                        if cnt > 0:
                            print(f"    job{a}->job{s} ({cat}): {cnt}/{result['n_sols']} sols")
                            all_top_pairs[(prefix, cond, series_name, a, s, cat)] += cnt

    print(f"\n[SUMMARY] {n_files_checked} files checked")
    for cat in ["real-real", "real-dummy", "dummy-real", "dummy-dummy"]:
        v, t = grand.get(cat, [0, 0])
        print(f"  {cat:<12}: {v}/{t} files had >=1 violating solution")


if __name__ == "__main__":
    main()
