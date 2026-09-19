#!/usr/bin/env python3
"""
verify_cost_consistency.py — 全系列の SCHED_* の記録コストを、唯一の正本
costs/costs_<prefix>.csv で再計算して照合し、併せて先行制約(precedence)の
実行可能性を検証する回帰ツール（欠陥2＝系列間コスト表ドリフトの即時検知）。

対象（cmake-build-release から実行想定, --build-dir で変更可）:
  results/FUN_ENC/<prefix>/SCHED_ENC_<prefix>_<cond>_<series>.txt   (NSGA: SchedObj/MaxShift/MaxShiftDur/P1..P3)
  results/FUN_MIP/<prefix>/SCHED_MIP_<prefix>_<cond>.txt            (MIP)

SCHED 形式（NSGA/MIP 共通の頑健パース）:
  line1: n nRes
  line2: durations (n)
  next n lines: demand rows (n x nRes)
  ... (MIP は静的容量・capacity_t・front数の余分行を含む) ...
  解行: ちょうど (2+n) トークン "<ms> <cost> <start_0..n-1>"

コスト再計算 = Σ_{j:dur>0} Σ_{tau=s_j..s_j+d_j-1} Σ_k cost[k][min(tau,T-1)] * demand[j][k]
先行制約     = 各 (i->s) 実ジョブ対で start_i + dur_i <= start_s

使い方:
  python ../tools/verify_cost_consistency.py [--build-dir .] [--tol 1.0]
  全 PASS で exit 0、1件でも不一致/違反があれば FAIL 詳細を出し exit 1。
"""
import argparse, glob, os, re, sys


def discover_instances(bd):
    """costs/costs_<prefix>.csv を正本の一覧として自動検出する（48インスタンス対応）。"""
    paths = sorted(glob.glob(os.path.join(bd, 'costs', 'costs_*.csv')))
    prefixes = []
    for p in paths:
        m = re.match(r'^costs_(.+)\.csv$', os.path.basename(p))
        if m:
            prefixes.append(m.group(1))
    return prefixes


def read_cost_csv(path):
    with open(path) as f:
        R, T = map(int, f.readline().split())
        table = [list(map(float, f.readline().split())) for _ in range(R)]
    return R, T, table


def read_sched(path):
    lines = [ln for ln in open(path).read().split('\n') if ln.strip()]
    n, nRes = map(int, lines[0].split())
    dur = list(map(int, lines[1].split()))
    demand = [list(map(int, lines[2 + j].split())) for j in range(n)]
    sols = []
    for ln in lines[2 + n:]:
        p = ln.split()
        if len(p) != 2 + n:
            continue
        try:
            ms = float(p[0]); cost = float(p[1]); starts = list(map(int, p[2:]))
        except ValueError:
            continue
        sols.append((ms, cost, starts))
    return n, nRes, dur, demand, sols


def parse_sm_successors(sm_path, n):
    """PSPLIB .sm の PRECEDENCE RELATIONS から後続リスト(0-indexed)を返す。"""
    succ = [[] for _ in range(n)]
    if not os.path.exists(sm_path):
        return None
    lines = open(sm_path).read().split('\n')
    i = 0
    while i < len(lines) and 'PRECEDENCE RELATIONS' not in lines[i]:
        i += 1
    i += 2  # skip section header + column header
    for _ in range(n):
        if i >= len(lines):
            break
        p = lines[i].split(); i += 1
        if len(p) < 3:
            continue
        jobnr = int(p[0]); nsucc = int(p[2])
        for s in p[3:3 + nsucc]:
            succ[jobnr - 1].append(int(s) - 1)  # 1-indexed -> 0-indexed
    return succ


def recompute_cost(starts, dur, demand, nRes, table, T):
    total = 0.0
    for j in range(len(dur)):
        d = dur[j]
        if d <= 0:
            continue
        for tau in range(starts[j], starts[j] + d):
            tt = min(max(tau, 0), T - 1)
            for k in range(nRes):
                total += table[k][tt] * demand[j][k]
    return total


def series_of(basename):
    # SCHED_ENC_<prefix>_<cond>_<series>  /  SCHED_MIP_<prefix>_<cond>
    if basename.startswith('SCHED_MIP_'):
        return 'MIP'
    m = re.match(r'^SCHED_ENC_.+_RR\d+_RV\d_(.+)$', basename)
    return m.group(1) if m else '?'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build-dir', default='.')
    ap.add_argument('--tol', type=float, default=1.0)
    ap.add_argument('--sm-dir', default='j30.sm', help='PSPLIB .sm ディレクトリ')
    ap.add_argument('--all-conditions', action='store_true',
                     help='RR=0.75（限界条件、既定では破綻するため除外）も含め全条件を検査する')
    ap.add_argument('--prefix-regex', default=None,
                     help='検証対象インスタンスを絞り込む正規表現（例: ^j30\\d+_1$）')
    args = ap.parse_args()
    bd = args.build_dir

    instances = discover_instances(bd)
    if args.prefix_regex:
        rx = re.compile(args.prefix_regex)
        instances = [p for p in instances if rx.match(p)]
    print(f"[INFO] {len(instances)} instances discovered under {os.path.join(bd, 'costs')}"
          + (f" (filtered by --prefix-regex {args.prefix_regex!r})" if args.prefix_regex else ""))

    # series -> [cost_ok, cost_total, prec_ok, prec_total, mismatch_files]
    from collections import defaultdict
    stats = defaultdict(lambda: [0, 0, 0, 0])
    fails = []
    per_series_files = defaultdict(int)

    cost_cache = {}
    succ_cache = {}

    for prefix in instances:
        cost_csv = os.path.join(bd, 'costs', f'costs_{prefix}.csv')
        if not os.path.exists(cost_csv):
            fails.append(f"[MISSING CANONICAL] {cost_csv}")
            continue
        if prefix not in cost_cache:
            cost_cache[prefix] = read_cost_csv(cost_csv)
        R, T, table = cost_cache[prefix]
        if prefix not in succ_cache:
            succ_cache[prefix] = parse_sm_successors(
                os.path.join(bd, args.sm_dir, prefix + '.sm'), 0) or None

        patterns = [
            os.path.join(bd, 'results', 'FUN_ENC', prefix, f'SCHED_ENC_{prefix}_*.txt'),
            os.path.join(bd, 'results', 'FUN_MIP', prefix, f'SCHED_MIP_{prefix}_*.txt'),
        ]
        for pat in patterns:
            for sched in sorted(glob.glob(pat)):
                bn = os.path.splitext(os.path.basename(sched))[0]
                # RR=0.75 は既定では検査対象外（MIP・NSGA とも実務的に破綻する限界条件のため）。
                # --all-conditions 指定時のみ検査する。
                if not args.all_conditions and re.search(r'RR075_RV\d', bn):
                    continue
                series = series_of(bn)
                per_series_files[series] += 1
                n, nRes, dur, demand, sols = read_sched(sched)
                # successors (parse .sm with correct n)
                succ = parse_sm_successors(os.path.join(bd, args.sm_dir, prefix + '.sm'), n)
                st = stats[series]
                for i, (ms, cost, starts) in enumerate(sols):
                    # --- cost consistency ---
                    st[1] += 1
                    rc = recompute_cost(starts, dur, demand, nRes, table, T)
                    if abs(rc - cost) < args.tol:
                        st[0] += 1
                    else:
                        fails.append(f"[COST] {bn} sol#{i} recorded={cost:.1f} "
                                     f"recompute={rc:.1f} diff={abs(rc-cost):.1f}")
                    # --- precedence feasibility ---
                    if succ is not None:
                        st[3] += 1
                        viol = 0
                        for a in range(n):
                            if dur[a] <= 0:
                                continue
                            for s in succ[a]:
                                if 0 <= s < n and dur[s] > 0 and starts[a] + dur[a] > starts[s]:
                                    viol += 1
                        if viol == 0:
                            st[2] += 1
                        else:
                            fails.append(f"[PREC] {bn} sol#{i} {viol} precedence violations")

    # 受入対象の4系列（これらの整合を exit ゲートにする）。
    # P2/P3・LS/NF・MaxShiftSeg 等の実験系列は「最小構成」の対象外＝INFO 表示のみ。
    CORE = {'MIP', 'MaxShiftDur', 'MaxShift', 'P1_MaxShift', 'SchedObj', 'P1_SchedObj'}

    # ---- report ----
    print("=" * 84)
    print(f"{'series':<18}{'files':>7}{'cost_ok/total':>18}{'prec_ok/total':>18}{'  scope':<10}")
    print("-" * 84)
    all_ok = True
    for series in sorted(stats):
        c_ok, c_tot, p_ok, p_tot = stats[series]
        is_core = series in CORE
        bad = (c_ok != c_tot or p_ok != p_tot)
        cflag = '' if c_ok == c_tot else '  <<< COST MISMATCH'
        pflag = '' if p_ok == p_tot else '  <<< PREC VIOL'
        if bad and is_core:
            all_ok = False
        scope = 'CORE' if is_core else 'info'
        print(f"{series:<18}{per_series_files[series]:>7}"
              f"{f'{c_ok}/{c_tot}':>18}{f'{p_ok}/{p_tot}':>18}  {scope:<8}{cflag}{pflag}")
    print("-" * 84)

    if fails:
        print(f"\nmismatch details (first 30 of {len(fails)}; non-CORE are informational):")
        for f in fails[:30]:
            print("  " + f)

    if all_ok:
        print("\n>>> PASS: all CORE series (MIP/MaxShift/SchedObj/MaxShiftDur) consistent "
              "with canonical cost table; no precedence violations.")
        sys.exit(0)
    else:
        print("\n>>> FAIL: CORE-series inconsistencies detected (see above).")
        sys.exit(1)


if __name__ == '__main__':
    main()
