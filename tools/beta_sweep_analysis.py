#!/usr/bin/env python3
"""
beta_sweep_analysis.py

run_beta_sweep.sh が生成した SCHED_ENC_*_MaxShiftDur_<tag>.txt 群を集計する。
各 (instance, beta, rep) について、6条件それぞれの HV比・コストgap・工期gap を
gap48_analysis.py の既存ロジックで計算し、beta別に平均・ノイズ床(反復間レンジ)
を出す。事前登録済み基準 (.beta_preregistration_20260814.md) に照らして採否を判定。
"""
import csv
import importlib.util
import math
import os
import statistics


def mean_skip_nan(xs):
    vals = [x for x in xs if not (isinstance(x, float) and math.isnan(x))]
    return statistics.mean(vals) if vals else float("nan")

HERE = os.path.dirname(os.path.abspath(__file__))
BD = "."

spec = importlib.util.spec_from_file_location("g48", os.path.join(HERE, "gap48_analysis.py"))
g48 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g48)

INSTANCES = ["j3034_1", "j309_1", "j3031_1", "j3041_1"]
CONDITIONS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]

VARIANTS = {
    0.50: [f"b0_50_rep{r}" for r in (1, 2, 3, 4)],
    0.10: [f"b0_1_rep{r}" for r in (1, 2)],
    0.25: [f"b0_25_rep{r}" for r in (1, 2)],
    0.75: [f"b0_75_rep{r}" for r in (1, 2)],
}


def compute_point(prefix, cond, tag):
    sm_path = os.path.join(BD, "j30.sm", f"{prefix}.sm")
    mip_sched = os.path.join(BD, "results", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
    if not os.path.exists(mip_sched):
        return None
    mip_pts, _ = g48.load_valid_points(mip_sched, sm_path)
    if not mip_pts:
        return None
    mip_front = g48.pareto_front(mip_pts)
    if len(mip_front) < 2:
        return None

    enc_sched = os.path.join(BD, "results", "FUN_ENC", prefix,
                              f"SCHED_ENC_{prefix}_{cond}_MaxShiftDur_{tag}.txt")
    if not os.path.exists(enc_sched):
        return None
    nsga_pts, _ = g48.load_valid_points(enc_sched, sm_path)
    nsga_front = g48.pareto_front(nsga_pts) if nsga_pts else []
    if not nsga_front:
        return None

    union_pts = mip_front + nsga_front
    ms_lo = min(p[0] for p in union_pts); ms_hi = max(p[0] for p in union_pts)
    c_lo = min(p[1] for p in union_pts); c_hi = max(p[1] for p in union_pts)
    ref = (1.1, 1.1)
    mip_front_n = g48.normalize_front(mip_front, ms_lo, ms_hi, c_lo, c_hi)
    nsga_front_n = g48.normalize_front(nsga_front, ms_lo, ms_hi, c_lo, c_hi)
    hv_mip = g48.hypervolume_2d(mip_front_n, ref)
    hv_nsga = g48.hypervolume_2d(nsga_front_n, ref)
    hv_ratio = hv_nsga / hv_mip if hv_mip > 0 else float("nan")

    cost_gaps = []
    for m_ms, m_cost in mip_front:
        n_cost = g48.min_cost_at_or_before(nsga_front, m_ms)
        if n_cost is not None:
            cost_gaps.append((n_cost - m_cost) / m_cost)
    ms_gaps = []
    for m_ms, m_cost in mip_front:
        n_ms = g48.min_ms_at_or_below_cost(nsga_front, m_cost)
        if n_ms is not None:
            ms_gaps.append((n_ms - m_ms) / m_ms)

    return {
        "hv_ratio": hv_ratio,
        "cost_gap_mean": statistics.mean(cost_gaps) if cost_gaps else float("nan"),
        "ms_gap_mean": statistics.mean(ms_gaps) if ms_gaps else float("nan"),
        "nsga_pf_size": len(nsga_front),
    }


def main():
    rows = []
    for beta, tags in VARIANTS.items():
        for tag in tags:
            for prefix in INSTANCES:
                for cond in CONDITIONS:
                    pt = compute_point(prefix, cond, tag)
                    if pt is None:
                        continue
                    rows.append({"beta": beta, "tag": tag, "instance": prefix, "condition": cond, **pt})

    out_dir = os.path.join(BD, "analysis", "gap48")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "beta_sweep_raw.csv")
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"[WRITE] {out_path} ({len(rows)} rows)")

    # ---- rep単位で平均(全instance×condition) -> beta別に反復間レンジ(ノイズ床)も出す ----
    print("\n=== per-rep means (averaged over instance x condition) ===")
    by_beta_tag = {}
    for r in rows:
        key = (r["beta"], r["tag"])
        by_beta_tag.setdefault(key, []).append(r)

    rep_summary = []
    for (beta, tag), grp in sorted(by_beta_tag.items()):
        hv = mean_skip_nan(g["hv_ratio"] for g in grp)
        cg = mean_skip_nan(g["cost_gap_mean"] for g in grp)
        mg = mean_skip_nan(g["ms_gap_mean"] for g in grp)
        n = len(grp)
        rep_summary.append({"beta": beta, "tag": tag, "n_points": n,
                             "hv_ratio_mean": hv, "cost_gap_mean": cg, "ms_gap_mean": mg})
        print(f"beta={beta:<5} tag={tag:<12} n={n:2d}  HV={hv:.4f}  cost_gap={cg*100:.2f}%  ms_gap={mg*100:.2f}%")

    rs_path = os.path.join(out_dir, "beta_sweep_rep_summary.csv")
    with open(rs_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rep_summary[0].keys()))
        w.writeheader()
        w.writerows(rep_summary)
    print(f"[WRITE] {rs_path}")

    # ---- beta別: 反復平均 + ノイズ床(反復間レンジ) ----
    print("\n=== beta-level summary (mean across reps, range = noise floor) ===")
    by_beta = {}
    for rs in rep_summary:
        by_beta.setdefault(rs["beta"], []).append(rs)

    final_rows = []
    for beta, reps in sorted(by_beta.items()):
        hvs = [r["hv_ratio_mean"] for r in reps if not math.isnan(r["hv_ratio_mean"])]
        cgs = [r["cost_gap_mean"] for r in reps if not math.isnan(r["cost_gap_mean"])]
        mgs = [r["ms_gap_mean"] for r in reps if not math.isnan(r["ms_gap_mean"])]
        row = {
            "beta": beta, "n_reps": len(reps),
            "hv_mean": statistics.mean(hvs) if hvs else float("nan"),
            "hv_range": (max(hvs) - min(hvs)) if len(hvs) > 1 else 0.0,
            "cost_gap_mean": statistics.mean(cgs) if cgs else float("nan"),
            "cost_gap_range": (max(cgs) - min(cgs)) if len(cgs) > 1 else 0.0,
            "ms_gap_mean": statistics.mean(mgs) if mgs else float("nan"),
            "ms_gap_range": (max(mgs) - min(mgs)) if len(mgs) > 1 else 0.0,
        }
        final_rows.append(row)
        print(f"beta={beta:<5} n_reps={row['n_reps']}  "
              f"HV={row['hv_mean']:.4f} (range={row['hv_range']:.4f})  "
              f"cost_gap={row['cost_gap_mean']*100:.2f}% (range={row['cost_gap_range']*100:.2f}pt)  "
              f"ms_gap={row['ms_gap_mean']*100:.2f}% (range={row['ms_gap_range']*100:.2f}pt)")

    fb_path = os.path.join(out_dir, "beta_sweep_final_summary.csv")
    with open(fb_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(final_rows[0].keys()))
        w.writeheader()
        w.writerows(final_rows)
    print(f"[WRITE] {fb_path}")

    # ---- 事前登録基準での採否判定 ----
    print("\n=== Pre-registered go/no-go (vs beta=0.5 baseline noise floor) ===")
    baseline = next(r for r in final_rows if r["beta"] == 0.50)
    hv_floor = baseline["hv_range"]
    cg_floor = baseline["cost_gap_range"]
    print(f"baseline HV={baseline['hv_mean']:.4f}  noise_floor(HV)={hv_floor:.4f}  "
          f"noise_floor(cost_gap)={cg_floor*100:.2f}pt")
    for row in final_rows:
        if row["beta"] == 0.50:
            continue
        hv_delta = row["hv_mean"] - baseline["hv_mean"]
        cg_delta = row["cost_gap_mean"] - baseline["cost_gap_mean"]
        hv_pass = hv_delta > hv_floor
        cost_ok = cg_delta <= cg_floor
        verdict = "ADOPT-candidate" if (hv_pass and cost_ok) else "reject"
        print(f"beta={row['beta']:<5} HV_delta={hv_delta:+.4f} (floor={hv_floor:.4f}, pass={hv_pass})  "
              f"cost_gap_delta={cg_delta*100:+.2f}pt (floor={cg_floor*100:.2f}pt, ok={cost_ok})  "
              f"=> {verdict}")


if __name__ == "__main__":
    main()
