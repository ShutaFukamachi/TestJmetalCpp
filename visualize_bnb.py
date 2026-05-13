#!/usr/bin/env python3
"""
visualize_bnb.py - 分枝限定法（BnB）結果のビジュアライゼーション

対象ファイル:
  FUN_tree_<prefix>_<ctag>   : パレートフロント
  SCHED_tree_<prefix>_<ctag> : スケジュール詳細

出力先: figures/bnb/  (実行のたびにクリアして再生成)

使い方:
  cd cmake-build-release
  python ../visualize_bnb.py
"""

import os
import sys
import glob
import re
import shutil
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.lines as mlines
import matplotlib.patches as mpatches
import numpy as np
from collections import defaultdict


OUTPUT_DIR = 'figures/bnb'


# ------------------------------------------------------------------ #
# ファイル読み込み
# ------------------------------------------------------------------ #

def read_fun(path):
    solutions = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                solutions.append((float(parts[0]), float(parts[1])))
    return solutions


def read_sched(path):
    with open(path) as f:
        lines = [l.strip() for l in f if l.strip()]

    idx = 0
    nJobs, nRes = map(int, lines[idx].split()); idx += 1
    durations = list(map(int, lines[idx].split())); idx += 1

    demand = []
    for _ in range(nJobs):
        demand.append(list(map(int, lines[idx].split()))); idx += 1

    capacity = list(map(int, lines[idx].split())); idx += 1

    T_cap = int(lines[idx]); idx += 1
    capacity_t = []
    if T_cap > 0:
        for _ in range(nRes):
            capacity_t.append(list(map(int, lines[idx].split()))); idx += 1

    nSols = int(lines[idx]); idx += 1
    solutions = []
    for _ in range(nSols):
        parts = list(map(float, lines[idx].split())); idx += 1
        makespan    = parts[0]
        cost        = parts[1]
        start_times = list(map(int, parts[2:2 + nJobs]))
        solutions.append((makespan, cost, start_times))

    return nJobs, nRes, durations, demand, capacity, capacity_t, solutions


# ------------------------------------------------------------------ #
# パレートフロント（RR/RV 条件ごとに色分けして1インスタンス1枚）
# ------------------------------------------------------------------ #

def plot_pareto_fronts(fun_files, output_dir):
    instance_groups = defaultdict(list)
    for fp in fun_files:
        bn = os.path.basename(fp)
        m = re.match(r'FUN_tree_(.+?)_(RR\d+_RV\d)', bn)
        if m:
            instance_groups[m.group(1)].append((m.group(2), fp))

    for prefix, items in sorted(instance_groups.items()):
        fig, ax = plt.subplots(figsize=(10, 7))
        colors = plt.cm.tab10(np.linspace(0, 0.8, len(items)))

        for (ctag, fp), color in zip(sorted(items), colors):
            sols = read_fun(fp)
            if not sols:
                continue
            ms = [s[0] for s in sols]
            cs = [s[1] for s in sols]
            ax.scatter(ms, cs, color=color, label=ctag,
                       alpha=0.75, s=60, marker='*', edgecolors='none')

        ax.set_xlabel('Makespan', fontsize=13)
        ax.set_ylabel('Cost', fontsize=13)
        ax.set_title(f'[BnB]  Pareto Front — {prefix}',
                     fontsize=14, fontweight='bold')
        ax.legend(loc='upper right', fontsize=9, framealpha=0.8)
        ax.grid(True, linestyle='--', alpha=0.4)

        out_path = os.path.join(output_dir, f'pareto_{prefix}.png')
        fig.savefig(out_path, dpi=150, bbox_inches='tight')
        plt.close(fig)
        print(f'  [Saved] {out_path}')


# ------------------------------------------------------------------ #
# 代表解の選択
# ------------------------------------------------------------------ #

def _pick_representative(solutions):
    by_ms = sorted(solutions, key=lambda s: s[0])
    by_cs = sorted(solutions, key=lambda s: s[1])

    candidates = [
        ('min_makespan', by_ms[0]),
        ('min_cost',     by_cs[0]),
    ]
    if len(by_ms) > 2:
        candidates.append(('median', by_ms[len(by_ms) // 2]))

    seen, unique = set(), []
    for name, sol in candidates:
        key = (sol[0], sol[1])
        if key not in seen:
            seen.add(key)
            unique.append((name, sol))
    return unique


# ------------------------------------------------------------------ #
# 資源使用量チャート
# ------------------------------------------------------------------ #

def plot_resource_usage(nJobs, nRes, durations, demand,
                        capacity, capacity_t,
                        solutions, prefix, ctag, output_dir):
    if not solutions:
        return

    cmap       = plt.cm.tab20
    job_colors = [cmap((j % 20) / 20) for j in range(nJobs)]

    for name, (makespan, cost, start_times) in _pick_representative(solutions):
        T = int(makespan) + 1
        time_axis = np.arange(T)

        fig_w = max(14, int(T * 0.25))
        fig, axes = plt.subplots(nRes, 1,
                                  figsize=(fig_w, 3.5 * nRes),
                                  sharex=True)
        if nRes == 1:
            axes = [axes]

        for k, ax in enumerate(axes):
            layers, layer_colors = [], []
            for j in range(nJobs):
                s = start_times[j]
                d = durations[j]
                if d == 0 or demand[j][k] == 0:
                    continue
                layer = np.zeros(T)
                t0, t1 = max(0, s), min(s + d, T)
                layer[t0:t1] = demand[j][k]
                layers.append(layer)
                layer_colors.append(job_colors[j])

            if layers:
                ax.stackplot(time_axis, layers,
                             colors=layer_colors, alpha=0.85, linewidth=0)

            if capacity_t and k < len(capacity_t) and capacity_t[k]:
                T_cap = len(capacity_t[k])
                cap_line = np.array(
                    [capacity_t[k][t] if t < T_cap else capacity[k] for t in range(T)],
                    dtype=float)
            else:
                cap_line = np.full(T, float(capacity[k]))

            ax.step(time_axis, cap_line, where='post',
                    color='red', linewidth=2.0, zorder=5, label='Capacity')

            vacation_mask = cap_line == 0
            if vacation_mask.any():
                ax.fill_between(time_axis, 0, capacity[k],
                                where=vacation_mask,
                                color='red', alpha=0.15, linewidth=0,
                                label='Vacation (cap=0)')

            max_cap = max(float(capacity[k]), cap_line.max()) if len(cap_line) else float(capacity[k])
            ax.set_ylabel(f'Resource {k}\nUsage', fontsize=10)
            ax.set_ylim(0, max(max_cap * 1.25, 1))
            ax.yaxis.set_major_locator(
                matplotlib.ticker.MaxNLocator(integer=True, nbins=5))
            ax.grid(True, axis='x', linestyle='--', alpha=0.3)

        axes[-1].set_xlabel('Time', fontsize=12)
        fig.suptitle(
            f'[BnB]  Resource Usage  [{name}]  —  {prefix}  {ctag}\n'
            f'Makespan = {int(makespan)},  Cost = {cost:.1f}',
            fontsize=12, fontweight='bold'
        )

        cap_handle = mlines.Line2D([], [], color='red', linewidth=2, label='Capacity')
        vac_handle = mpatches.Patch(color='red', alpha=0.15, label='Vacation (cap=0)')
        n_legend   = min(nJobs, 15)
        job_handles = [mpatches.Patch(color=job_colors[j], label=f'J{j}')
                       for j in range(n_legend)]
        axes[0].legend(
            handles=job_handles + [cap_handle, vac_handle],
            loc='upper right', fontsize=6,
            ncol=max(1, (n_legend + 2) // 5),
            framealpha=0.75
        )

        plt.tight_layout()
        out_path = os.path.join(output_dir, f'resource_{prefix}_{ctag}_{name}.png')
        fig.savefig(out_path, dpi=150, bbox_inches='tight')
        plt.close(fig)
        print(f'  [Saved] {out_path}')


# ------------------------------------------------------------------ #
# メイン
# ------------------------------------------------------------------ #

def main():
    search_dir = '.'
    output_dir = sys.argv[1] if len(sys.argv) > 1 else OUTPUT_DIR

    # 出力ディレクトリをクリアして再生成
    if os.path.exists(output_dir):
        shutil.rmtree(output_dir)
    os.makedirs(output_dir)
    print(f'Output directory cleared: {output_dir}/')

    # BnB のファイルのみ取得（_tree_ を含むもの）
    fun_files   = sorted(glob.glob(os.path.join(search_dir, 'FUN_tree_*')))
    sched_files = sorted(glob.glob(os.path.join(search_dir, 'SCHED_tree_*')))

    if not fun_files and not sched_files:
        print('BnB の FUN_tree_* / SCHED_tree_* ファイルが見つかりません。')
        return

    if fun_files:
        print(f'\n--- パレートフロント ({len(fun_files)} ファイル) ---')
        plot_pareto_fronts(fun_files, output_dir)

    if sched_files:
        print(f'\n--- 資源使用量チャート ({len(sched_files)} ファイル) ---')
        for fp in sched_files:
            bn = os.path.basename(fp)
            m  = re.match(r'SCHED_tree_(.+?)_(RR\d+_RV\d)', bn)
            if not m:
                continue
            prefix, ctag = m.group(1), m.group(2)
            try:
                nJobs, nRes, durations, demand, capacity, capacity_t, solutions = read_sched(fp)
                print(f'  {bn}  ({len(solutions)} 解)')
                plot_resource_usage(nJobs, nRes, durations, demand,
                                    capacity, capacity_t,
                                    solutions, prefix, ctag, output_dir)
            except Exception as e:
                print(f'  [ERROR] {bn}: {e}')

    print(f'\n完了。{output_dir}/ に保存されました。')


if __name__ == '__main__':
    main()