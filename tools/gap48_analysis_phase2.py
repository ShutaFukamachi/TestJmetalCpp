#!/usr/bin/env python3
"""
gap48_analysis_phase2.py — Phase2（A2シード注入）のA/B比較。7variant x 8インスタンス x 6条件。

results/FUN_ENC_phase2/<variant>/<prefix>/ にある MaxShiftDur結果を、
results/FUN_MIP/（不変）に対して比較する。gap48_analysis.py の関数を再利用。

出力: analysis/gap48/phase2_summary.csv, analysis/gap48/phase2_distribution.csv
実行: cmake-build-release から `python ../tools/gap48_analysis_phase2.py`
"""
import csv
import importlib.util
import os
import statistics as st
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g48", os.path.join(HERE, "gap48_analysis.py"))
g48 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g48)

INSTANCES = ["j3034_1", "j309_1", "j3038_1", "j3011_1", "j3022_1", "j3010_1", "j3019_1", "j3048_1"]
CONDS = g48.CONDS
VARIANTS = ["baseline_rep1", "baseline_rep2", "baseline_rep3", "baseline_rep4",
            "seed6_full", "seed12_full", "seed6_reduced"]


def compute_row(bd, prefix, cond, enc_dir, sm_path):
    mip_sched = os.path.join(bd, "results", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
    mip_pts, mip_stats = g48.load_valid_points(mip_sched, sm_path)
    mip_front = g48.pareto_front(mip_pts) if mip_pts is not None else []
    mip_excluded = mip_stats.get("missing") or mip_stats["n_valid"] == 0
    if mip_excluded:
        return dict(excluded=True, reason="MIP_INFEASIBLE")

    enc_sched = os.path.join(enc_dir, prefix, f"SCHED_ENC_{prefix}_{cond}_MaxShiftDur.txt")
    nsga_pts, nsga_stats = g48.load_valid_points(enc_sched, sm_path)
    nsga_front = g48.pareto_front(nsga_pts) if nsga_pts is not None else []
    if nsga_stats.get("missing") or nsga_stats["n_valid"] == 0:
        return dict(excluded=True, reason="NSGA_NO_FEASIBLE")

    gap_values, unmatched = [], 0
    for m_ms, m_cost in mip_front:
        c_nsga = g48.min_cost_at_or_before(nsga_front, m_ms)
        if c_nsga is None:
            unmatched += 1
            continue
        gap_values.append((c_nsga - m_cost) / m_cost)

    ms_gap_values, ms_unmatched = [], 0
    for m_ms, m_cost in mip_front:
        m_nsga = g48.min_ms_at_or_below_cost(nsga_front, m_cost)
        if m_nsga is None:
            ms_unmatched += 1
            continue
        ms_gap_values.append((m_nsga - m_ms) / m_ms)

    union_pts = list(mip_pts) + list(nsga_pts)
    hv_ratio = ""
    if union_pts:
        ms_lo = min(p[0] for p in union_pts); ms_hi = max(p[0] for p in union_pts)
        c_lo = min(p[1] for p in union_pts); c_hi = max(p[1] for p in union_pts)
        ref = (1.1, 1.1)
        mip_n = g48.normalize_front(mip_front, ms_lo, ms_hi, c_lo, c_hi)
        nsga_n = g48.normalize_front(nsga_front, ms_lo, ms_hi, c_lo, c_hi)
        hv_mip = g48.hypervolume_2d(mip_n, ref)
        hv_nsga = g48.hypervolume_2d(nsga_n, ref)
        hv_ratio = (hv_nsga / hv_mip) if hv_mip > 0 else ""

    mip_best_ms = min(p[0] for p in mip_front) if mip_front else None
    nsga_best_ms = min(p[0] for p in nsga_front) if nsga_front else None
    reached_mip_opt = (mip_best_ms is not None and nsga_best_ms is not None
                        and abs(mip_best_ms - nsga_best_ms) < 1e-6)

    return dict(
        excluded=False,
        gap_mean=(sum(gap_values) / len(gap_values)) if gap_values else "",
        ms_gap_mean=(sum(ms_gap_values) / len(ms_gap_values)) if ms_gap_values else "",
        hv_ratio=hv_ratio,
        mip_pf_size=len(mip_front),
        nsga_pf_size=len(nsga_front),
        reached_mip_opt=reached_mip_opt,
    )


def main():
    bd = "."
    sm_dir = os.path.join(bd, "j30.sm")
    rows = []
    for variant in VARIANTS:
        enc_dir = os.path.join(bd, "results", "FUN_ENC_phase2", variant)
        for prefix in INSTANCES:
            sm_path = os.path.join(sm_dir, f"{prefix}.sm")
            for cond in CONDS:
                r = compute_row(bd, prefix, cond, enc_dir, sm_path)
                row = {"variant": variant, "instance": prefix, "condition": cond}
                row.update(r)
                rows.append(row)

    out_dir = os.path.join(bd, "analysis", "gap48")
    os.makedirs(out_dir, exist_ok=True)
    summary_path = os.path.join(out_dir, "phase2_summary.csv")
    fieldnames = ["variant", "instance", "condition", "excluded", "reason",
                  "gap_mean", "ms_gap_mean", "hv_ratio",
                  "mip_pf_size", "nsga_pf_size", "reached_mip_opt"]
    with open(summary_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames, restval="")
        w.writeheader()
        w.writerows(rows)
    print(f"[WRITE] {summary_path}  ({len(rows)} rows)")

    # ---- variant別サマリ(cost_gap, ms_gap, hv_ratio) ----
    dist_path = os.path.join(out_dir, "phase2_distribution.csv")
    with open(dist_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["variant", "n_valid", "n_excluded", "n_mip_infeasible", "n_nsga_no_feasible",
                    "cost_gap_mean", "cost_gap_median",
                    "ms_gap_mean", "ms_gap_median", "hv_ratio_mean", "hv_ratio_median",
                    "n_reached_mip_opt"])
        for variant in VARIANTS:
            vrows = [r for r in rows if r["variant"] == variant]
            valid = [r for r in vrows if not r["excluded"]]
            excl = [r for r in vrows if r["excluded"]]
            n_mip_inf = sum(1 for r in excl if r["reason"] == "MIP_INFEASIBLE")
            n_nsga_nf = sum(1 for r in excl if r["reason"] == "NSGA_NO_FEASIBLE")
            cost_vals = [r["gap_mean"] for r in valid if r["gap_mean"] != ""]
            ms_vals = [r["ms_gap_mean"] for r in valid if r["ms_gap_mean"] != ""]
            hv_vals = [r["hv_ratio"] for r in valid if r["hv_ratio"] != ""]
            n_reached = sum(1 for r in valid if r.get("reached_mip_opt"))
            w.writerow([
                variant, len(valid), len(excl), n_mip_inf, n_nsga_nf,
                f"{sum(cost_vals)/len(cost_vals)*100:.4f}" if cost_vals else "",
                f"{st.median(cost_vals)*100:.4f}" if cost_vals else "",
                f"{sum(ms_vals)/len(ms_vals)*100:.4f}" if ms_vals else "",
                f"{st.median(ms_vals)*100:.4f}" if ms_vals else "",
                f"{sum(hv_vals)/len(hv_vals):.5f}" if hv_vals else "",
                f"{st.median(hv_vals):.5f}" if hv_vals else "",
                n_reached,
            ])
    print(f"[WRITE] {dist_path}")

    with open(dist_path, encoding="utf-8") as f:
        print(f.read())


if __name__ == "__main__":
    main()
