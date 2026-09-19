#!/usr/bin/env python3
"""
visualize_mip_comparison.py
──────────────────────────────────────────────────────────────────────────
MaxShiftDur / MaxShift (NSGA-II) と MIP（厳密解）の
Pareto フロント 3 系列比較。

出力 (build_dir/figures/MIPComparison/):
  mip_vs_{prefix}_{cond}.png   条件ごと 1 ファイル

使い方:
  cd cmake-build-release
  python ../visualize_mip_comparison.py
  python ../visualize_mip_comparison.py --prefix j3011_1 --cond RR000_RV0
  python ../visualize_mip_comparison.py --build-dir cmake-build-release
"""

import os, sys, glob, re, math, argparse
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# ──────────────────────────────────────────────────────────────────────────
#  定数・スタイル
# ──────────────────────────────────────────────────────────────────────────
CONDITIONS = [
    'RR000_RV0', 'RR000_RV1',
    'RR025_RV0', 'RR025_RV1',
    'RR050_RV0', 'RR050_RV1',
    'RR075_RV0', 'RR075_RV1',
]

# RR=0.75 は MIP・NSGA とも実務的に破綻する限界条件のため、既定の一括実行からは除外する
# （MIP: 変数膨張でSolCount=0になる条件あり／NSGA: フロントが単一makespanに退化）。
# --cond で明示指定すれば従来どおり個別に描画できる。
DEFAULT_CONDITIONS = [c for c in CONDITIONS if not c.startswith('RR075')]

COND_LABELS = {
    'RR000_RV0': 'RR=0.00 RV=off', 'RR000_RV1': 'RR=0.00 RV=on',
    'RR025_RV0': 'RR=0.25 RV=off', 'RR025_RV1': 'RR=0.25 RV=on',
    'RR050_RV0': 'RR=0.50 RV=off', 'RR050_RV1': 'RR=0.50 RV=on',
    'RR075_RV0': 'RR=0.75 RV=off', 'RR075_RV1': 'RR=0.75 RV=on',
}

# 比較する 4 系列の定義
SERIES = [
    dict(key='MIP',          color='#111111', marker='*', ms=11,
         label='MIP (Exact)',                    zorder=10, line=True),
    dict(key='SchedObj',     color='#2ca02c', marker='o', ms=7,
         label='SchedObj (NSGA-II)',              zorder=5, line=False),
    dict(key='MaxShift',     color='#6495ED', marker='^', ms=8,
         label='MaxShift (NSGA-II)',              zorder=6, line=False),
    dict(key='MaxShiftDur',  color='#d62728', marker='D', ms=8,
         label='MaxShiftDur (NSGA-II)',           zorder=7, line=False),
]

# ──────────────────────────────────────────────────────────────────────────
#  ユーティリティ
# ──────────────────────────────────────────────────────────────────────────
def read_fun(path):
    """FUN_*.txt を読み込み [(makespan, cost), ...] を返す。"""
    pts = []
    if not path or not os.path.exists(path):
        return pts
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2:
                try:
                    ms, c = float(p[0]), float(p[1])
                    if ms < 1e8 and c < 1e8:
                        pts.append((ms, c))
                except ValueError:
                    pass
    return pts


def pareto_filter(pts):
    """点集合から非支配解（Pareto front）のみを抽出する。"""
    if not pts:
        return []
    s, best, pf = sorted(set(pts), key=lambda p: (p[0], p[1])), math.inf, []
    for ms, c in s:
        if c < best:
            pf.append((ms, c))
            best = c
    return pf


def stats(pts):
    if not pts:
        return float('nan'), float('nan'), float('nan'), 0
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), len(pts)


# ──────────────────────────────────────────────────────────────────────────
#  ファイルパス解決
# ──────────────────────────────────────────────────────────────────────────
def get_paths(bd, prefix, cond):
    """
    各系列のファイルパスを返す。
    MaxShift / MaxShiftDur はともに現行パイプライン results/FUN_ENC/{prefix}/ の
    P1 ファイルを使用し、encoding 比較図（visualize_maxshiftdur.py）と一致させる。
      MaxShift    : FUN_ENC_{prefix}_{cond}_P1_MaxShift.txt（無ければ _MaxShift.txt）
      MaxShiftDur : FUN_ENC_{prefix}_{cond}_MaxShiftDur.txt
    ※ 旧 results/FUN/{prefix}/FUN_{prefix}_{cond}（拡張子なし）は古いランの残骸で
      正本コスト表と整合しないため使用しない。
    """
    enc_dir = os.path.join(bd, 'results', 'FUN_ENC', prefix)
    mip_dir = os.path.join(bd, 'results', 'FUN_MIP', prefix)

    ms_p1  = os.path.join(enc_dir, f'FUN_ENC_{prefix}_{cond}_P1_MaxShift.txt')
    ms_alt = os.path.join(enc_dir, f'FUN_ENC_{prefix}_{cond}_MaxShift.txt')
    ms_path = ms_p1 if os.path.exists(ms_p1) else ms_alt

    so_p1  = os.path.join(enc_dir, f'FUN_ENC_{prefix}_{cond}_P1_SchedObj.txt')
    so_alt = os.path.join(enc_dir, f'FUN_ENC_{prefix}_{cond}_SchedObj.txt')
    so_path = so_p1 if os.path.exists(so_p1) else so_alt

    return {
        'MIP':
            os.path.join(mip_dir, f'FUN_MIP_{prefix}_{cond}.txt'),
        'SchedObj':
            so_path,
        'MaxShift':
            ms_path,
        'MaxShiftDur':
            os.path.join(enc_dir, f'FUN_ENC_{prefix}_{cond}_MaxShiftDur.txt'),
    }


def discover_instances(bd):
    """
    results/FUN_MIP/**/FUN_MIP_*.txt を再帰検索し {prefix: {cond, ...}} を返す。
    MIP（厳密解）が存在するインスタンス・条件のみを対象とする。
    → MIP で解いていない問題は MIPComparison 図を出力しない。
    """
    groups = {}

    for fp in glob.glob(os.path.join(bd, 'results', 'FUN_MIP', '**', 'FUN_MIP_*.txt'),
                        recursive=True):
        bn = os.path.splitext(os.path.basename(fp))[0]
        m  = re.match(r'^FUN_MIP_(.+)_(RR\d+_RV\d)$', bn)
        if m:
            groups.setdefault(m.group(1), set()).add(m.group(2))

    return groups


# ──────────────────────────────────────────────────────────────────────────
#  1 条件グラフ
# ──────────────────────────────────────────────────────────────────────────
def plot_condition(bd, prefix, cond, out_dir):
    paths = get_paths(bd, prefix, cond)
    data  = {s['key']: pareto_filter(read_fun(paths[s['key']])) for s in SERIES}

    # MIP（厳密解）が無い条件は図を出力しない
    if not data['MIP']:
        print(f'  [SKIP] MIP 解なし → 図を出力しません: {prefix}/{cond}')
        return

    fig, ax = plt.subplots(figsize=(8, 6))
    ax.set_title(f'MIP vs NSGA-II — {prefix}  [{COND_LABELS.get(cond, cond)}]',
                 fontsize=12, fontweight='bold', pad=10)

    stats_lines = []
    for sty in SERIES:
        key = sty['key']
        pts = data[key]
        if not pts:
            continue
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        # MIP はパレートフロント折れ線を描画
        if sty.get('line') and len(pts) >= 2:
            sorted_pts = sorted(pts, key=lambda p: p[0])
            ax.plot([p[0] for p in sorted_pts],
                    [p[1] for p in sorted_pts],
                    color='#aaaaaa', linewidth=1.0, alpha=0.7,
                    zorder=sty['zorder'] - 1)
        ax.scatter(xs, ys,
                   c=sty['color'], marker=sty['marker'],
                   s=sty['ms'] ** 2 * 0.55,
                   edgecolors='black', linewidths=0.4,
                   alpha=0.85, zorder=sty['zorder'],
                   label=sty['label'])
        stats_lines.append(
            f"{sty['label']}: |PF|={len(pts)}"
            f"  ms*={int(min(xs))}  cost*={int(min(ys))}"
        )

    if not any(data[s['key']] for s in SERIES):
        ax.text(0.5, 0.5, 'No feasible data',
                ha='center', va='center', fontsize=12,
                color='#aaaaaa', transform=ax.transAxes)
    else:
        if stats_lines:
            ax.text(0.02, 0.04, '\n'.join(stats_lines),
                    transform=ax.transAxes,
                    fontsize=7.5, family='monospace',
                    verticalalignment='bottom',
                    bbox=dict(boxstyle='round,pad=0.4', facecolor='white',
                              alpha=0.85, edgecolor='#cccccc', linewidth=0.8))
        ax.legend(loc='upper right', fontsize=8.5, framealpha=0.9)

    ax.set_xlabel('Makespan',            fontsize=10)
    ax.set_ylabel('Total Resource Cost', fontsize=10)
    ax.grid(True, ls='--', alpha=0.35, color='#bbbbbb')
    ax.tick_params(labelsize=9)
    for sp in ['top', 'right']:
        ax.spines[sp].set_visible(False)

    plt.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f'mip_vs_{prefix}_{cond}.png')
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f'  [SAVED] {out_path}')

    # コンソールサマリ
    print(f'  {"Method":<35} {"PF|":>5}  {"ms*":>7}  {"cost*":>10}  {"ms_max":>7}')
    print('  ' + '-' * 70)
    for sty in SERIES:
        pts = data[sty['key']]
        ms_min, c_min, ms_max, n = stats(pts)
        if n > 0:
            print(f'  {sty["label"]:<35} {n:>5}  {int(ms_min):>7}'
                  f'  {int(c_min):>10}  {int(ms_max):>7}')
        else:
            print(f'  {sty["label"]:<35} {"---":>5}  {"---":>7}  {"---":>10}  {"---":>7}')


# ──────────────────────────────────────────────────────────────────────────
#  インスタンス全条件サマリ（テキスト表）
# ──────────────────────────────────────────────────────────────────────────
def print_instance_summary(bd, prefix, conds):
    """
    インスタンス全条件の min_makespan / min_cost を 4 系列横並びでコンソール出力。
    """
    headers = [s['label'] for s in SERIES]
    col_w   = 22

    print(f'\n{"=" * (16 + col_w * len(SERIES))}')
    print(f'  Instance: {prefix}')
    print(f'  {"Condition":<14}', end='')
    for h in headers:
        print(f'  {h[:col_w]:<{col_w}}', end='')
    print()
    print(f'  {"":14}', end='')
    for _ in headers:
        print(f'  {"ms* / cost*":<{col_w}}', end='')
    print()
    print('  ' + '-' * (14 + col_w * len(SERIES) + 2 * len(SERIES)))

    for cond in conds:
        paths = get_paths(bd, prefix, cond)
        row   = f'  {cond:<14}'
        for sty in SERIES:
            pts = pareto_filter(read_fun(paths[sty['key']]))
            if pts:
                xs, ys = [p[0] for p in pts], [p[1] for p in pts]
                cell = f'{int(min(xs))} / {int(min(ys))}'
            else:
                cell = '---'
            row += f'  {cell:<{col_w}}'
        print(row)

    print('  ' + '-' * (14 + col_w * len(SERIES) + 2 * len(SERIES)))
    print(f'  Values: min_makespan / min_cost')


# ──────────────────────────────────────────────────────────────────────────
#  メイン
# ──────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description='MaxShiftDur / MaxShift / MIP の 3 系列 Pareto 比較'
    )
    parser.add_argument('--build-dir', default='.',
                        help='cmake-build-* ディレクトリのパス (default: .)')
    parser.add_argument('--prefix',    default=None,
                        help='インスタンス prefix (例: j3011_1)。省略時は全インスタンス')
    parser.add_argument('--cond',      default=None,
                        help='条件タグ (例: RR000_RV0)。省略時は全条件')
    args = parser.parse_args()

    bd = args.build_dir
    if not os.path.isdir(bd):
        bd = os.path.join(os.path.dirname(os.path.abspath(__file__)), bd)

    print(f'[INFO] build_dir = {bd}')

    out_dir = os.path.join(bd, 'figures', 'MIPComparison')
    os.makedirs(out_dir, exist_ok=True)
    print(f'[INFO] out_dir   = {out_dir}')

    groups = discover_instances(bd)
    if not groups:
        print('[ERROR] FUN_ENC_* / FUN_MIP_* ファイルが見つかりません。')
        sys.exit(1)

    if args.prefix:
        groups = {k: v for k, v in groups.items() if k == args.prefix}
    if args.cond:
        groups = {k: {c for c in v if c == args.cond} for k, v in groups.items()}
        groups = {k: v for k, v in groups.items() if v}
    else:
        # --cond 未指定（既定の一括実行）は RR<=0.50 のみ対象。RR=0.75 は --cond で明示指定すること。
        groups = {k: {c for c in v if c in DEFAULT_CONDITIONS} for k, v in groups.items()}
        groups = {k: v for k, v in groups.items() if v}

    print(f'[INFO] インスタンス: {sorted(groups.keys())}')

    for prefix in sorted(groups.keys()):
        sorted_conds = sorted(
            groups[prefix],
            key=lambda c: CONDITIONS.index(c) if c in CONDITIONS else 99
        )
        print(f'\n{"=" * 60}')
        print(f'  Instance: {prefix}  ({len(sorted_conds)} 条件)')
        print(f'{"=" * 60}')

        for cond in sorted_conds:
            print(f'\n  [{prefix} / {cond}]')
            plot_condition(bd, prefix, cond, out_dir)

        print_instance_summary(bd, prefix, sorted_conds)

    print('\n[DONE]')


if __name__ == '__main__':
    main()
