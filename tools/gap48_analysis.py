#!/usr/bin/env python3
"""
gap48_analysis.py — j30 48インスタンス x 6条件で MIP(厳密解) と NSGA(SchedObj/MaxShift/
MaxShiftDur) の matched-makespan gap を集計する。

フィルタ方針（.miss_memory/022 と同じ対処＝点単位除外、条件は残す）:
  - 各解(ms, cost, starts) について、記録cost>=1e8（実行不能ペナルティ1e9） または
    先行制約違反(succ関係で start[a]+dur[a] > start[s])がある場合、その解を前線構築前に除外する。
  - フィルタ後、MIP側の有効解が0件になった(instance,condition)は「MIP側が真に実行不能」と
    判定し、全methodの当該行を excluded=True, reason=MIP_INFEASIBLE として集計から除外する
    （MIPログの "Infeasible (makespan solve)" と整合）。
  - NSGA側の有効解が0件だがMIP側に有効解がある場合は、excluded=True, reason=NSGA_NO_FEASIBLE
    として残す（手法の実探索失敗を示す重要な所見のため、集計からは除くが行として可視化する）。

gap定義（matched-makespan gap。現行・コスト超過率）:
  MIPの有効点(m, c_MIP)ごとに、NSGA有効フロントで ms<=m を満たす最小コスト c_NSGA を取り、
  (c_NSGA - c_MIP)/c_MIP を計算。平均・最大を (instance,condition,method) の gap とする。
  NSGA側にms<=mを満たす点が一つもない場合はその MIP点を unmatched としてスキップする
  （n_points には算入しないが unmatched_mip_points 列で件数を報告する）。

追加指標（2026-08 拡張。現行指標は変更・削除しない）:
  ① matched-cost makespan gap（新規・工期超過率、現行指標の対称版）:
     MIPの有効点(m_MIP, c_MIP)ごとに、NSGA有効フロントで cost<=c_MIP を満たす最小工期 m_NSGA
     を取り、(m_NSGA - m_MIP)/m_MIP を計算。cost<=c_MIP を満たすNSGA点が無ければ ms_unmatched
     としてスキップする。現行指標が「コスト方向」だけを見て「NSGAが工期そのものに到達できない」
     ケースを静かに落としていた問題（unmatched_mip_points列）への対処。
  ② Hypervolume（HV）: (instance,condition) ごとに MIP∪NSGA の全有効点の min/max で両目的を
     [0,1] に正規化し、共通の参照点(nadir)=(1.1,1.1) に対するHVをMIP・NSGA双方で計算する。
     hv_ratio = hv_nsga/hv_mip（1.0に近いほど良い、理論上MIPが上限）。
  ③ C-metric（Coverage）: C(MIP,NSGA) = NSGAの解のうちMIPのいずれかの解に弱支配される割合、
     C(NSGA,MIP) はその逆。フロント同士（pareto_front 適用後）で計算する。

出力: analysis/gap48/gap_summary.csv, analysis/gap48/gap_distribution_summary.csv
実行: cmake-build-release から `python ../tools/gap48_analysis.py`
"""
import csv
import glob
import importlib.util
import os
import re
import statistics as st
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("vcc", os.path.join(HERE, "verify_cost_consistency.py"))
vcc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vcc)

INFEASIBLE_THRESHOLD = 1e8
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
METHODS = {
    "MaxShiftDur": "{prefix}_{cond}_MaxShiftDur",
    "MaxShift": "{prefix}_{cond}_P1_MaxShift",
    "SchedObj": "{prefix}_{cond}_P1_SchedObj",
}


def load_valid_points(sched_path, sm_path):
    """SCHED_*.txt を読み、(ms,cost_recomputed)有効点リストと除外統計を返す。"""
    if not os.path.exists(sched_path):
        return None, {"missing": True}
    n, nRes, dur, demand, sols = vcc.read_sched(sched_path)
    succ = vcc.parse_sm_successors(sm_path, n)
    cost_csv_ok = True
    pts = []
    n_total = len(sols)
    n_infeasible = 0
    n_prec = 0
    for ms, cost, starts in sols:
        if cost >= INFEASIBLE_THRESHOLD:
            n_infeasible += 1
            continue
        if succ is not None:
            viol = 0
            for a in range(n):
                if dur[a] <= 0:
                    continue
                for s in succ[a]:
                    if 0 <= s < n and dur[s] > 0 and starts[a] + dur[a] > starts[s]:
                        viol += 1
                        break
                if viol:
                    break
            if viol:
                n_prec += 1
                continue
        pts.append((ms, cost))
    stats = {
        "missing": False,
        "n_total": n_total,
        "n_infeasible": n_infeasible,
        "n_prec_filtered": n_prec,
        "n_valid": len(pts),
    }
    return pts, stats


def pareto_front(points):
    """(ms,cost) 最小化2目的の非支配フロント（ms昇順、costの走行最小値）。"""
    if not points:
        return []
    pts = sorted(set(points))
    front = []
    best_cost = float("inf")
    for ms, cost in pts:
        if cost < best_cost:
            front.append((ms, cost))
            best_cost = cost
    return front


def min_cost_at_or_before(front, m):
    """front（ms昇順・cost走行最小）から ms<=m の最小costを返す。無ければNone。"""
    best = None
    for ms, cost in front:
        if ms <= m:
            best = cost
        else:
            break
    return best


def min_ms_at_or_below_cost(front, c):
    """front（ms昇順・cost走行最小＝非増加）から cost<=c を満たす最小msを返す。無ければNone。
    front は ms 昇順で cost が単調非増加なので、cost<=c を満たす最初の点の ms が答え。"""
    for ms, cost in front:
        if cost <= c:
            return ms
    return None


def hypervolume_2d(front_norm, ref):
    """正規化済み(ms,cost)の非支配フロントに対する2次元Hypervolumeを ref を基準に計算する
    （両目的とも最小化）。front_norm は ms 昇順（cost 非増加）を仮定。"""
    if not front_norm:
        return 0.0
    rx, ry = ref
    hv = 0.0
    prev_y = ry
    for ms, cost in front_norm:
        if cost < prev_y and ms < rx:
            hv += (rx - ms) * (prev_y - cost)
            prev_y = cost
    return hv


def normalize_front(front, ms_lo, ms_hi, c_lo, c_hi):
    def nx(v, lo, hi):
        return (v - lo) / (hi - lo) if hi > lo else 0.0
    return [(nx(ms, ms_lo, ms_hi), nx(cost, c_lo, c_hi)) for ms, cost in front]


def c_metric(front_a, front_b):
    """C(A,B) = front_b のうち front_a のいずれかの点に弱支配される割合。"""
    if not front_b:
        return None
    if not front_a:
        return 0.0
    covered = 0
    for bm, bc in front_b:
        for am, ac in front_a:
            if am <= bm and ac <= bc:
                covered += 1
                break
    return covered / len(front_b)


def count_dominated_mip_points(mip_front, nsga_front):
    """NSGAのいずれかの点に(弱く支配され、少なくとも一方は厳密に優れる)MIPフロント点数。"""
    cnt = 0
    for m_ms, m_cost in mip_front:
        for n_ms, n_cost in nsga_front:
            if n_ms <= m_ms and n_cost <= m_cost and (n_ms < m_ms or n_cost < m_cost):
                cnt += 1
                break
    return cnt


def main():
    bd = "."
    out_dir = os.path.join(bd, "analysis", "gap48")
    os.makedirs(out_dir, exist_ok=True)

    rows = []
    for x in range(1, 49):
        prefix = f"j30{x}_1"
        sm_path = os.path.join(bd, "j30.sm", f"{prefix}.sm")
        for cond in CONDS:
            mip_sched = os.path.join(bd, "results", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
            mip_pts, mip_stats = load_valid_points(mip_sched, sm_path)
            mip_front = pareto_front(mip_pts) if mip_pts is not None else []

            mip_excluded = mip_stats.get("missing") or mip_stats["n_valid"] == 0
            mip_exclude_reason = ""
            if mip_stats.get("missing"):
                mip_exclude_reason = "MIP_FILE_MISSING"
            elif mip_stats["n_valid"] == 0:
                mip_exclude_reason = "MIP_INFEASIBLE"

            for method, pattern in METHODS.items():
                enc_sched = os.path.join(
                    bd, "results", "FUN_ENC", prefix,
                    "SCHED_ENC_" + pattern.format(prefix=prefix, cond=cond) + ".txt")
                nsga_pts, nsga_stats = load_valid_points(enc_sched, sm_path)
                nsga_front = pareto_front(nsga_pts) if nsga_pts is not None else []

                excluded = False
                reason = ""
                if mip_excluded:
                    excluded = True
                    reason = mip_exclude_reason
                elif nsga_stats.get("missing"):
                    excluded = True
                    reason = "NSGA_FILE_MISSING"
                elif nsga_stats["n_valid"] == 0:
                    excluded = True
                    reason = "NSGA_NO_FEASIBLE"

                gap_values = []
                unmatched = 0
                dominated = 0
                ms_gap_values = []
                ms_unmatched = 0
                hv_mip = hv_nsga = hv_ratio = ""
                c_mip_over_nsga = c_nsga_over_mip = ""
                if not excluded:
                    for m_ms, m_cost in mip_front:
                        c_nsga = min_cost_at_or_before(nsga_front, m_ms)
                        if c_nsga is None:
                            unmatched += 1
                            continue
                        gap_values.append((c_nsga - m_cost) / m_cost)
                    dominated = count_dominated_mip_points(mip_front, nsga_front)

                    # ① matched-cost makespan gap（工期超過率、コスト方向の対称版）
                    for m_ms, m_cost in mip_front:
                        m_nsga = min_ms_at_or_below_cost(nsga_front, m_cost)
                        if m_nsga is None:
                            ms_unmatched += 1
                            continue
                        ms_gap_values.append((m_nsga - m_ms) / m_ms)

                    # ② Hypervolume（MIP∪NSGAの全有効点でmin/max正規化、共通nadir=(1.1,1.1)）
                    union_pts = list(mip_pts) + list(nsga_pts)
                    if union_pts:
                        ms_lo = min(p[0] for p in union_pts); ms_hi = max(p[0] for p in union_pts)
                        c_lo = min(p[1] for p in union_pts); c_hi = max(p[1] for p in union_pts)
                        ref = (1.1, 1.1)
                        mip_front_n = normalize_front(mip_front, ms_lo, ms_hi, c_lo, c_hi)
                        nsga_front_n = normalize_front(nsga_front, ms_lo, ms_hi, c_lo, c_hi)
                        hv_mip = hypervolume_2d(mip_front_n, ref)
                        hv_nsga = hypervolume_2d(nsga_front_n, ref)
                        hv_ratio = (hv_nsga / hv_mip) if hv_mip > 0 else ""

                    # ③ C-metric（弱支配カバレッジ）
                    c_mip_over_nsga = c_metric(mip_front, nsga_front)
                    c_nsga_over_mip = c_metric(nsga_front, mip_front)
                    if c_mip_over_nsga is None:
                        c_mip_over_nsga = ""
                    if c_nsga_over_mip is None:
                        c_nsga_over_mip = ""

                row = {
                    "instance": prefix,
                    "condition": cond,
                    "method": method,
                    "excluded": excluded,
                    "exclude_reason": reason,
                    "gap_mean": (sum(gap_values) / len(gap_values)) if gap_values else "",
                    "gap_max": max(gap_values) if gap_values else "",
                    "n_points": len(gap_values),
                    "unmatched_mip_points": unmatched,
                    "ms_gap_mean": (sum(ms_gap_values) / len(ms_gap_values)) if ms_gap_values else "",
                    "ms_gap_max": max(ms_gap_values) if ms_gap_values else "",
                    "ms_unmatched_points": ms_unmatched,
                    "hv_mip": hv_mip,
                    "hv_nsga": hv_nsga,
                    "hv_ratio": hv_ratio,
                    "c_mip_over_nsga": c_mip_over_nsga,
                    "c_nsga_over_mip": c_nsga_over_mip,
                    "mip_pf_size": len(mip_front),
                    "nsga_pf_size": len(nsga_front),
                    "dominated_points": dominated,
                    "mip_n_total": mip_stats.get("n_total", 0),
                    "mip_n_infeasible_filtered": mip_stats.get("n_infeasible", 0),
                    "mip_n_prec_filtered": mip_stats.get("n_prec_filtered", 0),
                    "nsga_n_total": nsga_stats.get("n_total", 0),
                    "nsga_n_infeasible_filtered": nsga_stats.get("n_infeasible", 0),
                    "nsga_n_prec_filtered": nsga_stats.get("n_prec_filtered", 0),
                }
                rows.append(row)

    fieldnames = list(rows[0].keys())
    csv_path = os.path.join(out_dir, "gap_summary.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)
    print(f"[WRITE] {csv_path}  ({len(rows)} rows)")

    # ---- distribution summary (method x condition), 3指標分 ----
    dist_path = os.path.join(out_dir, "gap_distribution_summary.csv")

    # 指標名 -> (行のフィールド名, 高いほど悪いか)
    METRIC_FIELDS = {
        "cost_gap": "gap_mean",        # 現行: matched-makespan gap（コスト超過率）
        "ms_gap": "ms_gap_mean",       # 新規: matched-cost makespan gap（工期超過率）
        "hv_ratio": "hv_ratio",        # 新規: Hypervolume比（1.0が上限、高いほど良い）
    }

    no_match_rows = []
    for r in rows:
        if r["excluded"]:
            continue
        if r["gap_mean"] == "":
            no_match_rows.append(r)

    dist_rows = []
    for metric_name, field in METRIC_FIELDS.items():
        grouped = defaultdict(list)
        for r in rows:
            if r["excluded"] or r[field] == "":
                continue
            grouped[(r["method"], r["condition"])].append(float(r[field]))
        for (method, cond), vals in sorted(grouped.items()):
            if not vals:
                continue
            avg = sum(vals) / len(vals)
            med = st.median(vals)
            p90 = sorted(vals)[max(0, int(round(0.90 * (len(vals) - 1))))]
            mx = max(vals)
            sd = st.pstdev(vals) if len(vals) > 1 else 0.0
            if metric_name == "hv_ratio":
                n_notable = sum(1 for v in vals if v < 0.99)  # HV比が1%超劣る
            else:
                n_notable = sum(1 for v in vals if v > 0.01)  # gapが1%超
            dist_rows.append([metric_name, method, cond, len(vals),
                               f"{avg:.5f}", f"{med:.5f}", f"{p90:.5f}", f"{mx:.5f}",
                               f"{sd:.5f}", n_notable])

    with open(dist_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["metric", "method", "condition", "n_instances", "mean", "median",
                     "p90", "max", "std", "n_notable"])
        w.writerows(dist_rows)
    print(f"[WRITE] {dist_path}")

    # ---- summary to stdout: exclusions ----
    excl = [r for r in rows if r["excluded"]]
    if no_match_rows:
        print(f"\n[NO MATCHED POINTS] {len(no_match_rows)} non-excluded rows had zero matched "
              f"MIP points (all MIP points unmatched); excluded from distribution stats:")
        for r in no_match_rows:
            print(f"  {r['method']:<12} {r['instance']:<10} {r['condition']}  "
                  f"unmatched_mip_points={r['unmatched_mip_points']}")

    print(f"\n[EXCLUDED] {len(excl)} / {len(rows)} rows excluded")
    reason_cnt = defaultdict(int)
    for r in excl:
        reason_cnt[r["exclude_reason"]] += 1
    for reason, cnt in sorted(reason_cnt.items()):
        print(f"  {reason}: {cnt}")

    nsga_no_feasible = [r for r in excl if r["exclude_reason"] == "NSGA_NO_FEASIBLE"]
    if nsga_no_feasible:
        print("\n[NSGA_NO_FEASIBLE detail] (method, instance, condition)")
        for r in nsga_no_feasible:
            print(f"  {r['method']:<12} {r['instance']:<10} {r['condition']}")

    dom = [r for r in rows if not r["excluded"] and r["dominated_points"] > 0]
    print(f"\n[DOMINATION ANOMALY] {len(dom)} rows where NSGA strictly dominates an MIP point")
    for r in dom:
        print(f"  {r['method']:<12} {r['instance']:<10} {r['condition']}  dominated={r['dominated_points']}")

    high_phantom = [r for r in rows if not r["excluded"] and r["nsga_n_total"] > 0
                    and (r["nsga_n_prec_filtered"] + r["nsga_n_infeasible_filtered"]) / r["nsga_n_total"] > 0.05]
    print(f"\n[HIGH PHANTOM RATE >5%] {len(high_phantom)} rows")
    for r in high_phantom[:30]:
        rate = (r["nsga_n_prec_filtered"] + r["nsga_n_infeasible_filtered"]) / r["nsga_n_total"]
        print(f"  {r['method']:<12} {r['instance']:<10} {r['condition']}  "
              f"filtered={r['nsga_n_prec_filtered']+r['nsga_n_infeasible_filtered']}/{r['nsga_n_total']} ({rate:.1%})")

    # ---- 新指標: unmatched（両方向）・HV比・C-metric の要約 ----
    considered = [r for r in rows if not r["excluded"]]
    total_cost_unmatched = sum(r["unmatched_mip_points"] for r in considered)
    total_ms_unmatched = sum(r["ms_unmatched_points"] for r in considered)
    total_mip_pts = sum(r["mip_pf_size"] for r in considered)
    rows_cost_unmatched = sum(1 for r in considered if r["unmatched_mip_points"] > 0)
    rows_ms_unmatched = sum(1 for r in considered if r["ms_unmatched_points"] > 0)
    print(f"\n[UNMATCHED] cost方向(現行): {total_cost_unmatched}/{total_mip_pts} points, "
          f"{rows_cost_unmatched}/{len(considered)} rows")
    print(f"[UNMATCHED] 工期方向(新規): {total_ms_unmatched}/{total_mip_pts} points, "
          f"{rows_ms_unmatched}/{len(considered)} rows")
    by_method_cost = defaultdict(int); by_method_ms = defaultdict(int)
    for r in considered:
        if r["unmatched_mip_points"] > 0:
            by_method_cost[r["method"]] += 1
        if r["ms_unmatched_points"] > 0:
            by_method_ms[r["method"]] += 1
    print("  cost方向 該当行数(method別):", dict(by_method_cost))
    print("  工期方向 該当行数(method別):", dict(by_method_ms))

    hv_vals = [r for r in considered if r["hv_ratio"] != ""]
    print(f"\n[HV RATIO] {len(hv_vals)} rows with valid hv_ratio")
    by_method_hv = defaultdict(list)
    for r in hv_vals:
        by_method_hv[r["method"]].append(float(r["hv_ratio"]))
    for method, vals in sorted(by_method_hv.items()):
        print(f"  {method:<12} mean={sum(vals)/len(vals):.5f}  median={st.median(vals):.5f}  "
              f"min={min(vals):.5f}")

    c_anomaly = [r for r in considered
                 if r["c_nsga_over_mip"] != "" and float(r["c_nsga_over_mip"]) > 0.05]
    print(f"\n[C-METRIC ANOMALY] {len(c_anomaly)} rows with C(NSGA,MIP) > 5% "
          f"(NSGAがMIPを弱支配する割合が高い＝要確認)")
    for r in c_anomaly[:20]:
        print(f"  {r['method']:<12} {r['instance']:<10} {r['condition']}  "
              f"C(NSGA,MIP)={float(r['c_nsga_over_mip']):.3f}")


if __name__ == "__main__":
    main()
