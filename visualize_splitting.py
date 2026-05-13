#!/usr/bin/env python3
"""
visualize_splitting.py  - P1 / P2 / P3 活動分割比較のビジュアライゼーション

対象ファイル (cmake-build-release/ 直下):
  FUN_<prefix>_<ctag>_<mode>    例: FUN_j30_1_RR000_RV0_P1
  SCHED_<prefix>_<ctag>_<mode>  例: SCHED_j30_1_RR000_RV0_P2

生成グラフ:
  1. pareto_compare_<prefix>_<ctag>.png
       P1/P2/P3 の 3 パレートフロントを同一軸上に重ねて比較
  2. pareto_all_<prefix>_<mode>.png
       同一モード内の全 RR/RV 条件を 1 枚に表示
  3. gantt_<prefix>_<ctag>_<mode>_<name>.png
       代表解 (min_makespan / min_cost) のガントチャート
  4. resource_<prefix>_<ctag>_<mode>_<name>.png
       代表解の資源使用量チャート
  5. summary_makespan_<prefix>.png / summary_cost_<prefix>.png
       P1/P2/P3 × 全条件の min-makespan / min-cost を棒グラフで比較

使い方:
  cd cmake-build-release   (または cmake-build-debug)
  python ../visualize_splitting.py
  python ../visualize_splitting.py figures/splitting_NSGA2  # 出力先を変更
"""

import os
import sys
import glob
import re
import shutil
import math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.lines as mlines
import matplotlib.patches as mpatches
import matplotlib.ticker as ticker
import numpy as np
from collections import defaultdict

OUTPUT_DIR = 'figures/splitting'

# P1/P2/P3 ごとの見た目
MODE_STYLES = {
    'P1': dict(color='#1f77b4', marker='o', label='P1 (No split)'),
    'P2': dict(color='#ff7f0e', marker='s', label='P2 (Non-preemptive split)'),
    'P3': dict(color='#2ca02c', marker='^', label='P3 (Preemptive split)'),
}

# ------------------------------------------------------------------ #
# ファイル読み込み
# ------------------------------------------------------------------ #

def read_fun(path):
    """FUN_* ファイルを読み込み [(makespan, cost), ...] を返す。"""
    solutions = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                ms, c = float(parts[0]), float(parts[1])
                if ms < 1e8 and c < 1e8:   # ペナルティ解を除外
                    solutions.append((ms, c))
    return solutions


def read_sched(path):
    """SCHED_* ファイルを読み込む。"""
    with open(path) as f:
        lines = [l.strip() for l in f if l.strip()]

    idx = 0
    nJobs, nRes = map(int, lines[idx].split()); idx += 1
    durations    = list(map(int, lines[idx].split())); idx += 1

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
        if makespan < 1e8 and cost < 1e8:
            solutions.append((makespan, cost, start_times))

    return nJobs, nRes, durations, demand, capacity, capacity_t, solutions


def parse_fun_filename(bn):
    """FUN_<prefix>_<ctag>_<mode> を解析して (prefix, ctag, mode) を返す。"""
    m = re.match(r'FUN_(.+?)_(RR\d+_RV\d)_(P[123])$', bn)
    if m:
        return m.group(1), m.group(2), m.group(3)
    return None, None, None


def parse_sched_filename(bn):
    """SCHED_<prefix>_<ctag>_<mode> を解析して (prefix, ctag, mode) を返す。"""
    m = re.match(r'SCHED_(.+?)_(RR\d+_RV\d)_(P[123])$', bn)
    if m:
        return m.group(1), m.group(2), m.group(3)
    return None, None, None


# ------------------------------------------------------------------ #
# 1. P1 vs P2 vs P3 パレートフロント比較
#    1 インスタンス × 1 条件 (ctag) につき 1 枚
# ------------------------------------------------------------------ #

def plot_pareto_compare(fun_files, output_dir):
    """
    key: (prefix, ctag)
    同一 (prefix, ctag) に属する P1/P2/P3 のパレートフロントを重ねてプロット。
    """
    groups = defaultdict(dict)   # groups[(prefix, ctag)][mode] = [(ms, c), ...]

    for fp in fun_files:
        bn = os.path.basename(fp)
        prefix, ctag, mode = parse_fun_filename(bn)
        if prefix is None:
            continue
        sols = read_fun(fp)
        if sols:
            groups[(prefix, ctag)][mode] = sols

    for (prefix, ctag), mode_sols in sorted(groups.items()):
        fig, ax = plt.subplots(figsize=(9, 7))

        for mode in ['P1', 'P2', 'P3']:
            sols = mode_sols.get(mode)
            if not sols:
                continue
            ms = [s[0] for s in sols]
            cs = [s[1] for s in sols]
            st = MODE_STYLES[mode]
            ax.scatter(ms, cs,
                       color=st['color'], marker=st['marker'],
                       label=st['label'],
                       alpha=0.80, s=45, edgecolors='none')

        ax.set_xlabel('Makespan', fontsize=13)
        ax.set_ylabel('Resource Cost', fontsize=13)
        ax.set_title(
            f'Pareto Front Comparison  —  {prefix}  {ctag}\n'
            f'P1: No split  |  P2: Non-preemptive  |  P3: Preemptive',
            fontsize=13, fontweight='bold'
        )
        ax.legend(loc='upper right', fontsize=11, framealpha=0.85)
        ax.grid(True, linestyle='--', alpha=0.4)

        out_path = os.path.join(output_dir, f'pareto_compare_{prefix}_{ctag}.png')
        fig.savefig(out_path, dpi=150, bbox_inches='tight')
        plt.close(fig)
        print(f'  [Saved] {out_path}')


# ------------------------------------------------------------------ #
# 2. 同一モード × 全 RR/RV 条件
#    1 インスタンス × 1 モード につき 1 枚
# ------------------------------------------------------------------ #

def plot_pareto_all_conditions(fun_files, output_dir):
    """同一 (prefix, mode) につき全条件の色分けパレートフロントを 1 枚に描画。"""
    groups = defaultdict(dict)   # groups[(prefix, mode)][ctag] = [(ms, c), ...]

    for fp in fun_files:
        bn = os.path.basename(fp)
        prefix, ctag, mode = parse_fun_filename(bn)
        if prefix is None:
            continue
        sols = read_fun(fp)
        if sols:
            groups[(prefix, mode)][ctag] = sols

    for (prefix, mode), ctag_sols in sorted(groups.items()):
        items = sorted(ctag_sols.items())
        fig, ax = plt.subplots(figsize=(10, 7))
        colors = plt.cm.tab10(np.linspace(0, 0.8, len(items)))

        for (ctag, sols), color in zip(items, colors):
            ms = [s[0] for s in sols]
            cs = [s[1] for s in sols]
            ax.scatter(ms, cs, color=color, label=ctag,
                       alpha=0.75, s=35, edgecolors='none')

        ax.set_xlabel('Makespan', fontsize=13)
        ax.set_ylabel('Resource Cost', fontsize=13)
        ax.set_title(
            f'[{mode}]  Pareto Fronts by Condition  —  {prefix}',
            fontsize=13, fontweight='bold'
        )
        ax.legend(loc='upper right', fontsize=9, framealpha=0.8)
        ax.grid(True, linestyle='--', alpha=0.4)

        out_path = os.path.join(output_dir, f'pareto_all_{prefix}_{mode}.png')
        fig.savefig(out_path, dpi=150, bbox_inches='tight')
        plt.close(fig)
        print(f'  [Saved] {out_path}')


# ------------------------------------------------------------------ #
# 3. ガントチャート
# ------------------------------------------------------------------ #

def _pick_representative(solutions, debug_label=''):
    """min_makespan 解と min_cost 解を代表解として返す。"""
    if not solutions:
        return []
    by_ms = sorted(solutions, key=lambda s: s[0])
    by_cs = sorted(solutions, key=lambda s: s[1])

    # --- デバッグ出力 ---
    label = f'[{debug_label}] ' if debug_label else ''
    print(f'  {label}全解 ({len(solutions)} 件):')
    for i, (ms, cost, st) in enumerate(solutions):
        print(f'    [{i:2d}] makespan={ms:.1f}  cost={cost:.1f}  start_times={st}')
    print(f'  {label}→ min_makespan 選択: makespan={by_ms[0][0]:.1f}  cost={by_ms[0][1]:.1f}')
    print(f'  {label}→ min_cost     選択: makespan={by_cs[0][0]:.1f}  cost={by_cs[0][1]:.1f}')
    # --- ここまで ---

    candidates = [('min_makespan', by_ms[0]), ('min_cost', by_cs[0])]
    seen, unique = set(), []
    for name, sol in candidates:
        key = (sol[0], sol[1])
        if key not in seen:
            seen.add(key)
            unique.append((name, sol))
    return unique


def plot_gantt(nJobs, nRes, durations, demand, capacity, capacity_t,
               solutions, prefix, ctag, mode, output_dir):
    """
    代表解のガントチャートを描画する。

    P2/P3 では start_times は「最初の実行時刻」、バーの長さは duration。
    実際には飛び飛びで実行される可能性があるが、SCHED ファイルには
    開始時刻のみ記録されているため、カバレッジ区間として表示する。
    タイトルに注記する。
    """
    if not solutions:
        return

    cmap = plt.cm.tab20
    job_colors = [cmap((j % 20) / 20) for j in range(nJobs)]
    note = ' (first-exec time shown)' if mode in ('P2', 'P3') else ''

    for name, (makespan, cost, start_times) in _pick_representative(
            solutions, debug_label=f'Gantt {prefix}_{ctag}_{mode}'):
        # Compute actual schedule end from bar extents (start + duration).
        # Use this for both xlim and the Gantt title so they always match.
        actual_end = max(
            (start_times[j] + durations[j] for j in range(nJobs)),
            default=int(makespan)
        )
        display_makespan = actual_end  # what the bars actually show
        T = actual_end + 2
        fig_w = max(12, int(T * 0.3))
        fig, ax = plt.subplots(figsize=(fig_w, max(4, nJobs * 0.35)))

        print(f'    [{name}] makespan={makespan:.1f}  cost={cost:.1f}  '
              f'actual_end={actual_end}  display_makespan={display_makespan}')
        print(f'    {"Job":>4}  {"start":>6}  {"dur":>5}  {"end(s+d)":>9}')
        for j in range(nJobs):
            s = start_times[j]
            d = durations[j]
            print(f'    J{j:>3}  {s:>6}  {d:>5}  {s+d:>9}')
            if d == 0:
                ax.plot(s, j, marker='D', color=job_colors[j], markersize=6)
            else:
                ax.barh(j, d, left=s, height=0.6,
                        color=job_colors[j], edgecolor='black', linewidth=0.5,
                        alpha=0.85)
                if d >= 2:
                    ax.text(s + d / 2, j, f'J{j}',
                            ha='center', va='center', fontsize=7,
                            color='black', fontweight='bold')

        ax.set_xlabel('Time', fontsize=12)
        ax.set_ylabel('Job', fontsize=12)
        ax.set_yticks(range(nJobs))
        ax.set_yticklabels([f'J{j}' for j in range(nJobs)], fontsize=7)
        ax.set_xlim(0, T)
        ax.invert_yaxis()
        ax.grid(True, axis='x', linestyle='--', alpha=0.4)
        ax.set_title(
            f'[{mode}]  Gantt Chart  [{name}]{note}  —  {prefix}  {ctag}\n'
            f'Makespan = {display_makespan},  Cost = {cost:.1f}',
            fontsize=12, fontweight='bold'
        )

        handles = [mpatches.Patch(color=job_colors[j], label=f'J{j}')
                   for j in range(min(nJobs, 20))]
        ax.legend(handles=handles, loc='lower right', fontsize=6,
                  ncol=max(1, min(nJobs, 20) // 5), framealpha=0.75)

        plt.tight_layout()
        out_path = os.path.join(output_dir,
                                f'gantt_{prefix}_{ctag}_{mode}_{name}.png')
        fig.savefig(out_path, dpi=150, bbox_inches='tight')
        plt.close(fig)
        print(f'  [Saved] {out_path}')


# ------------------------------------------------------------------ #
# 4. 資源使用量チャート
# ------------------------------------------------------------------ #

def plot_resource_usage(nJobs, nRes, durations, demand,
                        capacity, capacity_t,
                        solutions, prefix, ctag, mode, output_dir):
    if not solutions:
        return

    cmap = plt.cm.tab20
    job_colors = [cmap((j % 20) / 20) for j in range(nJobs)]
    note = ' (coverage window)' if mode in ('P2', 'P3') else ''

    for name, (makespan, cost, start_times) in _pick_representative(
            solutions, debug_label=f'Resource {prefix}_{ctag}_{mode}'):
        actual_end = max(
            (start_times[j] + durations[j] for j in range(nJobs)),
            default=int(makespan)
        )
        T = actual_end + 2
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
                    [capacity_t[k][t] if t < T_cap else capacity[k]
                     for t in range(T)], dtype=float)
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

            max_cap = max(float(capacity[k]),
                          cap_line.max() if len(cap_line) else 0)
            ax.set_ylabel(f'Resource {k}\nUsage', fontsize=10)
            ax.set_ylim(0, max(max_cap * 1.25, 1))
            ax.yaxis.set_major_locator(ticker.MaxNLocator(integer=True, nbins=5))
            ax.grid(True, axis='x', linestyle='--', alpha=0.3)

        axes[-1].set_xlabel('Time', fontsize=12)
        fig.suptitle(
            f'[{mode}]  Resource Usage{note}  [{name}]  —  {prefix}  {ctag}\n'
            f'Makespan = {int(makespan)},  Cost = {cost:.1f}',
            fontsize=12, fontweight='bold'
        )

        cap_handle = mlines.Line2D([], [], color='red', linewidth=2,
                                    label='Capacity')
        vac_handle = mpatches.Patch(color='red', alpha=0.15,
                                     label='Vacation (cap=0)')
        n_legend = min(nJobs, 15)
        job_handles = [mpatches.Patch(color=job_colors[j], label=f'J{j}')
                       for j in range(n_legend)]
        axes[0].legend(handles=job_handles + [cap_handle, vac_handle],
                        loc='upper right', fontsize=6,
                        ncol=max(1, (n_legend + 2) // 5),
                        framealpha=0.75)

        plt.tight_layout()
        out_path = os.path.join(output_dir,
                                f'resource_{prefix}_{ctag}_{mode}_{name}.png')
        fig.savefig(out_path, dpi=150, bbox_inches='tight')
        plt.close(fig)
        print(f'  [Saved] {out_path}')


# ------------------------------------------------------------------ #
# 5. サマリー棒グラフ：各条件の min-makespan / min-cost を P1/P2/P3 で比較
# ------------------------------------------------------------------ #

def plot_summary(fun_files, output_dir):
    """
    インスタンスごとに:
      summary_makespan_<prefix>.png  … 条件 × モードの min-makespan 棒グラフ
      summary_cost_<prefix>.png      … 条件 × モードの min-cost 棒グラフ
    """
    # data[(prefix, ctag, mode)] = (min_ms, min_cost)
    data = {}
    for fp in fun_files:
        bn = os.path.basename(fp)
        prefix, ctag, mode = parse_fun_filename(bn)
        if prefix is None:
            continue
        sols = read_fun(fp)
        if not sols:
            continue
        min_ms   = min(s[0] for s in sols)
        min_cost = min(s[1] for s in sols)
        data[(prefix, ctag, mode)] = (min_ms, min_cost)

    # インスタンス別にグループ化
    prefix_set = sorted({k[0] for k in data})

    for prefix in prefix_set:
        # 利用可能な条件とモード
        ctags = sorted({k[1] for k in data if k[0] == prefix})
        modes = ['P1', 'P2', 'P3']

        if not ctags:
            continue

        n_cond   = len(ctags)
        n_mode   = len(modes)
        x        = np.arange(n_cond)
        bar_w    = 0.25
        offsets  = np.linspace(-(n_mode - 1) / 2, (n_mode - 1) / 2, n_mode) * bar_w

        for metric, idx, ylabel, fname_suffix in [
            ('min-makespan', 0, 'Min Makespan', 'makespan'),
            ('min-cost',     1, 'Min Resource Cost', 'cost'),
        ]:
            fig, ax = plt.subplots(figsize=(max(10, n_cond * 1.5), 6))

            for mode, offset in zip(modes, offsets):
                st = MODE_STYLES[mode]
                values = [
                    data.get((prefix, ctag, mode), (np.nan, np.nan))[idx]
                    for ctag in ctags
                ]
                bars = ax.bar(x + offset, values, bar_w,
                              color=st['color'], label=st['label'],
                              edgecolor='white', linewidth=0.5, alpha=0.85)
                # 値ラベル
                for bar, v in zip(bars, values):
                    if not math.isnan(v):
                        ax.text(bar.get_x() + bar.get_width() / 2,
                                bar.get_height() + 0.5,
                                f'{v:.0f}',
                                ha='center', va='bottom', fontsize=7,
                                rotation=90)

            ax.set_xticks(x)
            ax.set_xticklabels(ctags, rotation=30, ha='right', fontsize=9)
            ax.set_ylabel(ylabel, fontsize=12)
            ax.set_title(
                f'[{metric}]  P1 vs P2 vs P3  —  {prefix}',
                fontsize=13, fontweight='bold'
            )
            ax.legend(loc='upper right', fontsize=10, framealpha=0.85)
            ax.grid(True, axis='y', linestyle='--', alpha=0.4)

            plt.tight_layout()
            out_path = os.path.join(output_dir,
                                    f'summary_{fname_suffix}_{prefix}.png')
            fig.savefig(out_path, dpi=150, bbox_inches='tight')
            plt.close(fig)
            print(f'  [Saved] {out_path}')


# ------------------------------------------------------------------ #
# 6. ハイパーボリューム近似（面積）による比較グラフ
#    P1→P2→P3 の改善をビジュアルに示す
# ------------------------------------------------------------------ #

def _hypervolume_2d(solutions, ref_point=None):
    """
    2D パレートフロントの簡易ハイパーボリューム（面積）を計算する。
    ref_point がなければ各軸最大値 × 1.1 を使う。
    """
    if not solutions:
        return 0.0
    pts = sorted(solutions, key=lambda p: p[0])   # makespan 昇順

    if ref_point is None:
        rx = max(p[0] for p in pts) * 1.1
        ry = max(p[1] for p in pts) * 1.1
    else:
        rx, ry = ref_point

    hv = 0.0
    prev_x = 0.0
    for ms, cost in pts:
        width = ms - prev_x
        height = max(0.0, ry - cost)
        hv += width * height
        prev_x = ms
    return hv


def plot_hypervolume(fun_files, output_dir):
    """
    インスタンスごとに hypervolume_<prefix>.png を生成。
    各条件 × モードのハイパーボリューム値を折れ線グラフで比較。
    """
    hv_data = defaultdict(lambda: defaultdict(dict))
    # hv_data[prefix][ctag][mode] = hv_value

    # まず全データを読み込んで共通 ref_point を決める
    all_sols = defaultdict(list)
    for fp in fun_files:
        bn = os.path.basename(fp)
        prefix, ctag, mode = parse_fun_filename(bn)
        if prefix is None:
            continue
        sols = read_fun(fp)
        all_sols[prefix].extend(sols)

    ref_points = {}
    for prefix, sols in all_sols.items():
        if sols:
            rx = max(p[0] for p in sols) * 1.1
            ry = max(p[1] for p in sols) * 1.1
            ref_points[prefix] = (rx, ry)

    for fp in fun_files:
        bn = os.path.basename(fp)
        prefix, ctag, mode = parse_fun_filename(bn)
        if prefix is None:
            continue
        sols = read_fun(fp)
        ref = ref_points.get(prefix)
        hv = _hypervolume_2d(sols, ref)
        hv_data[prefix][ctag][mode] = hv

    for prefix, ctag_dict in sorted(hv_data.items()):
        ctags = sorted(ctag_dict.keys())
        if not ctags:
            continue

        fig, ax = plt.subplots(figsize=(max(9, len(ctags) * 1.2), 5))
        x = np.arange(len(ctags))

        for mode in ['P1', 'P2', 'P3']:
            st = MODE_STYLES[mode]
            values = [ctag_dict[ctag].get(mode, np.nan) for ctag in ctags]
            ax.plot(x, values,
                    color=st['color'], marker=st['marker'],
                    linewidth=2.0, markersize=8,
                    label=st['label'])

        ax.set_xticks(x)
        ax.set_xticklabels(ctags, rotation=30, ha='right', fontsize=9)
        ax.set_ylabel('Hypervolume (approx.)', fontsize=12)
        ax.set_title(
            f'Hypervolume Comparison  —  {prefix}\n'
            f'(Higher is better)',
            fontsize=13, fontweight='bold'
        )
        ax.legend(loc='best', fontsize=10, framealpha=0.85)
        ax.grid(True, linestyle='--', alpha=0.4)

        plt.tight_layout()
        out_path = os.path.join(output_dir, f'hypervolume_{prefix}.png')
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
    print(f'Output directory: {output_dir}/')

    # FUN_* / SCHED_* ファイルを収集（P1/P2/P3 サフィックスを持つもの）
    all_fun   = sorted(glob.glob(os.path.join(search_dir, 'FUN_*')))
    all_sched = sorted(glob.glob(os.path.join(search_dir, 'SCHED_*')))

    fun_files   = [f for f in all_fun
                   if re.search(r'_(P[123])$', os.path.basename(f))]
    sched_files = [f for f in all_sched
                   if re.search(r'_(P[123])$', os.path.basename(f))]

    if not fun_files and not sched_files:
        print('P1/P2/P3 サフィックスを持つ FUN_*/SCHED_* ファイルが見つかりません。')
        print('NSGASplitCpp を実行してから再試行してください。')
        return

    print(f'\nFUN ファイル数  : {len(fun_files)}')
    print(f'SCHED ファイル数: {len(sched_files)}')

    # ---- 1. P1 vs P2 vs P3 パレート比較 ----
    if fun_files:
        print('\n--- [1/5] P1/P2/P3 パレートフロント比較 ---')
        plot_pareto_compare(fun_files, output_dir)

    # ---- 2. 同一モード × 全条件 ----
    if fun_files:
        print('\n--- [2/5] 同一モード・全条件 パレートフロント ---')
        plot_pareto_all_conditions(fun_files, output_dir)

    # ---- 3 & 4. ガントチャート & 資源使用量チャート ----
    if sched_files:
        print(f'\n--- [3/5 & 4/5] ガントチャート / 資源使用量チャート'
              f' ({len(sched_files)} ファイル) ---')
        for fp in sched_files:
            bn = os.path.basename(fp)
            prefix, ctag, mode = parse_sched_filename(bn)
            if prefix is None:
                continue
            try:
                nJobs, nRes, durations, demand, capacity, capacity_t, solutions = \
                    read_sched(fp)
                print(f'  {bn}  ({len(solutions)} 解)')
                plot_gantt(nJobs, nRes, durations, demand,
                           capacity, capacity_t,
                           solutions, prefix, ctag, mode, output_dir)
                plot_resource_usage(nJobs, nRes, durations, demand,
                                    capacity, capacity_t,
                                    solutions, prefix, ctag, mode, output_dir)
            except Exception as e:
                print(f'  [ERROR] {bn}: {e}')

    # ---- 5. サマリー棒グラフ ----
    if fun_files:
        print('\n--- [5/5] サマリー棒グラフ ---')
        plot_summary(fun_files, output_dir)

    # ---- 6. ハイパーボリューム比較 ----
    if fun_files:
        print('\n--- [6/6] ハイパーボリューム比較 ---')
        plot_hypervolume(fun_files, output_dir)

    print(f'\n完了。{output_dir}/ に保存されました。')


if __name__ == '__main__':
    main()