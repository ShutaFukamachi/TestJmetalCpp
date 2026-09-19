#!/usr/bin/env python3
"""
visualize_psgs.py
  Serial SGS ⇄ Parallel SGS（生成スキーム A/B）のパレートフロントを重ね描画する。

対象ファイル (カレントディレクトリ = cmake-build-*/ 直下):
  FUN_PSGS_<tag>_serial.txt       (Serial, baseline)
  FUN_PSGS_<tag>_parallel.txt     (Parallel SGS)
  FUN_PSGS_<tag>_serial_a1.txt    (Serial + A1 seed)
  FUN_PSGS_<tag>_parallel_a1.txt  (Parallel + A1 seed)
  ※ PSGS ターゲット（main_ParallelSGS_AB.cpp）が出力する。

生成グラフ:
  psgs_front_<tag>.png
    4条件のフロントを1枚に重ね描画。makespan の床（左端）が
    Parallel で下がるか、cost 側（右下）が悪化しないかを目視で判断する。

使い方:
  cd cmake-build-release   (または cmake-build-debug)
  python ../visualize_psgs.py --tag j309_1_RR050
  （--tag 省略時は FUN_PSGS_*_serial.txt から自動検出）
"""

import os
import re
import glob
import argparse
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# 条件ごとのスタイル（fn サフィックス -> 描画設定）
STYLES = {
    'serial':      dict(color='#1f77b4', marker='o', ls='-',  label='Serial (baseline)'),
    'parallel':    dict(color='#d62728', marker='^', ls='-',  label='Parallel SGS'),
    'serial_a1':   dict(color='#1f77b4', marker='o', ls='--', label='Serial + A1', alpha=0.55),
    'parallel_a1': dict(color='#d62728', marker='^', ls='--', label='Parallel + A1', alpha=0.55),
}
ORDER = ['serial', 'parallel', 'serial_a1', 'parallel_a1']


def read_fun(path):
    pts = []
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2:
                ms, c = float(p[0]), float(p[1])
                if ms < 1e8 and c < 1e8:
                    pts.append((ms, c))
    pts.sort()  # makespan 昇順（フロント線描画用）
    return pts


def detect_tag(search_dir):
    tags = []
    for fp in glob.glob(os.path.join(search_dir, 'FUN_PSGS_*_serial.txt')):
        bn = os.path.basename(fp)
        m = re.match(r'FUN_PSGS_(.+)_serial\.txt$', bn)
        if m:
            tags.append(m.group(1))
    return sorted(set(tags))


def plot_tag(search_dir, tag, out_dir):
    fig, ax = plt.subplots(figsize=(9, 6.5))
    fig.suptitle(f"Generation Scheme A/B — Serial vs Parallel SGS  [{tag}]",
                 fontsize=13, fontweight='bold')

    found = False
    ms_floor = {}
    for fn in ORDER:
        path = os.path.join(search_dir, f"FUN_PSGS_{tag}_{fn}.txt")
        if not os.path.exists(path):
            continue
        pts = read_fun(path)
        if not pts:
            continue
        found = True
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        st = STYLES[fn]
        ax.plot(xs, ys, color=st['color'], marker=st['marker'],
                linestyle=st['ls'], markersize=6, linewidth=1.4,
                alpha=st.get('alpha', 0.9), label=st['label'], zorder=3)
        ms_floor[fn] = min(xs)

    if not found:
        print(f"[ERROR] FUN_PSGS_{tag}_*.txt が見つかりません（PSGS を先に実行）。")
        return

    ax.set_xlabel('Makespan  (lower = better)', fontsize=11)
    ax.set_ylabel('Total Resource Cost  (lower = better)', fontsize=11)
    ax.grid(True, linestyle='--', alpha=0.4)
    ax.legend(fontsize=10, title='generation scheme')

    # makespan の床を注記（本命Aの核心指標）
    txt = "  ms* floor:  " + "   ".join(
        f"{STYLES[fn]['label'].split(' ')[0]}={int(ms_floor[fn])}"
        for fn in ORDER if fn in ms_floor)
    ax.set_title(txt, fontsize=10)

    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"psgs_front_{tag}.png")
    fig.savefig(out, dpi=140, bbox_inches='tight')
    plt.close(fig)
    print(f"  [saved] {out}")
    # コンソールにも床の要約を出す
    print(f"  ms* floor: " + ", ".join(
        f"{fn}={int(ms_floor[fn])}" for fn in ORDER if fn in ms_floor))


def main():
    ap = argparse.ArgumentParser(description='Serial vs Parallel SGS フロント比較')
    ap.add_argument('--tag', default='', help='例: j309_1_RR050（省略時は自動検出）')
    ap.add_argument('--dir', default='.', help='FUN_PSGS_* のあるディレクトリ')
    ap.add_argument('--out', default='figures/psgs', help='出力ディレクトリ')
    args = ap.parse_args()

    tags = [args.tag] if args.tag else detect_tag(args.dir)
    if not tags:
        print("[ERROR] FUN_PSGS_*_serial.txt が見つかりません。")
        return
    for tag in tags:
        print(f"=== tag: {tag} ===")
        plot_tag(args.dir, tag, args.out)
    print("[DONE]")


if __name__ == '__main__':
    main()
