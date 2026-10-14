#!/usr/bin/env python3
"""
run_all.py -- 2026-10-01 ゼミ資料（拡張コスト表での比較）を一括で作り直す。

  cd TestJmetalCpp
  python tools/ext1001/run_all.py            # 全部
  python tools/ext1001/run_all.py --no-sweep # 窓幅掃引（数分かかる）を省略

出力:
  cmake-build-release/analysis/ext1001/        集計 CSV / JSON / Excel（EXT1001_比較データ.xlsx）
  cmake-build-release/figures/EXT1001_Seminar/ ゼミ用まとめ図 fig1〜fig8
  cmake-build-release/figures/MIPComparison_EXT/ 条件ごとの前線比較（j30, MIP と3手法）
  cmake-build-release/figures/Size_EXT/        条件ごとの前線比較（j60, 3手法）
  cmake-build-release/figures/MaxShiftStep_EXT/ 窓幅を1刻みで動かしたときの目的値（指摘3）
  ゼミ発表原稿_20261001.txt
"""
import os, sys, shutil, subprocess, glob
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
A = os.path.join(BD, "analysis", "ext1001")
sys.path.insert(0, HERE)
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]


def run(script, *args, cwd=HERE):
    print(f"==> {script} {' '.join(args)}", flush=True)
    r = subprocess.run([sys.executable, script, *args], cwd=cwd)
    if r.returncode != 0:
        print(f"    [WARN] {script} exited {r.returncode}")


def mip_done_instances():
    base = os.path.join(BD, "results", "EXT_COST", "FUN_MIP")
    return sorted(p for p in os.listdir(base) if os.path.exists(os.path.join(base, p, f"GAP_MIP_{p}_RR050_RV1.csv")))


def sweeps_and_step_figures():
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
    import sweep
    from msd_decoder import load, start_order
    for f in glob.glob(os.path.join(A, "sweep_E*_MIP.csv")):
        os.remove(f)
    inst = mip_done_instances()
    print(f"==> sweep on {len(inst)} instances", flush=True)
    sweep.main(inst, root_kind="MIP", detail=("j309_1", "RR000_RV1", 0))
    sweep.main_e2(inst, root_kind="MIP")
    # 図: j309_1 の6条件、工期最小端の解で全活動一緒に掃引 / 1活動ずつ掃引
    out = os.path.join(BD, "figures", "MaxShiftStep_EXT"); os.makedirs(out, exist_ok=True)
    for cond in CONDS:
        try:
            cs = load("j309_1", cond)
        except Exception:
            continue
        s = sorted(cs.sols, key=lambda x: x[0])[0][2]
        seq = start_order(s); Wmax = int(round(0.5 * cs.T))
        curve = sweep.e1_uniform(cs, seq, Wmax)
        C = [c for c in curve if c[1] is not None]
        fig, ax1 = plt.subplots(figsize=(7.2, 3.6))
        ax1.step([c[0] for c in C], [c[2] for c in C], where="post", color="#e8710a")
        ax1.set_xlabel("全活動に与えた窓幅 w（スロット）"); ax1.set_ylabel("総コスト", color="#e8710a")
        ax2 = ax1.twinx(); ax2.step([c[0] for c in C], [c[1] for c in C], where="post", color="#1a73e8")
        ax2.set_ylabel("メイクスパン", color="#1a73e8")
        ax1.set_title(f"j309_1 {cond}: 全活動の窓幅を一緒に1刻みで変えたとき", fontsize=9)
        fig.tight_layout(); fig.savefig(os.path.join(out, f"uniform_j309_1_{cond}.png"), dpi=130); plt.close(fig)
        # 1活動ずつ: 変化回数の多い4活動
        base = cs.decode(seq, lambda j, t, s=s: s[j] - t, record_tmak=True)
        tm = base[3]; wneed = {j: s[j] - tm[j] for j in range(cs.n)}
        jobs = [j for j in range(cs.n) if cs.dur[j] > 0]
        curves = {}
        for jj in jobs:
            cc = []
            for w in range(Wmax + 1):
                r = cs.decode(seq, lambda j, t, jj=jj, w=w: (w if j == jj else wneed[j]))
                cc.append((w, r[1], r[2]) if r else (w, None, None))
            curves[jj] = cc
        nchg = {jj: sum(1 for i in range(1, len(c)) if c[i][2] != c[i - 1][2]) for jj, c in curves.items()}
        pick = sorted(nchg, key=nchg.get, reverse=True)[:4]
        fig, axes = plt.subplots(2, 2, figsize=(9, 5.4), sharex=True)
        for ax, jj in zip(axes.flat, pick):
            cc = [c for c in curves[jj] if c[1] is not None]
            ax.step([c[0] for c in cc], [c[2] for c in cc], where="post", color="#e8710a")
            ax.axvline(wneed[jj], color="#188038", ls="--", lw=1)
            a2 = ax.twinx(); a2.step([c[0] for c in cc], [c[1] for c in cc], where="post", color="#1a73e8", lw=0.9)
            ax.set_title(f"活動{jj}（所要{cs.dur[jj]}）: 変化 {nchg[jj]} 回、緑線=MIP解を再現する窓幅", fontsize=8)
            ax.tick_params(labelsize=7); a2.tick_params(labelsize=7)
        fig.suptitle(f"j309_1 {cond}: 1活動だけ窓幅を動かしたときの総コスト（橙）とメイクスパン（青）", fontsize=9)
        fig.tight_layout(); fig.savefig(os.path.join(out, f"single_j309_1_{cond}.png"), dpi=130); plt.close(fig)
    print("    step figures ->", out)


def main():
    do_sweep = "--no-sweep" not in sys.argv
    os.makedirs(A, exist_ok=True)
    run("features.py")
    run("analyze_ext.py")
    run("repro.py")
    run("artifact.py")
    if do_sweep:
        sweeps_and_step_figures()
    run("rho_analyze.py")
    run("make_report.py")
    run("plot_p123_vs_nsga.py")
    dst = os.path.join(BD, "figures", "EXT1001_Seminar"); os.makedirs(dst, exist_ok=True)
    for f in glob.glob(os.path.join(A, "figures", "*.png")):
        shutil.copy(f, dst)
    run(os.path.join(ROOT, "visualize_ext_comparison.py"), cwd=BD)
    run("make_excel_ext1001.py")
    run("make_manuscript.py")
    print("[run_all] done")


if __name__ == "__main__":
    main()
