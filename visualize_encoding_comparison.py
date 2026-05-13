#!/usr/bin/env python3
"""
visualize_encoding_comparison.py
  SchedObj (0/1 エンコーディング) と MaxShift エンコーディングの
  NSGA-II 解を比較するビジュアライゼーション。

対象ファイル (cmake-build-*/ 直下):
  FUN_ENC_<prefix>_<cond>_SchedObj.txt
  FUN_ENC_<prefix>_<cond>_MaxShift.txt

生成グラフ:
  1. encoding_cmp_<prefix>_<cond>.png  (条件ごと)
       両エンコーディングのパレートフロント重ね表示
  2. encoding_summary_<prefix>.png  (全条件サマリ)
       Δ min-makespan / Δ min-cost のヒートマップ
       (MaxShift - SchedObj, 負 = MaxShift が優秀)

使い方:
  cd cmake-build-release   (または cmake-build-debug)
  python ../visualize_encoding_comparison.py

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
import matplotlib.patches as mpatches
import numpy as np
from collections import defaultdict

# ------------------------------------------------------------------ #
# 定数・スタイル
# ------------------------------------------------------------------ #

ENC_STYLES = {
    'SchedObj': dict(color='#1f77b4', marker='o', label='SchedObj (0/1 リスト)'),
    'MaxShift': dict(color='#d62728', marker='^', label='MaxShift リスト'),
}

CONDITIONS = [
    'RR000_RV0', 'RR000_RV1',
    'RR025_RV0', 'RR025_RV1',
    'RR050_RV0', 'RR050_RV1',
    'RR075_RV0', 'RR075_RV1',
]

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
    search_dir 以下から FUN_ENC_<prefix>_<cond>_<enc>.txt を探し
    {(cond, enc): filepath} のマップを返す。
    prefix が空の場合はワイルドカードで探索する。
    """
    if prefix:
        pattern = os.path.join(search_dir, f"FUN_ENC_{prefix}_*_*.txt")
    else:
        pattern = os.path.join(search_dir, "FUN_ENC_*_*_*.txt")

    result = {}
    for fp in glob.glob(pattern):
        bn = os.path.splitext(os.path.basename(fp))[0]  # FUN_ENC_<prefix>_<cond>_<enc>
        # パース: FUN_ENC_ の後を取り出す
        rest = bn[len("FUN_ENC_"):]
        # enc は末尾トークン (SchedObj / MaxShift)
        # cond は RR\d+_RV\d
        m = re.search(r'(RR\d+_RV\d)_(SchedObj|MaxShift)$', rest)
        if not m:
            continue
        cond = m.group(1)
        enc  = m.group(2)
        result[(cond, enc)] = fp
    return result


def stats(sols):
    """解集合から min_makespan, min_cost を返す。解がなければ (nan, nan)。"""
    if not sols:
        return float('nan'), float('nan')
    ms_vals   = [s[0] for s in sols]
    cost_vals = [s[1] for s in sols]
    return min(ms_vals), min(cost_vals)


# ------------------------------------------------------------------ #
# 1. 条件ごとの比較プロット
# ------------------------------------------------------------------ #

def plot_condition(cond, data, prefix, out_dir):
    """
    data: {'SchedObj': [(ms,c),...], 'MaxShift': [(ms,c),...]}
    """
    fig, ax = plt.subplots(figsize=(8, 6))

    cond_label = COND_LABELS.get(cond, cond)
    fig.suptitle(
        f"Encoding Comparison — {prefix}  [{cond_label}]",
        fontsize=13, fontweight='bold'
    )

    has_data = False
    for enc, style in ENC_STYLES.items():
        sols = data.get(enc, [])
        if not sols:
            continue
        has_data = True
        xs = [s[0] for s in sols]
        ys = [s[1] for s in sols]
        ax.scatter(xs, ys,
                   c=style['color'], marker=style['marker'],
                   s=50, alpha=0.75, label=style['label'], zorder=3)

    ax.set_xlabel('Makespan', fontsize=11)
    ax.set_ylabel('Total Resource Cost', fontsize=11)
    ax.set_title('Pareto Front', fontsize=11)
    ax.grid(True, linestyle='--', alpha=0.4)
    if has_data:
        ax.legend(fontsize=10)

    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"encoding_cmp_{prefix}_{cond}.png")
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  [saved] {out_path}")


# ------------------------------------------------------------------ #
# 2. 全条件サマリ (ヒートマップ)
# ------------------------------------------------------------------ #

def plot_summary(all_data, prefix, out_dir):
    """
    all_data: {cond: {'SchedObj': sols, 'MaxShift': sols}}
    Δ min-makespan と Δ min-cost のヒートマップを 1 枚に出力。
    Δ = MaxShift - SchedObj  (負 = MaxShift が優秀)
    """
    conds = [c for c in CONDITIONS if c in all_data]
    if not conds:
        return

    delta_ms   = []
    delta_cost = []
    ms_so      = []
    ms_ms_enc  = []
    cost_so    = []
    cost_ms_enc = []
    pareto_so  = []
    pareto_ms  = []

    for cond in conds:
        d = all_data[cond]
        mn_ms_so,   mn_c_so  = stats(d.get('SchedObj', []))
        mn_ms_mse,  mn_c_mse = stats(d.get('MaxShift', []))
        delta_ms.append(mn_ms_mse - mn_ms_so
                        if not (math.isnan(mn_ms_so) or math.isnan(mn_ms_mse))
                        else float('nan'))
        delta_cost.append(mn_c_mse - mn_c_so
                          if not (math.isnan(mn_c_so) or math.isnan(mn_c_mse))
                          else float('nan'))
        ms_so.append(mn_ms_so)
        ms_ms_enc.append(mn_ms_mse)
        cost_so.append(mn_c_so)
        cost_ms_enc.append(mn_c_mse)
        pareto_so.append(len(d.get('SchedObj', [])))
        pareto_ms.append(len(d.get('MaxShift', [])))

    n_conds = len(conds)
    cond_labels = [COND_LABELS.get(c, c) for c in conds]

    fig, axes = plt.subplots(1, 2, figsize=(14, max(4, n_conds * 0.7 + 2)))
    fig.suptitle(
        f"Encoding Comparison Summary — {prefix}\n"
        f"Δ = MaxShift − SchedObj  (負 = MaxShift が優秀)",
        fontsize=12, fontweight='bold'
    )

    def draw_heatmap(ax, values, title, fmt='.1f'):
        mat = np.array(values, dtype=float).reshape(-1, 1)
        vmax = max(abs(np.nanmax(mat)), abs(np.nanmin(mat)), 1.0)
        im = ax.imshow(mat, cmap='RdBu_r', vmin=-vmax, vmax=vmax,
                       aspect='auto')
        plt.colorbar(im, ax=ax, shrink=0.9)
        ax.set_xticks([])
        ax.set_yticks(range(n_conds))
        ax.set_yticklabels(cond_labels, fontsize=9)
        ax.set_title(title, fontsize=10)
        # セル内に値を表示
        for r in range(n_conds):
            v = values[r]
            if not math.isnan(v):
                sign = '+' if v > 0 else ''
                txt = f"{sign}{v:{fmt}}"
                tc  = 'white' if abs(v) > vmax * 0.65 else 'black'
                ax.text(0, r, txt, ha='center', va='center',
                        fontsize=9, color=tc, fontweight='bold')

    draw_heatmap(axes[0], delta_ms,   'Δ min-Makespan', fmt='.0f')
    draw_heatmap(axes[1], delta_cost, 'Δ min-Cost',     fmt='.0f')

    plt.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"encoding_summary_{prefix}.png")
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  [saved] {out_path}")

    # ---- コンソールにテキストサマリを表示 ----
    print(f"\n{'Condition':<16} {'MS(SO)':>8} {'MS(MS)':>8} {'ΔMS':>8}"
          f"  {'Cost(SO)':>10} {'Cost(MS)':>10} {'ΔCost':>10}"
          f"  {'Pareto(SO)':>10} {'Pareto(MS)':>10}")
    print('-' * 100)
    for i, cond in enumerate(conds):
        ms_s = f'{ms_so[i]:.0f}'      if not math.isnan(ms_so[i])      else 'N/A'
        ms_m = f'{ms_ms_enc[i]:.0f}'  if not math.isnan(ms_ms_enc[i])  else 'N/A'
        dms  = (f'{delta_ms[i]:+.0f}' if not math.isnan(delta_ms[i])   else 'N/A')
        cs_s = f'{cost_so[i]:.0f}'    if not math.isnan(cost_so[i])    else 'N/A'
        cs_m = f'{cost_ms_enc[i]:.0f}'if not math.isnan(cost_ms_enc[i])else 'N/A'
        dc   = (f'{delta_cost[i]:+.0f}'if not math.isnan(delta_cost[i]) else 'N/A')
        print(f'{cond:<16} {ms_s:>8} {ms_m:>8} {dms:>8}'
              f'  {cs_s:>10} {cs_m:>10} {dc:>10}'
              f'  {pareto_so[i]:>10} {pareto_ms[i]:>10}')
    print('-' * 100)
    print('  SO = SchedObj  MS = MaxShift  Δ = MaxShift - SchedObj')


# ------------------------------------------------------------------ #
# 3. 全条件一覧プロット (パレートフロント 8 条件を 1 枚に)
# ------------------------------------------------------------------ #

def plot_all_conditions(all_data, prefix, out_dir):
    """
    8 条件分のパレートフロントを 4×2 のグリッドで 1 枚に表示。
    両エンコーディングを重ねて描画する。
    """
    conds = [c for c in CONDITIONS if c in all_data]
    n = len(conds)
    if n == 0:
        return

    n_cols = 2
    n_rows = math.ceil(n / n_cols)
    fig, axes = plt.subplots(n_rows, n_cols,
                             figsize=(14, 4 * n_rows + 1))
    axes = np.array(axes).flatten()

    fig.suptitle(
        f"Encoding Comparison — All Conditions — {prefix}",
        fontsize=12, fontweight='bold'
    )

    for idx, cond in enumerate(conds):
        ax = axes[idx]
        d  = all_data[cond]
        has = False
        for enc, style in ENC_STYLES.items():
            sols = d.get(enc, [])
            if not sols:
                continue
            has = True
            xs = [s[0] for s in sols]
            ys = [s[1] for s in sols]
            ax.scatter(xs, ys, c=style['color'], marker=style['marker'],
                       s=30, alpha=0.7, label=style['label'], zorder=3)

        ax.set_title(COND_LABELS.get(cond, cond), fontsize=9)
        ax.set_xlabel('Makespan', fontsize=8)
        ax.set_ylabel('Cost',     fontsize=8)
        ax.grid(True, linestyle='--', alpha=0.3)
        ax.tick_params(labelsize=7)
        if has and idx == 0:
            ax.legend(fontsize=7)

    # 余ったサブプロットを非表示
    for idx in range(n, len(axes)):
        axes[idx].set_visible(False)

    plt.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"encoding_allconds_{prefix}.png")
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  [saved] {out_path}")


# ------------------------------------------------------------------ #
# main
# ------------------------------------------------------------------ #

def main():
    parser = argparse.ArgumentParser(
        description='SchedObj vs MaxShift エンコーディング比較プロット'
    )
    parser.add_argument('--prefix', default='',
                        help='インスタンスのプレフィックス (例: j301_1)。'
                             '省略時は全ファイルを自動検出。')
    parser.add_argument('--dir', default='.',
                        help='FUN_ENC_* ファイルがある作業ディレクトリ (default: .)')
    parser.add_argument('--out', default='figures/encoding_cmp',
                        help='出力ディレクトリ (default: figures/encoding_cmp)')
    args = parser.parse_args()

    search_dir = args.dir
    out_dir    = args.out
    prefix     = args.prefix

    print(f"[visualize_encoding_comparison]")
    print(f"  search_dir : {os.path.abspath(search_dir)}")
    print(f"  out_dir    : {os.path.abspath(out_dir)}")
    print(f"  prefix     : {prefix if prefix else '(auto-detect)'}")

    file_map = discover_files(search_dir, prefix)
    if not file_map:
        print("[ERROR] FUN_ENC_* ファイルが見つかりません。")
        print("  NSGAEncCpp を先に実行してください。")
        sys.exit(1)

    # prefix ごと・条件ごとにデータを整理
    # key: (detected_prefix, cond, enc) -> filepath
    # detected_prefix を自動抽出
    prefix_map = defaultdict(lambda: defaultdict(dict))
    # {detected_prefix: {cond: {enc: [(ms,c),...]}}}

    for (cond, enc), fp in file_map.items():
        bn   = os.path.splitext(os.path.basename(fp))[0]
        rest = bn[len("FUN_ENC_"):]
        m    = re.search(r'(RR\d+_RV\d)_(SchedObj|MaxShift)$', rest)
        if not m:
            continue
        det_prefix = rest[:m.start()].rstrip('_')
        sols = read_fun(fp)
        prefix_map[det_prefix][cond][enc] = sols

    if not prefix_map:
        print("[ERROR] ファイルを解析できませんでした。")
        sys.exit(1)

    for det_prefix, cond_data in sorted(prefix_map.items()):
        print(f"\n=== prefix: {det_prefix}  ({len(cond_data)} conditions) ===")

        for cond in CONDITIONS:
            if cond not in cond_data:
                continue
            d = cond_data[cond]
            # 両エンコーディングが揃っているか確認
            if len(d) < 2:
                missing = [e for e in ENC_STYLES if e not in d]
                print(f"  [SKIP] {cond}: {missing} のファイルがありません。")
                continue
            print(f"  Plotting {cond} ...")
            plot_condition(cond, d, det_prefix, out_dir)

        plot_summary(cond_data, det_prefix, out_dir)
        plot_all_conditions(cond_data, det_prefix, out_dir)

    print("\n[DONE]")


if __name__ == '__main__':
    main()
