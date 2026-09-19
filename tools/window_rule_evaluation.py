#!/usr/bin/env python3
"""
window_rule_evaluation.py

window_representability_analysis.py が出力した analysis/gap48/window_w_needed_raw.csv
（MIP解の各活動の必要窓幅 w_needed）を読み、候補窓ルールごとの表現可能率を計算する。
GAは実行しない（計算のみ）。

出力:
  analysis/gap48/window_rule_summary.csv        ルール×(instance,condition,front位置) の完全表現可能率
  analysis/gap48/window_rule_by_position.csv     ルール×front位置バケット の集計
  analysis/gap48/window_needed_vs_duration.csv   w_needed と duration の相関・回帰統計
"""
import csv
import os
import statistics

BD = "."


def load_rows():
    path = os.path.join(BD, "analysis", "gap48", "window_w_needed_raw.csv")
    with open(path, encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        r["duration"] = int(r["duration"])
        r["w_needed"] = int(r["w_needed"])
        r["T"] = int(r["T"])
        r["pos_val"] = float(r["pos_val"])
        r["reproducible_unbounded"] = (r["reproducible_unbounded"] == "True")
    return rows


# ---- 候補ルール ----
def rules():
    out = {}
    for alpha in (1.5, 3, 5, 10, 20):
        out[f"MaxShiftDur_a{alpha}"] = (lambda d, T, a=alpha: a * d)
    # beta=0.5 は現行 production の実際値（main_NSGAII_RCPSP_MaxShiftDur.cpp 実測確認済み）。
    # さらに広げた場合にどこで飽和するかを 0.75/1.0/1.5/2.0 まで追加探索する。
    for beta in (0.05, 0.1, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0):
        out[f"Hybrid_a1.5_b{beta}"] = (lambda d, T, b=beta: max(1.5 * d, b * T))
    for frac, name in [(1/8, "T8"), (1/4, "T4"), (1/2, "T2"), (1.0, "T1"), (1.5, "T1.5"), (2.0, "T2.0x")]:
        out[f"MaxShift_{name}"] = (lambda d, T, f=frac: f * T)
    return out


def main():
    rows = load_rows()
    RULES = rules()

    # グループ化: (instance,condition,pos_label) -> list of rows(各job)
    groups = {}
    for r in rows:
        key = (r["instance"], r["condition"], r["pos_label"], r["pos_val"])
        groups.setdefault(key, []).append(r)

    # ---- ルール別サマリ: 各(instance,cond,pos)点で全ジョブ表現可能か ----
    summary_rows = []
    for (inst, cond, pos_label, pos_val), grp in groups.items():
        n_jobs = len(grp)
        n_unbounded_ok = sum(1 for r in grp if r["reproducible_unbounded"])
        rec = {"instance": inst, "condition": cond, "pos_label": pos_label,
               "pos_val": round(pos_val, 4), "n_jobs": n_jobs,
               "unbounded_reproducible_rate": round(100.0 * n_unbounded_ok / n_jobs, 1)}
        for rname, capfn in RULES.items():
            ok_jobs = 0
            for r in grp:
                if not r["reproducible_unbounded"]:
                    continue  # argmin自体が無理なら窓を広げても無理
                cap = capfn(r["duration"], r["T"])
                if r["w_needed"] <= cap + 1e-9:
                    ok_jobs += 1
            job_rate = 100.0 * ok_jobs / n_jobs
            full_ok = (ok_jobs == n_jobs)
            rec[f"{rname}_job_rate"] = round(job_rate, 1)
            rec[f"{rname}_full"] = int(full_ok)
        summary_rows.append(rec)

    out_dir = os.path.join(BD, "analysis", "gap48")
    os.makedirs(out_dir, exist_ok=True)
    fieldnames = list(summary_rows[0].keys())
    with open(os.path.join(out_dir, "window_rule_summary.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(summary_rows)
    print(f"[WRITE] window_rule_summary.csv ({len(summary_rows)} rows)")

    # ---- front位置バケット別の集計 (endpoint vs mid) ----
    def bucket(pos_label):
        return "endpoint" if pos_label.startswith("endpoint") else "mid"

    by_pos = {}
    for rec in summary_rows:
        b = bucket(rec["pos_label"])
        by_pos.setdefault(b, []).append(rec)

    pos_rows = []
    for b, recs in by_pos.items():
        row = {"bucket": b, "n_points": len(recs),
               "unbounded_full_rate": round(100.0 * sum(1 for r in recs if r["unbounded_reproducible_rate"] == 100.0) / len(recs), 1)}
        for rname in RULES:
            full_rate = 100.0 * sum(r[f"{rname}_full"] for r in recs) / len(recs)
            job_rate_mean = statistics.mean(r[f"{rname}_job_rate"] for r in recs)
            row[f"{rname}_full_rate"] = round(full_rate, 1)
            row[f"{rname}_job_rate_mean"] = round(job_rate_mean, 1)
        pos_rows.append(row)

    with open(os.path.join(out_dir, "window_rule_by_position.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(pos_rows[0].keys()))
        w.writeheader()
        w.writerows(pos_rows)
    print(f"[WRITE] window_rule_by_position.csv ({len(pos_rows)} rows)")

    # ---- 全体（全点合算）でのルール別 完全表現可能率 ----
    print("\n=== Overall full-representability rate per rule (all points) ===")
    n_total = len(summary_rows)
    print(f"unbounded (argmin only): {100.0*sum(1 for r in summary_rows if r['unbounded_reproducible_rate']==100.0)/n_total:.1f}%")
    for rname in RULES:
        rate = 100.0 * sum(r[f"{rname}_full"] for r in summary_rows) / n_total
        print(f"{rname}: {rate:.1f}%")

    # ---- w_needed vs duration の相関分析 ----
    wn = [r["w_needed"] for r in rows if r["reproducible_unbounded"]]
    dur = [r["duration"] for r in rows if r["reproducible_unbounded"]]
    wn_over_dur = [w / d for w, d in zip(wn, dur) if d > 0]
    wn_over_T = [r["w_needed"] / r["T"] for r in rows if r["reproducible_unbounded"] and r["T"] > 0]

    if len(wn) > 1:
        mean_w, mean_d = statistics.mean(wn), statistics.mean(dur)
        cov = sum((w - mean_w) * (d - mean_d) for w, d in zip(wn, dur)) / len(wn)
        sw = statistics.pstdev(wn); sd = statistics.pstdev(dur)
        pearson = cov / (sw * sd) if sw > 0 and sd > 0 else float("nan")
    else:
        pearson = float("nan")

    corr_path = os.path.join(out_dir, "window_needed_vs_duration.csv")
    with open(corr_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["metric", "value"])
        w.writerow(["n_reproducible_jobs", len(wn)])
        w.writerow(["pearson_w_needed_vs_duration", round(pearson, 4)])
        w.writerow(["w_needed_over_duration_mean", round(statistics.mean(wn_over_dur), 3)])
        w.writerow(["w_needed_over_duration_median", round(statistics.median(wn_over_dur), 3)])
        w.writerow(["w_needed_over_duration_max", round(max(wn_over_dur), 3)])
        w.writerow(["w_needed_over_duration_p90", round(statistics.quantiles(wn_over_dur, n=10)[8], 3)])
        w.writerow(["w_needed_over_T_mean", round(statistics.mean(wn_over_T), 4)])
        w.writerow(["w_needed_over_T_median", round(statistics.median(wn_over_T), 4)])
        w.writerow(["w_needed_over_T_max", round(max(wn_over_T), 4)])

    print(f"\n[WRITE] {corr_path}")
    print(f"Pearson(w_needed, duration) = {pearson:.4f}")
    print(f"w_needed/duration: mean={statistics.mean(wn_over_dur):.2f} median={statistics.median(wn_over_dur):.2f} max={max(wn_over_dur):.2f}")
    print(f"w_needed/T: mean={statistics.mean(wn_over_T):.4f} median={statistics.median(wn_over_T):.4f} max={max(wn_over_T):.4f}")

    # front位置による必要窓の違い（w_needed/T の平均、位置ラベル別）
    print("\n=== w_needed/T mean by front position ===")
    by_label = {}
    for r in rows:
        if not r["reproducible_unbounded"] or r["T"] <= 0:
            continue
        by_label.setdefault(r["pos_label"], []).append(r["w_needed"] / r["T"])
    for label in sorted(by_label):
        vals = by_label[label]
        print(f"{label}: mean={statistics.mean(vals):.4f}  n={len(vals)}")


if __name__ == "__main__":
    main()
