"""
visualize_comparison.py
──────────────────────────────────────────────────────────────────────
main_NSGAII_RCPSP_Comparison.cpp が出力した 2 つの CSV を読み込み、
P1 vs P2/P3 × セットアップモデル（TW/WD/WR）× RR/RV 条件を可視化する。

【出力ファイル】  (visualize_splitting.py と同じく条件ごとに1ファイル)
  comparison_{prefix}_{cond}.png   条件ごとの比較図（パレート＋Δ棒グラフ）
  summary_delta_{prefix}.png       全条件×モードの Δms/Δcost サマリー棒グラフ
  summary_heatmap_{prefix}.png     全条件×モードのヒートマップ

【使い方】
  cd cmake-build-release
  python ../visualize_comparison.py                       # 自動検索
  python ../visualize_comparison.py PF_*.csv CMP_*.csv   # ファイル直接指定
  python ../visualize_comparison.py --out figures/comparison_NSGA2

【依存ライブラリ】
  pip install pandas matplotlib numpy
"""

import sys
import os
import re
import glob
import argparse
from io import StringIO

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import matplotlib.patches as mpatches

# ═══════════════════════════════════════════════════════════════════════
#  スタイル定義
# ═══════════════════════════════════════════════════════════════════════

STYLES: dict = {
    ("P1", "-"):  dict(color="#333333", marker="s", ls="-",  lw=2.0, ms=5, label="P1"),
    ("P2", "TW"): dict(color="#1f77b4", marker="o", ls="-",  lw=1.5, ms=4, label="P2+TW"),
    ("P2", "WD"): dict(color="#ff7f0e", marker="o", ls="-",  lw=1.5, ms=4, label="P2+WD"),
    ("P2", "WR"): dict(color="#2ca02c", marker="o", ls="-",  lw=1.5, ms=4, label="P2+WR"),
    ("P3", "TW"): dict(color="#1f77b4", marker="^", ls="--", lw=1.5, ms=4, label="P3+TW"),
    ("P3", "WD"): dict(color="#ff7f0e", marker="^", ls="--", lw=1.5, ms=4, label="P3+WD"),
    ("P3", "WR"): dict(color="#2ca02c", marker="^", ls="--", lw=1.5, ms=4, label="P3+WR"),
}

COND_ORDER = [
    "RR000_RV0", "RR000_RV1",
    "RR025_RV0", "RR025_RV1",
    "RR050_RV0", "RR050_RV1",
    "RR075_RV0", "RR075_RV1",
]

MS_ORDER = [
    ("P2", "TW"), ("P2", "WD"), ("P2", "WR"),
    ("P3", "TW"), ("P3", "WD"), ("P3", "WR"),
]


def sty(mode: str, setup: str) -> dict:
    return dict(STYLES.get((mode, setup),
                dict(color="gray", marker="x", ls=":", lw=1, ms=4,
                     label=f"{mode}+{setup}")))


def cond_label(cond: str) -> str:
    m = re.match(r"RR(\d+)_RV(\d)", cond)
    if not m:
        return cond
    rr = int(m.group(1)) / 100
    rv = "on" if m.group(2) == "1" else "off"
    return f"RR={rr:.2f}  RV={rv}"


def ms_label(mode: str, setup: str) -> str:
    return f"{mode}+{setup}" if setup != "-" else mode


# ═══════════════════════════════════════════════════════════════════════
#  ファイル読み込み
# ═══════════════════════════════════════════════════════════════════════

def _read_comment_csv(path: str) -> pd.DataFrame:
    lines = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not line.lstrip().startswith("#"):
                lines.append(line)
    df = pd.read_csv(StringIO("".join(lines)))
    df.columns = df.columns.str.strip()
    return df


def load_pf_csv(path: str) -> pd.DataFrame:
    df = _read_comment_csv(path)
    df["setup"] = df["setup"].fillna("-").astype(str).str.strip()
    df["mode"]  = df["mode"].astype(str).str.strip()
    return df


def load_cmp_csv(path: str) -> pd.DataFrame:
    in_results = False
    lines = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            stripped = line.strip()
            if stripped == "[RESULTS]":
                in_results = True
                continue
            if in_results:
                if stripped.startswith("["):
                    break
                if stripped and not stripped.startswith("#"):
                    lines.append(line)
    if not lines:
        raise ValueError(f"[RESULTS] セクションが見つかりません: {path}")
    df = pd.read_csv(StringIO("".join(lines)))
    df.columns = df.columns.str.strip()
    df["setup"] = df["setup"].fillna("-").astype(str).str.strip()
    df["mode"]  = df["mode"].astype(str).str.strip()
    return df


def find_csvs_auto() -> tuple:
    pfs  = sorted(glob.glob("PF_*.csv"))
    cmps = sorted(glob.glob("CMP_*.csv"))
    if not pfs:
        raise FileNotFoundError("PF_*.csv が見つかりません。")
    if not cmps:
        raise FileNotFoundError("CMP_*.csv が見つかりません。")
    if len(pfs)  > 1: print(f"[INFO] 複数 PF  CSV → 最新を使用: {pfs[-1]}")
    if len(cmps) > 1: print(f"[INFO] 複数 CMP CSV → 最新を使用: {cmps[-1]}")
    return pfs[-1], cmps[-1]


# ═══════════════════════════════════════════════════════════════════════
#  共通ヘルパー
# ═══════════════════════════════════════════════════════════════════════

def ordered_conds(df: pd.DataFrame) -> list:
    present = set(df["condition"].unique())
    result  = [c for c in COND_ORDER if c in present]
    for c in sorted(present - set(result)):
        result.append(c)
    return result


def draw_pareto_step(ax, pts: pd.DataFrame, mode: str, setup: str,
                     add_label: bool = True) -> None:
    if pts.empty:
        return
    s = sty(mode, setup)
    pts_s = pts.sort_values("makespan")
    xs = pts_s["makespan"].values
    ys = pts_s["cost"].values
    label = s["label"] if add_label else "_nolegend_"
    ax.step(xs, ys, where="post",
            color=s["color"], ls=s["ls"], lw=s["lw"], alpha=0.7)
    ax.scatter(xs, ys,
               color=s["color"], marker=s["marker"],
               s=s["ms"] ** 2, zorder=3, label=label)


def save(fig, path: str) -> None:
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  [Saved] {path}")


# ═══════════════════════════════════════════════════════════════════════
#  条件ごとの比較図  (1 条件 = 1 ファイル)
#
#  レイアウト:
#    上段 (大): パレートフロント重ね描き（P1 + P2/P3 × TW/WD/WR）
#    下左     : Δ Makespan_min 棒グラフ（vs P1）
#    下右     : Δ Cost_min 棒グラフ（vs P1）
# ═══════════════════════════════════════════════════════════════════════

def plot_condition(cond: str,
                   df_pf: pd.DataFrame,
                   df_stats: pd.DataFrame,
                   prefix: str,
                   suffix: str,
                   as_tag: str,
                   out_dir: str) -> None:

    grp_pf    = df_pf[df_pf["condition"] == cond]
    grp_stats = df_stats[df_stats["condition"] == cond]

    fig = plt.figure(figsize=(14, 10))
    gs  = fig.add_gridspec(2, 2, height_ratios=[1.6, 1.0], hspace=0.45, wspace=0.35)
    ax_pf   = fig.add_subplot(gs[0, :])   # 上段：パレートフロント（2列分）
    ax_ms   = fig.add_subplot(gs[1, 0])   # 下左：Δmsmin
    ax_cost = fig.add_subplot(gs[1, 1])   # 下右：Δcostmin

    fig.suptitle(
        f"Comparison  —  {prefix}   {cond_label(cond)}{suffix}",
        fontsize=13, fontweight="bold",
    )

    # ---- 上段: パレートフロント ----
    draw_pareto_step(ax_pf,
                     grp_pf[(grp_pf["mode"] == "P1") & (grp_pf["setup"] == "-")],
                     "P1", "-")
    for mode, setup in MS_ORDER:
        draw_pareto_step(ax_pf,
                         grp_pf[(grp_pf["mode"] == mode) & (grp_pf["setup"] == setup)],
                         mode, setup)

    ax_pf.set_xlabel("Makespan", fontsize=10)
    ax_pf.set_ylabel("Cost",     fontsize=10)
    ax_pf.set_title("Pareto Front", fontsize=11)
    ax_pf.grid(True, alpha=0.3, ls="--")
    ax_pf.legend(loc="upper right", fontsize=8, framealpha=0.85, ncol=4)

    # ---- 下段: Δ棒グラフ（共通処理） ----
    def _draw_delta_bar(ax, col, ylabel, title):
        vals    = []
        clrs    = []
        hatches = []
        labels  = []
        for mode, setup in MS_ORDER:
            row = grp_stats[(grp_stats["mode"] == mode) & (grp_stats["setup"] == setup)]
            v = float(row[col].iloc[0]) if len(row) else 0.0
            vals.append(v)
            clrs.append(sty(mode, setup)["color"])
            hatches.append("//" if mode == "P3" else "")
            labels.append(ms_label(mode, setup))

        x    = np.arange(len(MS_ORDER))
        bars = ax.bar(x, vals, color=clrs, hatch=hatches,
                      edgecolor="white", alpha=0.85)

        for bar, v in zip(bars, vals):
            if abs(v) > 0.01:
                sign   = "+" if v >= 0 else ""
                y_off  = abs(bar.get_height()) * 0.03 + 0.1
                va     = "bottom" if v >= 0 else "top"
                y_text = bar.get_height() + (y_off if v >= 0 else -y_off)
                ax.text(bar.get_x() + bar.get_width() / 2, y_text,
                        f"{sign}{v:.1f}",
                        ha="center", va=va, fontsize=7, color="black")

        ax.axhline(0, color="black", lw=0.8)
        ax.set_xticks(x)
        ax.set_xticklabels(labels, rotation=35, ha="right", fontsize=8)
        ax.set_ylabel(ylabel, fontsize=9)
        ax.set_title(title, fontsize=10)
        ax.grid(axis="y", alpha=0.3, ls="--")
        ax.yaxis.set_major_formatter(ticker.FormatStrFormatter("%.1f"))

    _draw_delta_bar(ax_ms,
                    col="delta_ms_min",
                    ylabel="Δ Makespan_min",
                    title="Δ Makespan_min  vs P1\n（正値 = P1 より悪い）")
    _draw_delta_bar(ax_cost,
                    col="delta_cost_min",
                    ylabel="Δ Cost_min",
                    title="Δ Cost_min  vs P1\n（正値 = P1 より悪い）")

    # P2=実線・P3=ハッチの凡例
    leg_patches = [
        mpatches.Patch(facecolor="gray", edgecolor="white", label="P2 (実線)"),
        mpatches.Patch(facecolor="gray", edgecolor="white", hatch="//", label="P3 (ハッチ)"),
    ]
    ax_ms.legend(handles=leg_patches, fontsize=7, loc="best", framealpha=0.75)

    out_path = os.path.join(out_dir, f"comparison_{prefix}_{cond}_{as_tag}.png")
    save(fig, out_path)


# ═══════════════════════════════════════════════════════════════════════
#  グローバルサマリー 1: 全条件 × モードの Δms/Δcost 棒グラフ
# ═══════════════════════════════════════════════════════════════════════

def plot_summary_delta(df_stats: pd.DataFrame,
                       prefix: str, suffix: str, as_tag: str, out_dir: str) -> None:
    df    = df_stats[df_stats["setup"] != "-"].copy()
    conds = ordered_conds(df)
    n_c   = len(conds)
    n_b   = len(MS_ORDER)
    bw    = 0.11
    x     = np.arange(n_c)

    fig, (ax1, ax2) = plt.subplots(2, 1,
                                    figsize=(max(13, n_c * 1.8), 10),
                                    sharex=True)
    fig.suptitle(f"全条件サマリー  —  {prefix}{suffix}",
                 fontsize=13, fontweight="bold")

    def _draw(ax, col, ylabel, title):
        for i, (mode, setup) in enumerate(MS_ORDER):
            vals = []
            for cond in conds:
                row = df[(df["condition"] == cond) &
                         (df["mode"] == mode) & (df["setup"] == setup)]
                vals.append(float(row[col].iloc[0]) if len(row) else 0.0)

            s      = sty(mode, setup)
            offset = (i - n_b / 2 + 0.5) * bw
            hatch  = "//" if mode == "P3" else ""
            bars   = ax.bar(x + offset, vals, width=bw,
                            color=s["color"], label=s["label"],
                            alpha=0.85, hatch=hatch, edgecolor="white")
            for bar, v in zip(bars, vals):
                if abs(v) > 0.01:
                    sign = "+" if v >= 0 else ""
                    ax.text(bar.get_x() + bar.get_width() / 2,
                            bar.get_height() + 0.1,
                            f"{sign}{v:.1f}",
                            ha="center", va="bottom",
                            fontsize=5.5, rotation=90)

        ax.axhline(0, color="black", lw=0.8)
        ax.set_ylabel(ylabel, fontsize=10)
        ax.set_title(title, fontsize=11, fontweight="bold")
        ax.legend(ncol=3, fontsize=8, loc="upper left", framealpha=0.85)
        ax.grid(axis="y", alpha=0.3, ls="--")
        ax.yaxis.set_major_formatter(ticker.FormatStrFormatter("%.1f"))

    _draw(ax1, "delta_ms_min",   "Δ Makespan_min",
          "Δ Makespan_min vs P1  （正値 = P1 より悪い）")
    _draw(ax2, "delta_cost_min", "Δ Cost_min",
          "Δ Cost_min vs P1  （正値 = P1 より悪い）")

    ax2.set_xticks(x)
    ax2.set_xticklabels([cond_label(c) for c in conds], fontsize=8)

    fig.tight_layout()
    out_path = os.path.join(out_dir, f"summary_delta_{prefix}_{as_tag}.png")
    save(fig, out_path)


# ═══════════════════════════════════════════════════════════════════════
#  グローバルサマリー 2: ヒートマップ（matplotlib imshow 版）
# ═══════════════════════════════════════════════════════════════════════

def plot_summary_heatmap(df_stats: pd.DataFrame,
                         prefix: str, suffix: str, as_tag: str, out_dir: str) -> None:
    df    = df_stats[df_stats["setup"] != "-"].copy()
    conds = ordered_conds(df)
    cols  = [ms_label(m, s) for m, s in MS_ORDER]

    def make_matrix(col: str) -> np.ndarray:
        mat = np.full((len(conds), len(MS_ORDER)), np.nan)
        for ri, cond in enumerate(conds):
            for ci, (mode, setup) in enumerate(MS_ORDER):
                row = df[(df["condition"] == cond) &
                         (df["mode"] == mode) & (df["setup"] == setup)]
                if len(row):
                    mat[ri, ci] = float(row[col].iloc[0])
        return mat

    mat_ms   = make_matrix("delta_ms_min")
    mat_cost = make_matrix("delta_cost_min")

    vmax_ms   = max(float(np.nanmax(np.abs(mat_ms))),   0.1)
    vmax_cost = max(float(np.nanmax(np.abs(mat_cost))), 0.1)

    row_labels = [cond_label(c) for c in conds]

    fig, (ax1, ax2) = plt.subplots(1, 2,
                                    figsize=(15, max(4, len(conds) * 0.7 + 2)))
    fig.suptitle(f"ヒートマップ  —  {prefix}{suffix}\n赤 = 悪化  青 = 改善",
                 fontsize=13, fontweight="bold")

    for ax, mat, vmax, cbar_label in [
        (ax1, mat_ms,   vmax_ms,   "Δ Makespan_min"),
        (ax2, mat_cost, vmax_cost, "Δ Cost_min"),
    ]:
        n_rows, n_cols = mat.shape
        im = ax.imshow(mat, cmap="RdBu_r", vmin=-vmax, vmax=vmax, aspect="auto")

        cbar = fig.colorbar(im, ax=ax, shrink=0.8)
        cbar.set_label(cbar_label, fontsize=8)
        cbar.ax.tick_params(labelsize=7)

        for r in range(n_rows):
            for c in range(n_cols):
                val = mat[r, c]
                if not np.isnan(val):
                    txt_color = "white" if abs(val) > vmax * 0.65 else "black"
                    ax.text(c, r, f"{val:.1f}",
                            ha="center", va="center",
                            fontsize=7, color=txt_color)

        ax.set_xticks(range(n_cols))
        ax.set_xticklabels(cols, rotation=35, ha="right", fontsize=8)
        ax.set_yticks(range(n_rows))
        ax.set_yticklabels(row_labels, fontsize=8)
        ax.set_xticks(np.arange(-0.5, n_cols), minor=True)
        ax.set_yticks(np.arange(-0.5, n_rows), minor=True)
        ax.grid(which="minor", color="white", linewidth=0.5)
        ax.tick_params(which="minor", bottom=False, left=False)
        ax.set_title(f"{cbar_label}  vs P1", fontsize=11)
        ax.set_xlabel("Mode + Setup", fontsize=9)
        ax.set_ylabel("Condition",    fontsize=9)

    fig.tight_layout()
    out_path = os.path.join(out_dir, f"summary_heatmap_{prefix}_{as_tag}.png")
    save(fig, out_path)


# ═══════════════════════════════════════════════════════════════════════
#  メイン
# ═══════════════════════════════════════════════════════════════════════

def main() -> None:
    parser = argparse.ArgumentParser(
        description="RCPSP Comparison 可視化（条件ごとに1ファイル出力）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("pf_csv",  nargs="?", default=None,
                        help="PF_*.csv のパス（省略時は自動検索）")
    parser.add_argument("cmp_csv", nargs="?", default=None,
                        help="CMP_*.csv のパス（省略時は自動検索）")
    parser.add_argument("--out", default="figures/comparison",
                        help="出力先ディレクトリ（デフォルト: figures/comparison）")
    args = parser.parse_args()

    # ---- ファイル特定 ----
    if args.pf_csv is None or args.cmp_csv is None:
        pf_path, cmp_path = find_csvs_auto()
    else:
        pf_path  = args.pf_csv
        cmp_path = args.cmp_csv

    print(f"[READ] PF  : {pf_path}")
    print(f"[READ] CMP : {cmp_path}")

    os.makedirs(args.out, exist_ok=True)

    # ---- データ読み込み ----
    df_pf    = load_pf_csv(pf_path)
    df_stats = load_cmp_csv(cmp_path)

    # 統計列を数値型に統一（'+3.0' のような文字列が含まれる場合の対策）
    for col in ["delta_ms_min", "delta_cost_min", "ratio_ms_min", "ratio_cost_min",
                "makespan_min", "makespan_max", "cost_min", "cost_max"]:
        if col in df_stats.columns:
            df_stats[col] = pd.to_numeric(
                df_stats[col].astype(str).str.replace("+", "", regex=False),
                errors="coerce",
            )

    # prefix（インスタンス名）の取得
    instances = df_pf["instance"].dropna().unique()
    prefix    = str(instances[0]) if len(instances) > 0 else "instance"

    # alphaS の取得
    # P1 行は alphaS=0.0 で記録されるため、P2/P3 行から実際の値を取得する。
    # P2/P3 行が存在しない場合はファイル名（例: PF_xxx_a50.csv → 0.50）から取得。
    non_p1_alphas = df_pf[df_pf["mode"] != "P1"]["alphaS"].dropna()
    if len(non_p1_alphas) > 0:
        alpha_s = float(non_p1_alphas.iloc[0])
    else:
        m_fn = re.search(r'_a(\d+)\.csv$', os.path.basename(pf_path))
        alpha_s = int(m_fn.group(1)) / 100 if m_fn else 0.0

    # タイトル用サフィックスとファイル名用タグ
    suffix = f"  (αs={alpha_s:.2f})"
    as_tag = f"as{alpha_s:.2f}"  # 例: "as0.50"

    conds = ordered_conds(df_pf)

    print(f"\n--- 読み込み完了 ---")
    print(f"  PF 行数    : {len(df_pf)}")
    print(f"  Stats 行数 : {len(df_stats)}")
    print(f"  条件数     : {len(conds)}  {conds}")
    print(f"  出力先     : {args.out}/")
    print()

    # ---- 条件ごとの比較図 ----
    print(f"--- 条件別比較図 ({len(conds)} 件) ---")
    for cond in conds:
        print(f"  {cond} ...")
        try:
            plot_condition(cond, df_pf, df_stats, prefix, suffix, as_tag, args.out)
        except Exception as e:
            print(f"  [SKIP] {cond}: {e}")

    # ---- グローバルサマリー ----
    print("\n--- グローバルサマリー ---")
    try:
        plot_summary_delta(df_stats, prefix, suffix, as_tag, args.out)
    except Exception as e:
        print(f"  [SKIP] summary_delta: {e}")

    try:
        plot_summary_heatmap(df_stats, prefix, suffix, as_tag, args.out)
    except Exception as e:
        print(f"  [SKIP] summary_heatmap: {e}")

    print(f"\n[ALL DONE]  {args.out}/ に保存されました。")


if __name__ == "__main__":
    main()
