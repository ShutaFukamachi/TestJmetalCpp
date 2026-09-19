#!/usr/bin/env python3
"""
visualize_all_comparison.py
  EncodingComparisonRunner_All の出力（P1/P2/P3 × SchedObj/MaxShift）を可視化する。

対象ファイル (cmake-build-*/ 直下):
  FUN_ENC_<prefix>_<cond>_<mode>_SchedObj.txt
  FUN_ENC_<prefix>_<cond>_<mode>_MaxShift.txt
  例: FUN_ENC_j301_1_RR000_RV0_P1_SchedObj.txt

生成グラフ:
  1. encoding_all_cmp_{prefix}_{cond}.png   (条件ごと)
       P1/P2/P3 を横並びで SchedObj vs MaxShift を重ねて表示

  2. encoding_all_summary_{prefix}.png      (全条件サマリ)
       Δ min-makespan / Δ min-cost のヒートマップ
       行 = 8 条件、列 = P1/P2/P3
       (Δ = MaxShift − SchedObj、負 = MaxShift が優秀)

  3. encoding_all_overview_{prefix}_{mode}.png  (スキームごと全条件一覧)
       P1/P2/P3 それぞれについて、8 条件のパレートフロントを 4×2 グリッドで表示

使い方:
  cd cmake-build-release   (または cmake-build-debug)
  python ../visualize_all_comparison.py
  python ../visualize_all_comparison.py --prefix j301_1 --out figures/all_cmp
"""

import os
import sys
import glob
import re
import argparse
import math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict

# ------------------------------------------------------------------ #
# 定数・スタイル
# ------------------------------------------------------------------ #

SPLIT_MODES = ['P1', 'P2', 'P3']

CONDITIONS = [
    'RR000_RV0', 'RR000_RV1',
    'RR025_RV0', 'RR025_RV1',
    'RR050_RV0', 'RR050_RV1',
    'RR075_RV0', 'RR075_RV1',
]

# RR=0.75 は MIP・NSGA とも実務的に破綻する限界条件のため、既定の一括実行からは除外する。
# --cond で明示指定すれば従来どおり個別に描画できる。
DEFAULT_CONDITIONS = [c for c in CONDITIONS if not c.startswith('RR075')]

COND_LABELS = {
    'RR000_RV0': 'RR=0.00 RV=off',
    'RR000_RV1': 'RR=0.00 RV=on',
    'RR025_RV0': 'RR=0.25 RV=off',
    'RR025_RV1': 'RR=0.25 RV=on',
    'RR050_RV0': 'RR=0.50 RV=off',
    'RR050_RV1': 'RR=0.50 RV=on',
    'RR075_RV0': 'RR=0.75 RV=off',
    'RR075_RV1': 'RR=0.75 RV=on',
}

ENC_STYLES = {
    'SchedObj':    dict(color='#1f77b4', marker='o', label='SchedObj (0/1 list)'),
    'MaxShift':    dict(color='#d62728', marker='^', label='MaxShift'),
    'MaxShift_NF': dict(color='#ff7f0e', marker='*', s=80,
                        label='MaxShift + Novelty Filter (試験)'),
}

MODE_COLORS = {'P1': '#2ca02c', 'P2': '#ff7f0e', 'P3': '#9467bd'}


# ------------------------------------------------------------------ #
# ファイル読み込み
# ------------------------------------------------------------------ #

def read_fun(path):
    """FUN_ENC_* ファイルを読み込み [(makespan, cost), ...] を返す。"""
    sols = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                ms, c = float(parts[0]), float(parts[1])
                if ms < 1e8 and c < 1e8:
                    sols.append((ms, c))
    return sols


def discover_files(search_dir, prefix):
    """
    FUN_ENC_<prefix>_<cond>_<mode>_<enc>.txt を探し
    {(inst_prefix, cond, mode, enc): filepath} を返す。
    複数インスタンスが混在していても上書きされない。
    """
    pat = os.path.join(search_dir, f"FUN_ENC_{prefix}_*.txt" if prefix else "FUN_ENC_*.txt")
    result = {}
    re_pat = re.compile(r'(RR\d+_RV\d)_(P[123])_(SchedObj|MaxShift(?:_NF)?)$')
    for fp in glob.glob(pat):
        bn   = os.path.splitext(os.path.basename(fp))[0]
        rest = bn[len("FUN_ENC_"):]
        m = re_pat.search(rest)
        if not m:
            continue
        cond, mode, enc = m.group(1), m.group(2), m.group(3)
        inst_prefix = rest[:m.start()].rstrip('_')
        result[(inst_prefix, cond, mode, enc)] = fp
    return result


def stats(sols):
    if not sols:
        return float('nan'), float('nan')
    return min(s[0] for s in sols), min(s[1] for s in sols)


# ------------------------------------------------------------------ #
# 1. 条件ごとの比較プロット (P1/P2/P3 横並び)
# ------------------------------------------------------------------ #

def plot_condition(cond, mode_enc_data, prefix, out_dir):
    """
    mode_enc_data: {mode: {enc: [(ms,c),...]}}
    横軸 = P1/P2/P3、各サブプロットに SchedObj vs MaxShift
    """
    modes = [m for m in SPLIT_MODES if m in mode_enc_data]
    if not modes:
        return

    n_cols = len(modes)
    fig, axes = plt.subplots(1, n_cols, figsize=(5 * n_cols, 5))
    if n_cols == 1:
        axes = [axes]

    cond_label = COND_LABELS.get(cond, cond)
    fig.suptitle(
        f"Encoding Comparison — {prefix}  [{cond_label}]",
        fontsize=12, fontweight='bold'
    )

    for ax, mode in zip(axes, modes):
        d = mode_enc_data[mode]
        has = False
        for enc, style in ENC_STYLES.items():
            sols = d.get(enc, [])
            if not sols:
                continue
            has = True
            xs = [s[0] for s in sols]
            ys = [s[1] for s in sols]
            ax.scatter(xs, ys,
                       c=style['color'], marker=style['marker'],
                       s=style.get('s', 50), alpha=0.75, label=style['label'], zorder=3)

        ax.set_title(mode, fontsize=11, color=MODE_COLORS.get(mode, 'black'),
                     fontweight='bold')
        ax.set_xlabel('Makespan', fontsize=9)
        ax.set_ylabel('Total Resource Cost', fontsize=9)
        ax.grid(True, linestyle='--', alpha=0.4)
        if has:
            ax.legend(fontsize=8)

    plt.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"encoding_all_cmp_{prefix}_{cond}.png")
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  [saved] {out_path}")


# ------------------------------------------------------------------ #
# 2. 全条件サマリ ヒートマップ (行=条件, 列=P1/P2/P3)
# ------------------------------------------------------------------ #

def plot_summary(all_data, prefix, out_dir):
    """
    all_data: {cond: {mode: {enc: [(ms,c),...]}}}
    Δ = MaxShift - SchedObj のヒートマップ (2 枚: Δ makespan / Δ cost)
    """
    conds = [c for c in CONDITIONS if c in all_data]
    modes = [m for m in SPLIT_MODES if any(m in all_data[c] for c in conds)]
    if not conds or not modes:
        return

    n_conds = len(conds)
    n_modes = len(modes)

    delta_ms   = np.full((n_conds, n_modes), np.nan)
    delta_cost = np.full((n_conds, n_modes), np.nan)
    pareto_so  = np.zeros((n_conds, n_modes), dtype=int)
    pareto_ms  = np.zeros((n_conds, n_modes), dtype=int)

    for ci, cond in enumerate(conds):
        for mi, mode in enumerate(modes):
            d = all_data[cond].get(mode, {})
            mn_ms_so,  mn_c_so  = stats(d.get('SchedObj', []))
            mn_ms_mse, mn_c_mse = stats(d.get('MaxShift', []))
            if not (math.isnan(mn_ms_so) or math.isnan(mn_ms_mse)):
                delta_ms[ci, mi]   = mn_ms_mse   - mn_ms_so
                delta_cost[ci, mi] = mn_c_mse    - mn_c_so
            pareto_so[ci, mi] = len(d.get('SchedObj', []))
            pareto_ms[ci, mi] = len(d.get('MaxShift', []))

    cond_labels = [COND_LABELS.get(c, c) for c in conds]

    fig, axes = plt.subplots(1, 2, figsize=(5 + n_modes * 1.8, max(4, n_conds * 0.75 + 2.5)))
    fig.suptitle(
        f"Encoding Comparison Summary — {prefix}\n"
        f"Delta = MaxShift - SchedObj   (negative = MaxShift better)",
        fontsize=12, fontweight='bold'
    )

    def draw_heatmap(ax, mat, title, fmt='.0f'):
        vmax = max(np.nanmax(np.abs(mat)), 1.0)
        im = ax.imshow(mat, cmap='RdBu_r', vmin=-vmax, vmax=vmax, aspect='auto')
        plt.colorbar(im, ax=ax, shrink=0.85)
        ax.set_xticks(range(n_modes))
        ax.set_xticklabels(modes, fontsize=10)
        ax.set_yticks(range(n_conds))
        ax.set_yticklabels(cond_labels, fontsize=9)
        ax.set_title(title, fontsize=10, fontweight='bold')
        for ci in range(n_conds):
            for mi in range(n_modes):
                v = mat[ci, mi]
                if not math.isnan(v):
                    sign = '+' if v > 0 else ''
                    txt = f"{sign}{v:{fmt}}"
                    tc  = 'white' if abs(v) > vmax * 0.65 else 'black'
                    ax.text(mi, ci, txt, ha='center', va='center',
                            fontsize=9, color=tc, fontweight='bold')

    draw_heatmap(axes[0], delta_ms,   'Δ min-Makespan')
    draw_heatmap(axes[1], delta_cost, 'Δ min-Cost', fmt='.1f')

    plt.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"encoding_all_summary_{prefix}.png")
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  [saved] {out_path}")

    # コンソールサマリ
    col_w = max(len(m) * 2 + 14 for m in modes)
    header = f"{'Condition':<16}"
    for mode in modes:
        header += f"  {mode}_SO_ms {mode}_MS_ms {mode}_ΔMS  {mode}_ΔCost"
    print(f"\n{header}")
    print('-' * (16 + len(modes) * 42))
    for ci, cond in enumerate(conds):
        row = f"{cond:<16}"
        for mi, mode in enumerate(modes):
            d = all_data[cond].get(mode, {})
            ms_s, c_s = stats(d.get('SchedObj', []))
            ms_m, c_m = stats(d.get('MaxShift', []))
            dms  = delta_ms[ci, mi]
            dcost= delta_cost[ci, mi]
            row += (
                f"  {ms_s:>6.0f} {ms_m:>6.0f} "
                f"{('+' if dms   >= 0 else '') + f'{dms:.0f}':>5}  "
                f"{('+' if dcost >= 0 else '') + f'{dcost:.1f}':>7}"
                if not math.isnan(dms) else f"  {'N/A':>6} {'N/A':>6} {'N/A':>5}  {'N/A':>7}"
            )
        print(row)
    print('-' * (16 + len(modes) * 42))
    print('  Delta = MaxShift - SchedObj  (negative = MaxShift better)')


# ------------------------------------------------------------------ #
# 3. スキームごと・全条件一覧プロット (4×2 グリッド)
# ------------------------------------------------------------------ #

def plot_overview_per_mode(all_data, prefix, mode, out_dir):
    """
    指定 split mode について 8 条件分を 4×2 グリッドで表示。
    """
    conds = [c for c in CONDITIONS if c in all_data and mode in all_data[c]]
    if not conds:
        return

    n = len(conds)
    n_cols = 2
    n_rows = math.ceil(n / n_cols)
    fig, axes = plt.subplots(n_rows, n_cols,
                             figsize=(12, 4 * n_rows + 1))
    axes = np.array(axes).flatten()

    fig.suptitle(
        f"Encoding Comparison [{mode}] — All Conditions — {prefix}",
        fontsize=12, fontweight='bold',
        color=MODE_COLORS.get(mode, 'black')
    )

    for idx, cond in enumerate(conds):
        ax = axes[idx]
        d  = all_data[cond][mode]
        has = False
        for enc, style in ENC_STYLES.items():
            sols = d.get(enc, [])
            if not sols:
                continue
            has = True
            xs = [s[0] for s in sols]
            ys = [s[1] for s in sols]
            ax.scatter(xs, ys,
                       c=style['color'], marker=style['marker'],
                       s=30, alpha=0.75, label=style['label'], zorder=3)
            # min-makespan と min-cost の点を強調
            min_ms_pt   = min(sols, key=lambda p: p[0])
            min_cost_pt = min(sols, key=lambda p: p[1])
            ax.scatter(*min_ms_pt,   c=style['color'], marker='*', s=120,
                       edgecolors='black', linewidths=0.5, zorder=5)
            ax.scatter(*min_cost_pt, c=style['color'], marker='D', s=60,
                       edgecolors='black', linewidths=0.5, zorder=5)

        ax.set_title(COND_LABELS.get(cond, cond), fontsize=9)
        ax.set_xlabel('Makespan', fontsize=8)
        ax.set_ylabel('Cost',     fontsize=8)
        ax.grid(True, linestyle='--', alpha=0.3)
        ax.tick_params(labelsize=7)
        if has and idx == 0:
            ax.legend(fontsize=7)

    for idx in range(n, len(axes)):
        axes[idx].set_visible(False)

    # 凡例の補足（★ = min-makespan, ◆ = min-cost）
    fig.text(0.5, 0.01,
             '★ = min-makespan point  ◆ = min-cost point',
             ha='center', fontsize=8, color='gray')

    plt.tight_layout(rect=[0, 0.03, 1, 1])
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"encoding_all_overview_{prefix}_{mode}.png")
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  [saved] {out_path}")


# ------------------------------------------------------------------ #
# 4. P1/P2/P3 スキーム比較プロット（同一エンコーディングで重ねる）
# ------------------------------------------------------------------ #

def plot_scheme_comparison(all_data, prefix, out_dir):
    """
    各条件で P1/P2/P3 のパレートフロントを重ね表示。
    SchedObj と MaxShift を別サブプロットで横並び。
    """
    conds = [c for c in CONDITIONS if c in all_data]
    if not conds:
        return

    n_cols = 2   # SchedObj / MaxShift
    n_rows = len(conds)
    fig, axes = plt.subplots(n_rows, n_cols,
                             figsize=(12, 3.5 * n_rows + 1))
    if n_rows == 1:
        axes = axes.reshape(1, -1)

    fig.suptitle(
        f"P1 / P2 / P3 Scheme Comparison — {prefix}",
        fontsize=12, fontweight='bold'
    )

    enc_list = ['SchedObj', 'MaxShift']
    for ri, cond in enumerate(conds):
        for ci, enc in enumerate(enc_list):
            ax = axes[ri][ci]
            has = False
            for mode in SPLIT_MODES:
                sols = all_data[cond].get(mode, {}).get(enc, [])
                if not sols:
                    continue
                has = True
                xs = [s[0] for s in sols]
                ys = [s[1] for s in sols]
                ax.scatter(xs, ys,
                           c=MODE_COLORS[mode], marker='o',
                           s=30, alpha=0.7, label=mode, zorder=3)

            if ri == 0:
                ax.set_title(f"{enc}", fontsize=10, fontweight='bold',
                             color=ENC_STYLES[enc]['color'])
            ax.set_ylabel(COND_LABELS.get(cond, cond), fontsize=7, rotation=90)
            ax.set_xlabel('Makespan', fontsize=7)
            ax.grid(True, linestyle='--', alpha=0.3)
            ax.tick_params(labelsize=6)
            if has and ri == 0 and ci == 0:
                ax.legend(fontsize=7, title='Scheme')

    plt.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"encoding_all_scheme_cmp_{prefix}.png")
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  [saved] {out_path}")


# ------------------------------------------------------------------ #
# main
# ------------------------------------------------------------------ #

def main():
    parser = argparse.ArgumentParser(
        description='P1/P2/P3 × SchedObj/MaxShift エンコーディング比較プロット'
    )
    parser.add_argument('--prefix', default='',
                        help='インスタンスのプレフィックス (例: j301_1)。'
                             '省略時は全ファイルを自動検出。')
    parser.add_argument('--dir', default='results/FUN_ENC',
                        help='FUN_ENC_* ファイルがある作業ディレクトリ (default: results/FUN_ENC)')
    parser.add_argument('--out', default='figures/all_cmp',
                        help='出力ディレクトリ (default: figures/all_cmp)')
    parser.add_argument('--cond', default=None,
                        help='条件タグ (例: RR075_RV0)。省略時は既定条件（RR<=0.50）のみ対象')
    args = parser.parse_args()

    search_dir = args.dir
    out_dir    = args.out
    prefix     = args.prefix

    print('[visualize_all_comparison]')
    print(f'  search_dir : {os.path.abspath(search_dir)}')
    print(f'  out_dir    : {os.path.abspath(out_dir)}')
    print(f'  prefix     : {prefix if prefix else "(auto-detect)"}')

    file_map = discover_files(search_dir, prefix)
    if not file_map:
        print('[ERROR] FUN_ENC_*_P[123]_*.txt ファイルが見つかりません。')
        print('  NSGAEncCpp (EncodingComparisonRunner_All) を先に実行してください。')
        sys.exit(1)

    if args.cond:
        file_map = {k: v for k, v in file_map.items() if k[1] == args.cond}
    else:
        file_map = {k: v for k, v in file_map.items() if k[1] in DEFAULT_CONDITIONS}
    if not file_map:
        print(f'[ERROR] 条件フィルタ後にファイルが残りません（--cond={args.cond}）。')
        sys.exit(1)

    # {detected_prefix: {cond: {mode: {enc: [(ms,c),...]}}}}
    prefix_map = defaultdict(lambda: defaultdict(lambda: defaultdict(dict)))

    for (inst_prefix, cond, mode, enc), fp in file_map.items():
        sols = read_fun(fp)
        prefix_map[inst_prefix][cond][mode][enc] = sols

    if not prefix_map:
        print('[ERROR] ファイルを解析できませんでした。')
        sys.exit(1)

    for det_prefix, all_data in sorted(prefix_map.items()):
        conds_found = [c for c in CONDITIONS if c in all_data]
        modes_found = sorted({m for c in all_data for m in all_data[c]})
        print(f'\n=== prefix: {det_prefix}  ({len(conds_found)} conditions, modes: {modes_found}) ===')

        # 1. 条件ごとのプロット
        print('\n-- 1. Per-condition plots --')
        for cond in conds_found:
            print(f'  Plotting {cond} ...')
            plot_condition(cond, all_data[cond], det_prefix, out_dir)

        # 2. 全条件サマリ ヒートマップ
        print('\n-- 2. Summary heatmap --')
        plot_summary(all_data, det_prefix, out_dir)

        # 3. スキームごと全条件一覧
        print('\n-- 3. Per-mode overview --')
        for mode in SPLIT_MODES:
            if any(mode in all_data[c] for c in conds_found):
                plot_overview_per_mode(all_data, det_prefix, mode, out_dir)

        # 4. P1/P2/P3 スキーム比較
        print('\n-- 4. Scheme comparison --')
        plot_scheme_comparison(all_data, det_prefix, out_dir)

    print('\n[DONE]')


if __name__ == '__main__':
    main()
