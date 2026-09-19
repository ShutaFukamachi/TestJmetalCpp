#!/usr/bin/env python3
"""
gap48_analysis_a1.py — A1 priority-rule シード ON/OFF の A/B比較（8インスタンス限定）。

results/FUN_ENC_a1on/ にある A1=ON の結果を、results/FUN_MIP/（MIPは不変）と
results/FUN_ENC/（A1=OFFの現行公式データ）に対して比較する。
gap48_analysis.py の各種関数をそのまま再利用する。

出力: analysis/gap48/a1_ab_summary.csv
実行: cmake-build-release から `python ../tools/gap48_analysis_a1.py`
"""
import csv
import importlib.util
import os

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g48", os.path.join(HERE, "gap48_analysis.py"))
g48 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g48)

INSTANCES = ["j3034_1", "j309_1", "j3022_1", "j3031_1", "j3011_1", "j3013_1", "j3026_1", "j3038_1"]
CONDS = g48.CONDS
METHODS = g48.METHODS


def compute_row(bd, prefix, cond, method, enc_root, sm_path):
    mip_sched = os.path.join(bd, "results", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
    mip_pts, mip_stats = g48.load_valid_points(mip_sched, sm_path)
    mip_front = g48.pareto_front(mip_pts) if mip_pts is not None else []
    mip_excluded = mip_stats.get("missing") or mip_stats["n_valid"] == 0
    if mip_excluded:
        return None  # MIP_INFEASIBLE 等はA/B比較から除外（既存gap48_analysisと同じ扱い）

    enc_sched = os.path.join(
        enc_root, prefix, "SCHED_ENC_" + METHODS[method].format(prefix=prefix, cond=cond) + ".txt")
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

    return dict(
        excluded=False,
        gap_mean=(sum(gap_values) / len(gap_values)) if gap_values else "",
        unmatched_mip_points=unmatched,
        ms_gap_mean=(sum(ms_gap_values) / len(ms_gap_values)) if ms_gap_values else "",
        ms_unmatched_points=ms_unmatched,
        hv_ratio=hv_ratio,
        mip_pf_size=len(mip_front),
        nsga_pf_size=len(nsga_front),
    )


def main():
    bd = "."
    sm_dir = os.path.join(bd, "j30.sm")
    rows = []
    for prefix in INSTANCES:
        sm_path = os.path.join(sm_dir, f"{prefix}.sm")
        for cond in CONDS:
            for method in METHODS:
                for tag, enc_root in [("A1_OFF", os.path.join(bd, "results", "FUN_ENC")),
                                        ("A1_ON", os.path.join(bd, "results", "FUN_ENC_a1on"))]:
                    r = compute_row(bd, prefix, cond, method, enc_root, sm_path)
                    row = {"instance": prefix, "condition": cond, "method": method, "a1": tag}
                    if r is None:
                        row.update(excluded=True, reason="MIP_INFEASIBLE",
                                    gap_mean="", unmatched_mip_points="",
                                    ms_gap_mean="", ms_unmatched_points="",
                                    hv_ratio="", mip_pf_size="", nsga_pf_size="")
                    else:
                        row.update(reason=r.get("reason", ""), **{k: v for k, v in r.items() if k != "reason"})
                    rows.append(row)

    out_dir = os.path.join(bd, "analysis", "gap48")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "a1_ab_summary.csv")
    fieldnames = list(rows[0].keys())
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)
    print(f"[WRITE] {out_path}  ({len(rows)} rows)")

    # ---- サマリ表示 ----
    import statistics as st
    for method in METHODS:
        print(f"\n=== {method} ===")
        for tag in ["A1_OFF", "A1_ON"]:
            vals_gap = [r["gap_mean"] for r in rows
                        if r["method"] == method and r["a1"] == tag
                        and not r["excluded"] and r["gap_mean"] != ""]
            vals_ms = [r["ms_gap_mean"] for r in rows
                       if r["method"] == method and r["a1"] == tag
                       and not r["excluded"] and r["ms_gap_mean"] != ""]
            vals_hv = [r["hv_ratio"] for r in rows
                       if r["method"] == method and r["a1"] == tag
                       and not r["excluded"] and r["hv_ratio"] != ""]
            n_excl = sum(1 for r in rows if r["method"] == method and r["a1"] == tag and r["excluded"])
            print(f"  {tag:<8} cost_gap mean={sum(vals_gap)/len(vals_gap)*100:.3f}%"
                  f" (n={len(vals_gap)})  "
                  f"ms_gap mean={sum(vals_ms)/len(vals_ms)*100:.3f}% (n={len(vals_ms)})  "
                  f"hv_ratio mean={sum(vals_hv)/len(vals_hv):.4f} (n={len(vals_hv)})  "
                  f"excluded={n_excl}")


if __name__ == "__main__":
    main()
