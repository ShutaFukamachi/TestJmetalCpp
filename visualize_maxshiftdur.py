#!/usr/bin/env python3
"""
visualize_maxshiftdur.py
  MaxShiftDur エンコーディング（所要時間ベース遅延スケーリング）の
  NSGA-II Pareto フロントを可視化する。

  出力スタイル:
    1×3 サブプロット (P1 / P2 / P3) の横並びレイアウト。
    各サブプロットに MaxShiftDur / MaxShift / SchedObj を重ねて散布表示。

  対象ファイル (build_dir 直下):
    MaxShiftDur: FUN_ENC_<prefix>_<cond>_MaxShiftDur.txt      (P1)
                 FUN_ENC_<prefix>_<cond>_P2_MaxShiftDur.txt   (P2)
                 FUN_ENC_<prefix>_<cond>_P3_MaxShiftDur.txt   (P3)
    MaxShift:    FUN_ENC_<prefix>_<cond>_P1_MaxShift.txt  or  _MaxShift.txt
                 FUN_ENC_<prefix>_<cond>_P2_MaxShift.txt
                 FUN_ENC_<prefix>_<cond>_P3_MaxShift.txt
    SchedObj:    FUN_ENC_<prefix>_<cond>_P1_SchedObj.txt  or  _SchedObj.txt
                 FUN_ENC_<prefix>_<cond>_P2_SchedObj.txt
                 FUN_ENC_<prefix>_<cond>_P3_SchedObj.txt

使い方:
  cd cmake-build-release
  python ../visualize_maxshiftdur.py
  python ../visualize_maxshiftdur.py --prefix j3011_1 --cond RR000_RV0

オプション:
  --build-dir DIR   ビルドディレクトリのパス (デフォルト: cmake-build-release)
  --prefix PREFIX   インスタンス prefix (例: j3011_1)  省略時は自動検出
  --cond COND       条件タグ (例: RR000_RV0)            省略時は自動検出
  --no-compare      MaxShift/SchedObj との比較を無効化

出力:
  <build_dir>/figures/MaxShiftDur/encoding_maxshiftdur_<prefix>_<cond>.png
"""

import os
import sys
import glob
import re
import argparse
import math

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict

# ============================================================
#  定数・スタイル
# ============================================================
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

# エンコーディングのスタイル（画像のスタイルに合わせる）
ENC_STYLES = {
    'MaxShiftDur': dict(color='#e63946', marker='D', s=45,  label='MaxShiftDur', zorder=5),
    'MaxShift':    dict(color='#1f77b4', marker='^', s=40,  label='MaxShift',    zorder=4),
    'SchedObj':    dict(color='#2ca02c', marker='o', s=35,  label='SchedObj',    zorder=3),
}

MODE_COLORS = {'P1': '#2ca02c', 'P2': '#ff7f0e', 'P3': '#9467bd'}


# ============================================================
#  ファイル読み込み
# ============================================================
def read_fun(path):
    """FUN_ENC_* ファイルを読み込み [(makespan, cost), ...] を返す。"""
    sols = []
    try:
        with open(path) as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 2:
                    ms, c = float(parts[0]), float(parts[1])
                    if ms < 1e8 and c < 1e8:
                        sols.append((ms, c))
    except FileNotFoundError:
        pass
    return sols


def pareto_filter(pts):
    """非支配フィルタ（最小化 × 2 目的）を適用して Pareto フロントを返す。"""
    if not pts:
        return []
    sorted_pts = sorted(pts, key=lambda p: (p[0], p[1]))
    pareto = []
    best_cost = math.inf
    for ms, cost in sorted_pts:
        if cost < best_cost:
            pareto.append((ms, cost))
            best_cost = cost
    return pareto


# ============================================================
#  ファイル検出
# ============================================================
def _cond_allowed(cond, cond_filter):
    """cond_filter 指定時はそれと一致する条件のみ、未指定時は既定条件（RR<=0.50）のみ許可。"""
    if cond_filter:
        return cond == cond_filter
    return cond in DEFAULT_CONDITIONS


def discover_maxshiftdur_files(build_dir, prefix_filter, cond_filter):
    """
    FUN_ENC_*MaxShiftDur*.txt を検索し
    {(prefix, cond): {'P1': path, 'P2': path, 'P3': path}} を返す。
    """
    result = defaultdict(dict)

    # MaxShiftDur P1: FUN_ENC_{prefix}_{cond}_MaxShiftDur.txt
    pat_p1 = os.path.join(build_dir, 'FUN_ENC_*_MaxShiftDur.txt')
    re_p1  = re.compile(r'^FUN_ENC_(.+)_(RR\d+_RV\d)_MaxShiftDur$')

    for fp in glob.glob(pat_p1):
        bn = os.path.splitext(os.path.basename(fp))[0]
        m  = re_p1.match(bn)
        if not m:
            continue
        pfx, cond = m.group(1), m.group(2)
        if prefix_filter and pfx != prefix_filter:
            continue
        if not _cond_allowed(cond, cond_filter):
            continue
        result[(pfx, cond)]['P1'] = fp

    # MaxShiftDur P2/P3: FUN_ENC_{prefix}_{cond}_{P2|P3}_MaxShiftDur.txt
    for split in ['P2', 'P3']:
        pat = os.path.join(build_dir, f'FUN_ENC_*_{split}_MaxShiftDur.txt')
        re_ = re.compile(rf'^FUN_ENC_(.+)_(RR\d+_RV\d)_{split}_MaxShiftDur$')
        for fp in glob.glob(pat):
            bn = os.path.splitext(os.path.basename(fp))[0]
            m  = re_.match(bn)
            if not m:
                continue
            pfx, cond = m.group(1), m.group(2)
            if prefix_filter and pfx != prefix_filter:
                continue
            if not _cond_allowed(cond, cond_filter):
                continue
            result[(pfx, cond)][split] = fp

    return result


def load_compare_data(build_dir, prefix, cond):
    """
    MaxShift / SchedObj の P1/P2/P3 ファイルを探し
    {'MaxShift': {'P1': [...], 'P2': [...], ...}, 'SchedObj': {...}} を返す。
    """
    data = {'MaxShift': {}, 'SchedObj': {}}
    for enc in ['MaxShift', 'SchedObj']:
        # P1: _P1_{enc}.txt または _{enc}.txt
        p1_candidates = [
            os.path.join(build_dir, f'FUN_ENC_{prefix}_{cond}_P1_{enc}.txt'),
            os.path.join(build_dir, f'FUN_ENC_{prefix}_{cond}_{enc}.txt'),
        ]
        for cp in p1_candidates:
            if os.path.exists(cp):
                pts = read_fun(cp)
                if pts:
                    data[enc]['P1'] = pts
                break
        # P2/P3
        for split in ['P2', 'P3']:
            cp = os.path.join(build_dir, f'FUN_ENC_{prefix}_{cond}_{split}_{enc}.txt')
            if os.path.exists(cp):
                pts = read_fun(cp)
                if pts:
                    data[enc][split] = pts
    return data


# ============================================================
#  条件ごとプロット: 1×3 サブプロット (P1/P2/P3)
# ============================================================
def plot_condition(prefix, cond, maxshiftdur_paths, compare_data, out_dir):
    """
    P1/P2/P3 を横並びで MaxShiftDur / MaxShift / SchedObj を重ねて散布表示。
    """
    # 利用可能なモードを確認
    modes_avail = []
    for m in SPLIT_MODES:
        has_dur = m in maxshiftdur_paths
        has_cmp = any(m in compare_data[enc] for enc in compare_data)
        if has_dur or has_cmp:
            modes_avail.append(m)

    if not modes_avail:
        print(f"  [SKIP] {prefix}/{cond}: データなし")
        return

    n_cols = len(modes_avail)
    fig, axes = plt.subplots(1, n_cols, figsize=(5.5 * n_cols, 5.2))
    if n_cols == 1:
        axes = [axes]

    cond_label = COND_LABELS.get(cond, cond)
    fig.suptitle(
        f"Encoding Comparison — {prefix}  [{cond_label}]",
        fontsize=13, fontweight='bold'
    )

    summary_rows = []

    for ax, mode in zip(axes, modes_avail):
        ax.set_title(mode, fontsize=12,
                     color=MODE_COLORS.get(mode, 'black'),
                     fontweight='bold')
        ax.set_xlabel('Makespan', fontsize=10)
        ax.set_ylabel('Total Resource Cost', fontsize=10)
        ax.grid(True, linestyle='--', alpha=0.4)

        has_any = False

        # MaxShiftDur
        if mode in maxshiftdur_paths:
            pts = read_fun(maxshiftdur_paths[mode])
            pf  = pareto_filter(pts)
            if pf:
                has_any = True
                sty = ENC_STYLES['MaxShiftDur']
                xs = [p[0] for p in pf]
                ys = [p[1] for p in pf]
                ax.scatter(xs, ys,
                           c=sty['color'], marker=sty['marker'],
                           s=sty['s'], alpha=0.85,
                           label=sty['label'], zorder=sty['zorder'],
                           edgecolors='black', linewidths=0.4)
                summary_rows.append({
                    'mode': mode, 'enc': 'MaxShiftDur',
                    'n': len(pf), 'min_ms': min(xs), 'min_cost': min(ys)
                })

        # MaxShift / SchedObj
        for enc in ['MaxShift', 'SchedObj']:
            pts = compare_data.get(enc, {}).get(mode, [])
            pf  = pareto_filter(pts)
            if pf:
                has_any = True
                sty = ENC_STYLES[enc]
                xs = [p[0] for p in pf]
                ys = [p[1] for p in pf]
                ax.scatter(xs, ys,
                           c=sty['color'], marker=sty['marker'],
                           s=sty['s'], alpha=0.85,
                           label=sty['label'], zorder=sty['zorder'],
                           edgecolors='black', linewidths=0.4)
                summary_rows.append({
                    'mode': mode, 'enc': enc,
                    'n': len(pf), 'min_ms': min(xs), 'min_cost': min(ys)
                })

        if has_any:
            ax.legend(fontsize=8, loc='upper right')

    plt.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"encoding_maxshiftdur_{prefix}_{cond}.png")
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  [SAVED] {out_path}")

    # テキストサマリ
    print(f"\n  === {prefix} / {cond} ===")
    print(f"  {'Mode':<4} {'Encoding':<14} {'|PF|':>6} {'min_makespan':>14} {'min_cost':>14}")
    print("  " + "-" * 56)
    for row in summary_rows:
        print(f"  {row['mode']:<4} {row['enc']:<14} {row['n']:>6} "
              f"{int(row['min_ms']):>14} {row['min_cost']:>14.1f}")


# ============================================================
#  全条件サマリグラフ (4×2 グリッド)
# ============================================================
def plot_overview(prefix, all_groups, all_compare, out_dir):
    """
    全条件 × P1/P2/P3 の Pareto front を概観するグラフ（任意モード）。
    MaxShiftDur-P1 と MaxShift-P1 のみで 4×2 グリッド表示。
    """
    conds = [c for c in CONDITIONS
             if c in {cond for (_, cond) in all_groups}]
    if not conds:
        return

    mode = 'P1'
    n = len(conds)
    n_cols = 2
    n_rows = math.ceil(n / n_cols)
    fig, axes = plt.subplots(n_rows, n_cols, figsize=(12, 4 * n_rows + 1))
    axes = np.array(axes).flatten()

    fig.suptitle(
        f"MaxShiftDur vs MaxShift [P1] — All Conditions — {prefix}",
        fontsize=12, fontweight='bold'
    )

    for idx, cond in enumerate(conds):
        ax = axes[idx]
        has = False

        key = (prefix, cond)
        if key in all_groups and mode in all_groups[key]:
            pts = read_fun(all_groups[key][mode])
            pf  = pareto_filter(pts)
            if pf:
                has = True
                sty = ENC_STYLES['MaxShiftDur']
                ax.scatter([p[0] for p in pf], [p[1] for p in pf],
                           c=sty['color'], marker=sty['marker'],
                           s=30, alpha=0.8, label='MaxShiftDur', zorder=5,
                           edgecolors='black', linewidths=0.3)

        cmp_pts = all_compare.get((prefix, cond), {}).get('MaxShift', {}).get(mode, [])
        pf_cmp  = pareto_filter(cmp_pts)
        if pf_cmp:
            has = True
            sty = ENC_STYLES['MaxShift']
            ax.scatter([p[0] for p in pf_cmp], [p[1] for p in pf_cmp],
                       c=sty['color'], marker=sty['marker'],
                       s=25, alpha=0.75, label='MaxShift', zorder=4,
                       edgecolors='black', linewidths=0.3)

        ax.set_title(COND_LABELS.get(cond, cond), fontsize=9)
        ax.set_xlabel('Makespan', fontsize=8)
        ax.set_ylabel('Cost', fontsize=8)
        ax.grid(True, linestyle='--', alpha=0.3)
        ax.tick_params(labelsize=7)
        if has and idx == 0:
            ax.legend(fontsize=7)

    for idx in range(n, len(axes)):
        axes[idx].set_visible(False)

    plt.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"encoding_maxshiftdur_overview_{prefix}.png")
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  [SAVED] {out_path}")


# ============================================================
#  メイン
# ============================================================
def main():
    parser = argparse.ArgumentParser(description='MaxShiftDur Pareto front visualizer')
    parser.add_argument('--build-dir', default='cmake-build-release',
                        help='ビルドディレクトリのパス')
    parser.add_argument('--prefix',   default=None,
                        help='インスタンス prefix（例: j3011_1）')
    parser.add_argument('--cond',     default=None,
                        help='条件タグ（例: RR000_RV0）')
    parser.add_argument('--no-compare', action='store_true',
                        help='MaxShift/SchedObj との比較を無効化')
    args = parser.parse_args()

    build_dir = args.build_dir
    if not os.path.isdir(build_dir):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        build_dir  = os.path.join(script_dir, build_dir)

    print(f"[INFO] build_dir = {build_dir}")

    # 出力先: <build_dir>/figures/MaxShiftDur/
    out_dir = os.path.join(build_dir, 'figures', 'MaxShiftDur')
    os.makedirs(out_dir, exist_ok=True)
    print(f"[INFO] figures dir = {out_dir}")

    # MaxShiftDur ファイルを検索
    groups = discover_maxshiftdur_files(build_dir, args.prefix, args.cond)

    if not groups:
        print("[ERROR] FUN_ENC_*MaxShiftDur*.txt が見つかりません。")
        print(f"  build_dir={build_dir} を確認してください。")
        sys.exit(1)

    print(f"[INFO] (prefix, cond) グループ数: {len(groups)}")

    # 比較データをキャッシュ
    compare_cache = {}

    # 条件ごとにプロット
    all_groups  = groups  # for overview
    all_compare = {}

    for (pfx, cond) in sorted(groups.keys()):
        key = (pfx, cond)
        if not args.no_compare:
            compare_cache[key] = load_compare_data(build_dir, pfx, cond)
            # 比較データの読み込み状況をログ
            for enc in ['MaxShift', 'SchedObj']:
                for mode, pts in compare_cache[key].get(enc, {}).items():
                    if pts:
                        print(f"[INFO] 比較データ: {pfx}/{cond}/{mode}/{enc} ({len(pts)} 解)")
        else:
            compare_cache[key] = {'MaxShift': {}, 'SchedObj': {}}

        all_compare[key] = compare_cache[key]

        plot_condition(pfx, cond, groups[key], compare_cache[key], out_dir)

    # 全条件概観グラフ（prefix ごと）
    prefixes = sorted(set(pfx for (pfx, _) in groups.keys()))
    for pfx in prefixes:
        plot_overview(pfx, all_groups, all_compare, out_dir)

    print("\n[DONE]")


if __name__ == '__main__':
    main()
