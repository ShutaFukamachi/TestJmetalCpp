"""
analyze_diagnosis.py
====================
RCPSP MaxShift エンコーディング診断スクリプト

タスク1: generation_log.csv の可視化
タスク3: ガントチャート (SCHED_MS_*.txt から matplotlib で生成)
タスク4: コスト分析・MaxShift vs SchedObj 比較

使い方:
  python analyze_diagnosis.py [prefix] [cond_tag]
  例:  python analyze_diagnosis.py j301_1 RR000_RV0

  prefix:   インスタンスプレフィックス (デフォルト: j301_1)
  cond_tag: 条件タグ (デフォルト: RR000_RV0)
"""

import sys
import os
import csv
import glob
import math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

# -----------------------------------------------------------------------
# ユーティリティ
# -----------------------------------------------------------------------

def read_fun(path):
    """FUN ファイル読み込み: [(makespan, cost), ...]"""
    pts = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split()
            if len(parts) >= 2:
                try:
                    pts.append((float(parts[0]), float(parts[1])))
                except ValueError:
                    continue
    return pts


def read_sched(path):
    """
    SCHED_MS_*.txt 読み込み
    フォーマット:
      line1: nJobs nRes
      line2: duration[0..n-1]
      line3..n+2: demand[j][0..nRes-1]
      line n+3: capacity[0..nRes-1]
      line n+4: T_cap  (0 なら時間変動なし)
      (T_cap>0 なら続けて nRes 行の容量列)
      line ...: nSolutions
      各解: makespan cost start[0] start[1] ...
    """
    with open(path) as f:
        lines = [l.rstrip() for l in f if l.strip()]

    idx = 0
    nJobs, nRes = map(int, lines[idx].split()); idx += 1
    dur = list(map(int, lines[idx].split())); idx += 1

    demand = []
    for _ in range(nJobs):
        demand.append(list(map(int, lines[idx].split()))); idx += 1

    cap = list(map(int, lines[idx].split())); idx += 1

    T_cap = int(lines[idx]); idx += 1
    cap_t = []
    if T_cap > 0:
        for _ in range(nRes):
            cap_t.append(list(map(int, lines[idx].split()))); idx += 1

    nSol = int(lines[idx]); idx += 1

    solutions = []
    for _ in range(nSol):
        parts = list(map(float, lines[idx].split())); idx += 1
        ms   = int(parts[0])
        cost = parts[1]
        starts = list(map(int, parts[2:]))
        solutions.append({"makespan": ms, "cost": cost, "starts": starts})

    return {
        "nJobs": nJobs, "nRes": nRes, "dur": dur,
        "demand": demand, "cap": cap, "cap_t": cap_t,
        "solutions": solutions
    }


def read_generation_log(path):
    """generation_log.csv 読み込み。複数 run を辞書のリストで返す。"""
    runs = {}
    current_run = None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("run_id"):
                continue  # header
            parts = line.split(",")
            if len(parts) < 7:
                continue
            run_id  = int(parts[0])
            gen     = int(parts[1])
            avg_ms  = float(parts[2])
            min_ms  = float(parts[3])
            cnt_0   = int(parts[4])
            min_s   = int(parts[5])
            max_s   = int(parts[6])
            if run_id not in runs:
                runs[run_id] = {"gen": [], "avg_shift": [], "min_makespan": [],
                                "cnt_allzero": [], "min_shift": [], "max_shift": []}
            runs[run_id]["gen"].append(gen)
            runs[run_id]["avg_shift"].append(avg_ms)
            runs[run_id]["min_makespan"].append(min_ms)
            runs[run_id]["cnt_allzero"].append(cnt_0)
            runs[run_id]["min_shift"].append(min_s)
            runs[run_id]["max_shift"].append(max_s)
    return runs

# -----------------------------------------------------------------------
# タスク1: generation_log.csv の可視化
# -----------------------------------------------------------------------

def plot_generation_log(log_path, out_dir="."):
    if not os.path.exists(log_path):
        print(f"[Task1] {log_path} が見つかりません。スキップします。")
        return

    runs = read_generation_log(log_path)
    if not runs:
        print("[Task1] ログデータが空です。")
        return

    fig, axes = plt.subplots(3, 1, figsize=(10, 12), sharex=False)

    colors = plt.cm.tab10.colors

    for i, (run_id, data) in enumerate(sorted(runs.items())):
        c = colors[i % len(colors)]
        label = f"Run {run_id}"
        axes[0].plot(data["gen"], data["avg_shift"], color=c, label=label)
        axes[1].plot(data["gen"], data["min_makespan"], color=c, label=label)
        axes[2].plot(data["gen"], data["cnt_allzero"], color=c, label=label)

    axes[0].set_ylabel("avg max_shift")
    axes[0].set_title("max_shift の世代平均値の推移")
    axes[0].legend(fontsize=7)
    axes[0].grid(True, alpha=0.3)

    axes[1].set_ylabel("min makespan in population")
    axes[1].set_title("母集団中の makespan 最小値の推移")
    axes[1].legend(fontsize=7)
    axes[1].grid(True, alpha=0.3)

    axes[2].set_ylabel("count (all max_shift=0)")
    axes[2].set_xlabel("generation")
    axes[2].set_title("全 max_shift=0 の個体数の推移")
    axes[2].legend(fontsize=7)
    axes[2].grid(True, alpha=0.3)

    plt.tight_layout()
    out = os.path.join(out_dir, "generation_log_plot.png")
    plt.savefig(out, dpi=120)
    plt.close()
    print(f"[Task1] -> {out}")


# -----------------------------------------------------------------------
# タスク3: ガントチャート（SCHED ファイルから）
# -----------------------------------------------------------------------

def plot_gantt(sched_data, sol_idx, title, out_path, cost_series=None):
    """
    sched_data: read_sched() の返り値
    sol_idx: 描画する解のインデックス
    cost_series: コスト系列 [[c_k(t) for t] for k] または None
    """
    nJobs = sched_data["nJobs"]
    nRes  = sched_data["nRes"]
    dur   = sched_data["dur"]
    demand = sched_data["demand"]
    cap   = sched_data["cap"]
    cap_t = sched_data["cap_t"]
    sol   = sched_data["solutions"][sol_idx]

    starts   = sol["starts"]
    makespan = sol["makespan"]
    cost     = sol["cost"]

    # 描画: ジョブバー + 資源使用量
    n_sub = 1 + nRes  # Gantt + 各資源
    fig_h = max(6, nJobs * 0.35 + nRes * 2)
    fig, axes = plt.subplots(n_sub, 1, figsize=(max(12, makespan * 0.25), fig_h),
                              gridspec_kw={"height_ratios": [nJobs * 0.4] + [2] * nRes})

    ax_gantt = axes[0]
    cmap = plt.cm.Set3

    # 先行制約違反・資源制約違反チェック
    violations = []

    for j in range(nJobs):
        if j >= len(starts) or dur[j] <= 0:
            continue
        s = starts[j]
        d = dur[j]
        color = cmap(j % 12)
        ax_gantt.barh(j, d, left=s, height=0.7, color=color, edgecolor="black", linewidth=0.5)
        ax_gantt.text(s + d / 2, j, str(j), ha="center", va="center", fontsize=7)

    ax_gantt.set_xlim(0, makespan + 1)
    ax_gantt.set_ylim(-0.5, nJobs - 0.5)
    ax_gantt.set_yticks(range(nJobs))
    ax_gantt.set_yticklabels([f"J{j}" for j in range(nJobs)], fontsize=6)
    ax_gantt.set_xlabel("時刻")
    ax_gantt.set_title(f"{title}\nmakespan={makespan}  cost={cost:.2f}")
    ax_gantt.grid(True, axis="x", alpha=0.3)

    # 先行制約違反チェック（注釈）
    from collections import defaultdict
    preds_map = defaultdict(list)
    # 資源使用量の時系列
    usage = [[0] * (makespan + 1) for _ in range(nRes)]
    for j in range(nJobs):
        if j >= len(starts) or dur[j] <= 0:
            continue
        s = starts[j]
        d = dur[j]
        for t in range(s, s + d):
            if t <= makespan:
                for k in range(nRes):
                    if j < len(demand) and k < len(demand[j]):
                        usage[k][t] += demand[j][k]

    # 各資源の使用量グラフ
    for k in range(nRes):
        ax = axes[1 + k]
        times = list(range(makespan + 1))
        ax.fill_between(times, usage[k], step="post", alpha=0.5, label=f"使用量 R{k}")
        ax.step(times, usage[k], where="post", color=f"C{k}")

        # 容量ライン
        if cap_t and k < len(cap_t):
            caps = [cap_t[k][t] if t < len(cap_t[k]) else cap[k] for t in times]
            ax.step(times, caps, where="post", color="red", linestyle="--",
                    label=f"容量上限 R{k}")
            # 違反チェック
            for t in range(makespan + 1):
                lim = cap_t[k][t] if t < len(cap_t[k]) else cap[k]
                if usage[k][t] > lim:
                    violations.append(f"資源違反: R{k} t={t} use={usage[k][t]} > cap={lim}")
        else:
            ax.axhline(cap[k], color="red", linestyle="--", label=f"容量上限 R{k}")
            for t in range(makespan + 1):
                if usage[k][t] > cap[k]:
                    violations.append(f"資源違反: R{k} t={t} use={usage[k][t]} > cap={cap[k]}")

        ax.set_ylabel(f"R{k}")
        ax.legend(fontsize=7, loc="upper right")
        ax.set_xlim(0, makespan + 1)
        ax.grid(True, alpha=0.3)

    if violations:
        print(f"  [警告] 制約違反 {len(violations)} 件:")
        for v in violations[:5]:
            print(f"    {v}")
        if len(violations) > 5:
            print(f"    ... 他 {len(violations)-5} 件")
    else:
        print(f"  [OK] 制約違反なし")

    plt.tight_layout()
    plt.savefig(out_path, dpi=100, bbox_inches="tight")
    plt.close()
    print(f"[Task3] -> {out_path}")


def find_min_makespan_idx(solutions):
    return min(range(len(solutions)), key=lambda i: solutions[i]["makespan"])

def find_min_cost_idx(solutions):
    return min(range(len(solutions)), key=lambda i: solutions[i]["cost"])


# -----------------------------------------------------------------------
# タスク4: コスト分析
# -----------------------------------------------------------------------

def plot_cost_analysis(fun_ms_path, fun_cmp_path, cond_tag, out_dir="."):
    """
    MaxShift と SchedObj のパレートフロントを並べて比較する。
    max_shift の大小とコスト削減量の関係を散布図で示す。
    """
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # --- 左: パレートフロント比較 ---
    ax = axes[0]

    if os.path.exists(fun_ms_path):
        pts_ms = read_fun(fun_ms_path)
        if pts_ms:
            ms_vals  = [p[0] for p in pts_ms]
            cost_vals = [p[1] for p in pts_ms]
            ax.scatter(ms_vals, cost_vals, c="blue", s=25, label="MaxShift", alpha=0.7, zorder=3)
            ax.plot(sorted(ms_vals), [c for _, c in sorted(pts_ms)],
                    c="blue", linewidth=0.8, alpha=0.5)

    if os.path.exists(fun_cmp_path):
        pts_cmp = read_fun(fun_cmp_path)
        if pts_cmp:
            ms_vals2  = [p[0] for p in pts_cmp]
            cost_vals2 = [p[1] for p in pts_cmp]
            ax.scatter(ms_vals2, cost_vals2, c="orange", s=25, marker="^",
                       label="SchedObj", alpha=0.7, zorder=3)
            ax.plot(sorted(ms_vals2), [c for _, c in sorted(pts_cmp)],
                    c="orange", linewidth=0.8, alpha=0.5)

    ax.set_xlabel("Makespan")
    ax.set_ylabel("Total Cost")
    ax.set_title(f"Pareto Front 比較 ({cond_tag})")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # --- 右: cost vs makespan の差分（MaxShift - SchedObj） ---
    ax2 = axes[1]
    if os.path.exists(fun_ms_path) and os.path.exists(fun_cmp_path):
        pts_ms  = sorted(read_fun(fun_ms_path),  key=lambda p: p[0])
        pts_cmp = sorted(read_fun(fun_cmp_path), key=lambda p: p[0])

        if pts_ms and pts_cmp:
            # コスト最小付近の解を 10 点抽出して情報表示
            top_n = min(10, len(pts_ms))
            cheapest = sorted(pts_ms, key=lambda p: p[1])[:top_n]
            ax2.scatter([p[0] for p in cheapest], [p[1] for p in cheapest],
                        c="blue", s=60, zorder=5, label=f"MaxShift cost-min {top_n}点")
            for p in cheapest:
                ax2.annotate(f"ms={int(p[0])}", p, fontsize=6,
                              textcoords="offset points", xytext=(3, 3))

            # SchedObj の cost 最小付近
            cheapest_cmp = sorted(pts_cmp, key=lambda p: p[1])[:top_n]
            ax2.scatter([p[0] for p in cheapest_cmp], [p[1] for p in cheapest_cmp],
                        c="orange", marker="^", s=60, zorder=5,
                        label=f"SchedObj cost-min {top_n}点")

    ax2.set_xlabel("Makespan")
    ax2.set_ylabel("Total Cost")
    ax2.set_title("コスト最小解の拡大表示")
    ax2.legend(fontsize=8)
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    out = os.path.join(out_dir, f"cost_analysis_{cond_tag}.png")
    plt.savefig(out, dpi=120, bbox_inches="tight")
    plt.close()
    print(f"[Task4] -> {out}")


# -----------------------------------------------------------------------
# メイン
# -----------------------------------------------------------------------

def main():
    prefix   = sys.argv[1] if len(sys.argv) > 1 else "j301_1"
    cond_tag = sys.argv[2] if len(sys.argv) > 2 else "RR000_RV0"

    print(f"[Diagnosis] prefix={prefix}  cond={cond_tag}")
    print("=" * 60)

    # ---- ディレクトリ構造に合わせたパス解決ヘルパー ----
    # 新構造: results/FUN_ENC/{prefix}/, results/SCHED/{prefix}/, figures/GANTT/{prefix}/
    enc_dir   = os.path.join("results", "FUN_ENC", prefix)
    sched_dir = os.path.join("results", "SCHED",   prefix)
    fun_dir   = os.path.join("results", "FUN",     prefix)
    gantt_dir = os.path.join("figures", "GANTT",   prefix)
    log_dir   = os.path.join("logs",    "generation")

    def find_file(*candidates):
        """候補パスを順に試して最初に存在するものを返す。なければ先頭候補を返す。"""
        for p in candidates:
            if os.path.exists(p):
                return p
        return candidates[0]

    out_dir = "."  # 出力先

    # ---- タスク1: 世代ログ ----
    log_path = find_file(
        os.path.join(log_dir, f"generation_log_s1.csv"),
        "generation_log.csv",
    )
    plot_generation_log(log_path, out_dir)

    # ---- タスク3: ガントチャート ----
    # SCHED ファイル名候補（新旧両方対応）
    sched_ms_candidates = [
        os.path.join(enc_dir,   f"SCHED_ENC_{prefix}_{cond_tag}_MaxShift.txt"),
        os.path.join(sched_dir, f"SCHED_MS_{prefix}_{cond_tag}_MaxShift"),
        f"SCHED_ENC_{prefix}_{cond_tag}_MaxShift.txt",
        f"SCHED_MS_{prefix}_{cond_tag}_MaxShift",
    ]
    sched_ms_path = find_file(*sched_ms_candidates)

    if os.path.exists(sched_ms_path):
        sched_data = read_sched(sched_ms_path)
        sols = sched_data["solutions"]
        print(f"[Task3] SCHED loaded: {len(sols)} solutions from {sched_ms_path}")

        if sols:
            idx_ms   = find_min_makespan_idx(sols)
            idx_cost = find_min_cost_idx(sols)

            plot_gantt(sched_data, idx_ms,
                       "MaxShift Pareto - Makespan最小解 (3a)",
                       os.path.join(out_dir, "gantt_maxshift_minMS.png"))

            if idx_cost != idx_ms:
                plot_gantt(sched_data, idx_cost,
                           "MaxShift Pareto - Cost最小解 (3b)",
                           os.path.join(out_dir, "gantt_maxshift_minCost.png"))
            else:
                print("[Task3b] MinCost == MinMS のため別ファイルはスキップ")
    else:
        print(f"[Task3] {sched_ms_path} が見つかりません。先に C++ を実行してください。")

    # タスク3c: SchedObj MinMS
    sched_so_candidates = [
        os.path.join(enc_dir,   f"SCHED_ENC_{prefix}_{cond_tag}_SchedObj.txt"),
        os.path.join(sched_dir, f"SCHED_MS_{prefix}_{cond_tag}_SchedObj"),
        f"SCHED_ENC_{prefix}_{cond_tag}_SchedObj.txt",
        f"SCHED_MS_{prefix}_{cond_tag}_SchedObj",
    ]
    sched_so_path = find_file(*sched_so_candidates)
    if os.path.exists(sched_so_path):
        sched_so = read_sched(sched_so_path)
        if sched_so["solutions"]:
            idx_so_ms = find_min_makespan_idx(sched_so["solutions"])
            plot_gantt(sched_so, idx_so_ms,
                       "SchedObj Pareto - Makespan最小解 (3c)",
                       os.path.join(out_dir, "gantt_schedobj_minMS.png"))
    else:
        print(f"[Task3c] {sched_so_path} なし → スキップ")

    # タスク3d: 初期解ガントチャート（テキストを Python で再描画）
    gantt_init_txt = find_file(
        os.path.join(gantt_dir, f"GANTT_MS_{prefix}_initial_allzero.txt"),
        f"GANTT_MS_{prefix}_initial_allzero.txt",
    )
    if not os.path.exists(gantt_init_txt):
        print(f"[Task3d] {gantt_init_txt} なし → スキップ")
    else:
        print(f"[Task3d] {gantt_init_txt} はテキスト形式で出力済み。PNG変換はSCHEDファイルが必要です。")

    # ---- タスク4: コスト分析 ----
    fun_ms_candidates = [
        os.path.join(enc_dir, f"FUN_ENC_{prefix}_{cond_tag}_MaxShift.txt"),
        os.path.join(fun_dir, f"FUN_MS_{prefix}_{cond_tag}_MaxShift"),
        f"FUN_ENC_{prefix}_{cond_tag}_MaxShift.txt",
        f"FUN_MS_{prefix}_{cond_tag}_MaxShift",
    ]
    fun_ms_path = find_file(*fun_ms_candidates)

    fun_cmp_candidates = [
        os.path.join(enc_dir, f"FUN_ENC_{prefix}_{cond_tag}_SchedObj.txt"),
        os.path.join(fun_dir, f"FUN_CMP_{prefix}_{cond_tag}_P1_-_a50.txt"),
        os.path.join(fun_dir, f"FUN_{prefix}_{cond_tag}_SchedObj"),
        f"FUN_ENC_{prefix}_{cond_tag}_SchedObj.txt",
        f"FUN_CMP_{prefix}_{cond_tag}_P1_-_a50.txt",
        f"FUN_{prefix}_{cond_tag}_SchedObj",
    ]
    fun_cmp_path = ""
    for c in fun_cmp_candidates:
        if os.path.exists(c):
            fun_cmp_path = c
            break

    plot_cost_analysis(fun_ms_path, fun_cmp_path, cond_tag, out_dir)

    # ---- サマリ ----
    print("\n" + "=" * 60)
    print("[Summary] 生成ファイル確認:")
    for f in [
        os.path.join(enc_dir, f"FUN_ENC_{prefix}_{cond_tag}_MaxShift.txt"),
        os.path.join(enc_dir, f"FUN_ENC_{prefix}_{cond_tag}_SchedObj.txt"),
        os.path.join(enc_dir, f"SCHED_ENC_{prefix}_{cond_tag}_MaxShift.txt"),
        os.path.join(enc_dir, f"SCHED_ENC_{prefix}_{cond_tag}_SchedObj.txt"),
        "gantt_maxshift_minMS.png",
        "gantt_maxshift_minCost.png",
        "gantt_schedobj_minMS.png",
        f"cost_analysis_{cond_tag}.png",
    ]:
        status = "OK" if os.path.exists(f) else "MISSING"
        print(f"  [{status}] {f}")
    print("=" * 60)


if __name__ == "__main__":
    main()
