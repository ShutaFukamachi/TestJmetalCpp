#!/usr/bin/env python3
"""
window_representability_analysis.py

D2Repro.exe を使い、複数の (instance, condition, フロント位置) について
MIP解の各活動の必要窓幅 w_needed を収集し、候補となる窓ルール（MaxShiftDur型
alpha*d_j、ハイブリッド max(alpha*d_j, beta*T)、MaxShift型固定 T/8,T/4,T/2）
それぞれで「完全表現可能率」「活動単位の表現可能率」を計算する。
GAは一切実行しない（計算のみ）。

使い方: cmake-build-release から
  python ../tools/window_representability_analysis.py
"""
import csv
import importlib.util
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
BD = "."  # cmake-build-release がカレントディレクトリという前提

spec = importlib.util.spec_from_file_location("vcc", os.path.join(HERE, "verify_cost_consistency.py"))
vcc = importlib.util.module_from_spec(spec); spec.loader.exec_module(vcc)
spec2 = importlib.util.spec_from_file_location("g48", os.path.join(HERE, "gap48_analysis.py"))
g48 = importlib.util.module_from_spec(spec2); spec2.loader.exec_module(g48)

INSTANCES = ["j3034_1", "j309_1", "j3031_1", "j3041_1"]  # ms_gapが高い順に多様性を確保
CONDITIONS = [
    ("RR000_RV0", 0.00, 0),
    ("RR000_RV1", 0.00, 1),
    ("RR025_RV0", 0.25, 0),
    ("RR025_RV1", 0.25, 1),
    ("RR050_RV0", 0.50, 0),
    ("RR050_RV1", 0.50, 1),
]
D2REPRO_EXE = os.path.join(BD, "D2Repro.exe")


def get_horizon(durations, rr, rv):
    """RCPSP_Problem_MaxShift::getHorizon() と同一の決定論的計算式
    （容量値はRNG依存だがTのサイズ自体はrr/rvとdurationのみで決まる）。"""
    T_base = sum(durations)
    if rr > 0.0 or rv:
        factor = 3.5 if rv else (2.5 if rr >= 0.75 else 1.5)
        T = int(T_base * factor) + 20
    else:
        T = T_base
    return max(T, 1)


def find_starts(sols, ms, cost, tol=1e-6):
    for m, c, st in sols:
        if abs(m - ms) < tol and abs(c - cost) < 1e-3:
            return st
    # フォールバック: makespan一致優先
    for m, c, st in sols:
        if abs(m - ms) < tol:
            return st
    return None


def pick_front_positions(front):
    """端点2点＋中間部3点（makespan正規化位置で0.3/0.5/0.7に最も近い点）を選ぶ。
    返り値: [(pos_label, pos_value, ms, cost), ...]"""
    if len(front) < 2:
        return []
    ms_lo, ms_hi = front[0][0], front[-1][0]
    span = ms_hi - ms_lo if ms_hi > ms_lo else 1.0
    picks = [("endpoint_ms_min", 0.0, front[0][0], front[0][1]),
             ("endpoint_ms_max", 1.0, front[-1][0], front[-1][1])]
    for target in (0.3, 0.5, 0.7):
        best = min(front, key=lambda p: abs((p[0] - ms_lo) / span - target))
        pos = (best[0] - ms_lo) / span
        picks.append((f"mid_{target}", pos, best[0], best[1]))
    return picks


def run_d2repro(instance_file, rr, rv, starts):
    args = [D2REPRO_EXE,
            "--instance", instance_file,
            "--rr", str(rr),
            "--rv", "1" if rv else "0",
            "--strategy", "4",
            "--maxrounds", "10",
            "--starts", ",".join(str(s) for s in starts)]
    out = subprocess.run(args, capture_output=True, text=True, cwd=BD, timeout=120)
    return out.stdout


ROW_RE = re.compile(r"^(-?\d+) (-?\d+) (-?\d+) (-?\d+) (YES|no) (-?\d+) (-?\d+) (\S+) (\S+) (\S+)$")


def parse_test_b(stdout):
    """TEST B セクションの各行を (job, s_star, est, t_mak, reproducible, w_needed) にパース。"""
    lines = stdout.splitlines()
    try:
        start_idx = next(i for i, l in enumerate(lines) if l.startswith("=== TEST B"))
    except StopIteration:
        return None
    rows = []
    for l in lines[start_idx + 1:]:
        if l.startswith("[SUMMARY_B]"):
            break
        m = ROW_RE.match(l.strip())
        if not m:
            continue
        job = int(m.group(1))
        s_star = int(m.group(2))
        reproducible = (m.group(5) == "YES")
        w_needed = int(m.group(6))
        rows.append((job, s_star, reproducible, w_needed))
    return rows


def main():
    records = []  # 1 row per (instance,cond,frontpos,job)
    sm_dir = os.path.join(BD, "j30.sm")

    for prefix in INSTANCES:
        sm_path = os.path.join(sm_dir, f"{prefix}.sm")
        instance_file = f"j30.sm/{prefix}.sm"
        for cond, rr, rv in CONDITIONS:
            mip_sched = os.path.join(BD, "results", "FUN_MIP", prefix, f"SCHED_MIP_{prefix}_{cond}.txt")
            if not os.path.exists(mip_sched):
                continue
            n, nRes, dur, demand, sols = vcc.read_sched(mip_sched)
            mip_pts, _ = g48.load_valid_points(mip_sched, sm_path)
            if not mip_pts:
                continue
            front = g48.pareto_front(mip_pts)
            if len(front) < 2:
                continue
            T = get_horizon(dur, rr, rv)

            for pos_label, pos_val, ms, cost in pick_front_positions(front):
                starts = find_starts(sols, ms, cost)
                if starts is None:
                    print(f"[WARN] no matching starts for {prefix} {cond} ms={ms} cost={cost}")
                    continue
                stdout = run_d2repro(instance_file, rr, rv, starts)
                rows = parse_test_b(stdout)
                if rows is None:
                    print(f"[WARN] failed to parse D2Repro output for {prefix} {cond} {pos_label}")
                    continue
                for job, s_star, reproducible, w_needed in rows:
                    d = dur[job]
                    if d <= 0:
                        continue  # ダミー端点は除外
                    records.append({
                        "instance": prefix, "condition": cond, "rr": rr, "rv": int(rv),
                        "pos_label": pos_label, "pos_val": round(pos_val, 4),
                        "ms": ms, "cost": cost, "T": T,
                        "job": job, "duration": d, "s_star": s_star,
                        "reproducible_unbounded": reproducible, "w_needed": w_needed,
                    })
            print(f"[DONE] {prefix} {cond}")

    out_dir = os.path.join(BD, "analysis", "gap48")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "window_w_needed_raw.csv")
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(records[0].keys()))
        w.writeheader()
        w.writerows(records)
    print(f"[WRITE] {out_path}  ({len(records)} rows)")


if __name__ == "__main__":
    main()
