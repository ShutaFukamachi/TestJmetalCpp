"""make_report.py -- ゼミ用の集計値（summary.json）と図（PNG）を作る。"""
import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import csv, json, os, math, statistics as S
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
plt.rcParams["axes.unicode_minus"] = False

BD = _os.path.join(ROOT, "cmake-build-release")
A = os.path.join(BD, "analysis", "ext1001")
FIG = os.path.join(A, "figures"); os.makedirs(FIG, exist_ok=True)
M3 = ["SchedObj", "MaxShift", "MaxShiftDur"]
COL = {"SchedObj": "#9aa0a6", "MaxShift": "#4c8bf5", "MaxShiftDur": "#e8710a", "MIP": "#202124"}
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
out = {}


def rd(name):
    p = os.path.join(A, name)
    return list(csv.DictReader(open(p, encoding="utf-8"))) if os.path.exists(p) else []


def fnum(x):
    return None if x in ("", None) else float(x)


feat = {r["instance"]: r for r in rd("features.csv")}
rows = rd("rows.csv")

# ---------- 1. MIP 比 HV（公式定義）: ext と、同じ (instance,cond) 集合での old ----------
ext_keys = {(r["instance"], r["cond"]) for r in rows if r["root"] == "ext" and r["size"] == "j30" and r["hv_ratio"] != ""}
res = {}
for root in ["old", "ext"]:
    for m in M3:
        v = [fnum(r["hv_ratio"]) for r in rows if r["root"] == root and r["method"] == m and r["hv_ratio"] != ""
             and (r["instance"], r["cond"]) in ext_keys]
        cg = [fnum(r["cost_gap"]) for r in rows if r["root"] == root and r["method"] == m and r["cost_gap"] != ""
              and (r["instance"], r["cond"]) in ext_keys]
        mg = [fnum(r["ms_gap"]) for r in rows if r["root"] == root and r["method"] == m and r["ms_gap"] != ""
              and (r["instance"], r["cond"]) in ext_keys]
        rc = [fnum(r["reach"]) for r in rows if r["root"] == root and r["method"] == m and r.get("reach", "") != ""
              and (r["instance"], r["cond"]) in ext_keys]
        res[f"{root}_{m}"] = dict(n=len(v), hv_ratio=S.mean(v) if v else None, reach=S.mean(rc) if rc else None,
                                  cost_gap_pct=100 * S.mean(cg) if cg else None, ms_gap_pct=100 * S.mean(mg) if mg else None)
out["hv_vs_mip_same_subset"] = res
out["n_instances_with_ext_mip"] = len({k[0] for k in ext_keys})
fig, ax = plt.subplots(figsize=(6.4, 3.6))
x = range(len(M3)); wdt = 0.36
for i, root in enumerate(["old", "ext"]):
    vals = [res[f"{root}_{m}"]["hv_ratio"] or 0 for m in M3]
    b = ax.bar([k + (i - 0.5) * wdt for k in x], vals, wdt, label={"old": "旧コスト表", "ext": "拡張コスト表"}[root],
               color=["#c6c6c6", "#5f6368"][i])
    for k, v in enumerate(vals):
        ax.text(k + (i - 0.5) * wdt, v + 0.005, f"{v:.3f}", ha="center", fontsize=8)
ax.set_xticks(list(x)); ax.set_xticklabels(M3); ax.set_ylim(0.7, 1.02); ax.set_ylabel("HV比（対MIP）")
ax.set_title(f"MIP比HV（同じ {len(ext_keys)} 条件で比較）"); ax.legend(fontsize=8, loc="lower right")
fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig2_hv_ratio_old_vs_ext.png"), dpi=150); plt.close(fig)

# ---------- 2. 3手法の直接比較（共通正規化 HV）: j30 全 / j60 全 ----------
def three_way(size, root="ext"):
    by = defaultdict(dict)
    for r in rows:
        if r["size"] == size and r["root"] == root:
            by[(r["instance"], r["cond"])][r["method"]] = fnum(r["hv_common"])
    by = {k: v for k, v in by.items() if len(v) == 3}
    win = defaultdict(int); diff_msd_so = []; diff_ms_so = []; diff_msd_ms = []
    for k, v in by.items():
        best = max(v.values())
        for m in M3:
            if v[m] >= best - 1e-12:
                win[m] += 1
        diff_msd_so.append(v["MaxShiftDur"] - v["SchedObj"]); diff_ms_so.append(v["MaxShift"] - v["SchedObj"])
        diff_msd_ms.append(v["MaxShiftDur"] - v["MaxShift"])
    return by, dict(n=len(by), n_inst=len({k[0] for k in by}), wins=dict(win),
                    msd_beats_so=sum(1 for d in diff_msd_so if d > 1e-9), so_beats_msd=sum(1 for d in diff_msd_so if d < -1e-9),
                    ms_beats_so=sum(1 for d in diff_ms_so if d > 1e-9), so_beats_ms=sum(1 for d in diff_ms_so if d < -1e-9),
                    msd_beats_ms=sum(1 for d in diff_msd_ms if d > 1e-9), ms_beats_msd=sum(1 for d in diff_msd_ms if d < -1e-9),
                    mean_hv={m: S.mean([v[m] for v in by.values()]) for m in M3} if by else {},
                    median_diff_msd_so=S.median(diff_msd_so) if by else None), diff_msd_so


tw = {}
diffs = {}
for size in ["j30", "j60"]:
    by, st, d = three_way(size)
    tw[size] = st; diffs[size] = (by, d)
    # 条件別
    st["by_cond"] = {}
    for c in CONDS:
        dd = [v["MaxShiftDur"] - v["SchedObj"] for k, v in by.items() if k[1] == c]
        st["by_cond"][c] = dict(n=len(dd), so_wins=sum(1 for x in dd if x < -1e-9), mean_diff=S.mean(dd) if dd else None)
tw["j30_old"] = three_way("j30", "old")[1]
out["three_way"] = tw

fig, axes = plt.subplots(1, 2, figsize=(9, 3.4), sharey=True)
for ax, size in zip(axes, ["j30", "j60"]):
    by, d = diffs[size]
    if d:
        ax.hist(d, bins=25, color=COL["MaxShiftDur"], alpha=0.85)
        ax.axvline(0, color="k", lw=1)
        n_neg = sum(1 for x in d if x < -1e-9)
        ax.set_title(f"{size}: MaxShiftDur − SchedObj（共通HV）\nSchedObj が勝った条件 {n_neg}/{len(d)}", fontsize=9)
    ax.set_xlabel("HV の差（正ならMaxShiftDurが良い）")
axes[0].set_ylabel("条件数")
fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig3_three_way_diff.png"), dpi=150); plt.close(fig)

# ---------- 3. 問題の特徴（RS, RF, NC）と SchedObj の勝ち ----------
by30 = diffs["j30"][0]; feat_tab = {}


def design(inst):
    """PSPLIB j30 のパラメータ番号 x（j30x_1）から設計値 (NC, RF, RS) を返す（features.py の実測値で対応を確認済み）"""
    x = int(inst[3:inst.index("_")])
    return {"NC": [1.5, 1.8, 2.1][(x - 1) // 16], "RF": [0.25, 0.5, 0.75, 1.0][((x - 1) // 4) % 4],
            "RS": [0.2, 0.5, 0.7, 1.0][(x - 1) % 4]}


for key in ["NC", "RF", "RS"]:
    g = defaultdict(list)
    for (inst, cond), v in by30.items():
        g[design(inst)[key]].append(v["MaxShiftDur"] - v["SchedObj"])
    feat_tab[key] = {str(k): dict(n=len(v), mean_diff=S.mean(v), so_wins=sum(1 for x in v if x < -1e-9)) for k, v in sorted(g.items())}
out["by_feature_j30"] = feat_tab
fig, axes = plt.subplots(1, 3, figsize=(10, 3.2), sharey=True)
for ax, key in zip(axes, ["NC", "RF", "RS"]):
    xs, ys = [], []
    for (inst, cond), v in by30.items():
        xs.append(float(feat[inst][key])); ys.append(v["MaxShiftDur"] - v["SchedObj"])
    ax.scatter(xs, ys, s=8, alpha=0.5, color=COL["MaxShiftDur"]); ax.axhline(0, color="k", lw=0.8)
    ax.set_xlabel(key)
axes[0].set_ylabel("HV差 MaxShiftDur−SchedObj")
fig.suptitle("j30: 問題の特徴量と提案手法の優位の大きさ", fontsize=10)
fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig7_features.png"), dpi=150); plt.close(fig)

# ---------- 4. 再現性 ----------
sch = rd("repro_sched.csv"); jobs = rd("repro_jobs.csv")
if sch:
    tb = lambda v: v in ("True", "1", 1, True)
    r = dict(n_sched=len(sch), n_jobs=len(jobs), n_inst=len({x["instance"] for x in sch}),
             job_msd=sum(tb(x["msd"]) for x in jobs) / len(jobs), job_ms=sum(tb(x["ms"]) for x in jobs) / len(jobs),
             job_so_det=sum(tb(x["so_det"]) for x in jobs) / len(jobs),
             job_so_pos=sum(1 for x in jobs if float(x["so_p"]) > 0) / len(jobs),
             job_rank1=sum(tb(x["rank1"]) for x in jobs) / len(jobs),
             sched_msd=sum(int(x["msd_all"]) for x in sch) / len(sch), sched_ms=sum(int(x["ms_all"]) for x in sch) / len(sch),
             sched_so_det=sum(int(x["so_det_all"]) for x in sch) / len(sch))
    lp = sorted(float(x["so_log10p"]) for x in sch)
    fin = [v for v in lp if v != -math.inf]
    r["so_log10p_median"] = lp[len(lp) // 2]; r["so_log10p_max"] = max(fin) if fin else None
    # 再現できない原因の内訳（MaxShiftDur, 活動単位）
    bad = [x for x in jobs if not tb(x["msd"])]
    r["msd_fail_rank"] = sum(1 for x in bad if not tb(x["rank1"])); r["msd_fail_window"] = sum(1 for x in bad if tb(x["rank1"]))
    out["repro"] = r
    fig, ax = plt.subplots(figsize=(6.2, 3.4))
    labs = ["MaxShiftDur", "MaxShift", "SchedObj\n(確実)"]
    a = [r["job_msd"], r["job_ms"], r["job_so_det"]]; b = [r["sched_msd"], r["sched_ms"], r["sched_so_det"]]
    ax.bar([i - 0.18 for i in range(3)], a, 0.36, label="活動単位", color="#c6c6c6")
    ax.bar([i + 0.18 for i in range(3)], b, 0.36, label="スケジュール全体", color="#5f6368")
    for i in range(3):
        ax.text(i - 0.18, a[i] + 0.01, f"{100*a[i]:.1f}%", ha="center", fontsize=8)
        ax.text(i + 0.18, b[i] + 0.01, f"{100*b[i]:.1f}%", ha="center", fontsize=8)
    ax.set_xticks(range(3)); ax.set_xticklabels(labs); ax.set_ylim(0, 1.1); ax.set_ylabel("MIP解を再現できる割合")
    ax.set_title(f"再現性（拡張コスト表, MIP解 {r['n_sched']} 個）", fontsize=10); ax.legend(fontsize=8)
    fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig5_reproducibility.png"), dpi=150); plt.close(fig)

if sch and res.get("ext_MaxShiftDur", {}).get("reach") is not None:
    fig, ax = plt.subplots(figsize=(6.4, 3.4))
    labs = ["SchedObj", "MaxShift", "MaxShiftDur"]
    rep = [r["sched_so_det"], r["sched_ms"], r["sched_msd"]]
    rea = [res[f"ext_{m}"]["reach"] for m in labs]
    ax.bar([i - 0.18 for i in range(3)], rep, 0.36, color="#c6c6c6", label="MIP解を表現できる（再現性）")
    ax.bar([i + 0.18 for i in range(3)], rea, 0.36, color="#e8710a", label="探索で到達できた（MIP前線の点の割合）")
    for i in range(3):
        ax.text(i - 0.18, rep[i] + 0.01, f"{100*rep[i]:.0f}%", ha="center", fontsize=8)
        ax.text(i + 0.18, rea[i] + 0.01, f"{100*rea[i]:.0f}%", ha="center", fontsize=8)
    ax.set_xticks(range(3)); ax.set_xticklabels(labs); ax.set_ylim(0, 1.1)
    ax.set_title("表現できること と 探索で到達できること は別", fontsize=10); ax.legend(fontsize=7, loc="upper left")
    fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig8_representable_vs_reached.png"), dpi=150); plt.close(fig)

# ---------- 5. 指摘3 掃引 ----------
e1 = rd("sweep_E1_ENC.csv") or rd("sweep_E1_MIP.csv"); e2 = rd("sweep_E2_ENC.csv") or rd("sweep_E2_MIP.csv")
if e1:
    g = lambda k: [float(x[k]) for x in e1 if x[k] != ""]
    out["sweep_E1"] = dict(n=len(e1), base=e1[0]["base"], n_inst=len({x["instance"] for x in e1}),
                           median_distinct_over_nw=S.median([float(x["n_distinct"]) / float(x["n_w"]) for x in e1]),
                           median_plateau=S.median(g("median_plateau")), median_max_plateau=S.median(g("max_plateau")),
                           frac_sweeps_with_cost_up=sum(1 for x in e1 if int(x["cost_up_changes"]) > 0) / len(e1),
                           frac_sweeps_with_both_worse=sum(1 for x in e1 if int(x["both_worse_changes"]) > 0) / len(e1),
                           frac_costmin_interior=sum(1 for x in e1 if int(x["w_at_cost_min"]) < int(x["Wmax"])) / len(e1),
                           median_w_costmin_over_Wmax=S.median([int(x["w_at_cost_min"]) / int(x["Wmax"]) for x in e1]),
                           median_nondominated_w=S.median(g("n_nondominated_w")),
                           median_max_jump_rel=S.median(g("max_jump_rel")))
if e2:
    out["sweep_E2"] = dict(n=len(e2), n_inst=len({x["instance"] for x in e2}),
                           median_frac_same=S.median([float(x["frac_same_as_base"]) for x in e2]),
                           median_changes=S.median([int(x["n_change"]) for x in e2]),
                           frac_jobs_cost_up=sum(1 for x in e2 if int(x["cost_up"]) > 0) / len(e2),
                           frac_jobs_no_change=sum(1 for x in e2 if int(x["n_change"]) == 0) / len(e2))
cv = os.path.join(A, "sweep_curve_j309_1_RR000_RV1_pos0.csv")
if os.path.exists(cv):
    C = [(int(r["w"]), fnum(r["makespan"]), fnum(r["cost"])) for r in csv.DictReader(open(cv))]
    C = [c for c in C if c[1] is not None]
    fig, ax1 = plt.subplots(figsize=(7.2, 3.6))
    ax1.step([c[0] for c in C], [c[2] for c in C], where="post", color=COL["MaxShiftDur"], label="総コスト")
    ax1.set_xlabel("全活動に与えた窓幅 w（スロット）"); ax1.set_ylabel("総コスト", color=COL["MaxShiftDur"])
    ax2 = ax1.twinx(); ax2.step([c[0] for c in C], [c[1] for c in C], where="post", color="#1a73e8", label="メイクスパン")
    ax2.set_ylabel("メイクスパン", color="#1a73e8")
    ax1.set_title("j309_1 RR000_RV1: 活動リスト固定で窓幅だけを1刻みで変えたときの目的値", fontsize=9)
    fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig4_maxshift_step.png"), dpi=150); plt.close(fig)

# ---------- 6. 旧→拡張 の MIP 前線（j309_1 RR000_RV1） ----------
def front(path):
    return [tuple(map(float, l.split()[:2])) for l in open(path) if l.strip()] if os.path.exists(path) else []
old = front(os.path.join(BD, "results/FUN_MIP/j309_1/FUN_MIP_j309_1_RR000_RV1.txt"))
new = front(os.path.join(BD, "results/EXT_COST/FUN_MIP/j309_1/FUN_MIP_j309_1_RR000_RV1.txt"))
recost = [(169, 342789.7), (186, 349086.4), (200, 356850.0), (214, 362103.6), (226, 366889.7), (254, 366759.1), (268, 376513.0)]
fig, ax = plt.subplots(figsize=(6.6, 3.8))
ax.plot(*zip(*old), "o--", color="#9aa0a6", label="旧コスト表でのMIP前線")
ax.plot(*zip(*new), "o-", color=COL["MIP"], ms=4, label="拡張コスト表でのMIP前線")
ax.plot(*zip(*recost), "x", color="#d93025", ms=8, label="旧前線の右側を正しいコストで再計算")
ax.set_xlabel("メイクスパン"); ax.set_ylabel("総コスト"); ax.legend(fontsize=8)
ax.set_title("j309_1 RR000_RV1: 旧コスト表の右半分は見かけの解だった", fontsize=10)
fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig1_old_vs_ext_front.png"), dpi=150); plt.close(fig)

# ---------- 7. P1 / P2 / P3（j309_1） ----------
p = dict(
    P1_RV1=front(os.path.join(BD, "results/EXT_COST/FUN_MIP/j309_1/FUN_MIP_j309_1_RR000_RV1.txt")),
    P2_RV1=front(os.path.join(BD, "results/EXT_COST/FUN_MIP_P2/j309_1/FUN_MIP_P2_j309_1_RR000_RV1.txt")),
    P1_RV0=front(os.path.join(BD, "results/EXT_COST/FUN_MIP/j309_1/FUN_MIP_j309_1_RR000_RV0.txt")),
    P3_RV0=front(os.path.join(BD, "results/EXT_COST/FUN_MIP_P3SLOT/j309_1/FUN_MIP_P3SLOT_j309_1_RR000_RV0.txt")),
    P3_RV1=front(os.path.join(BD, "results/EXT_COST/FUN_MIP_P3SLOT/j309_1/FUN_MIP_P3SLOT_j309_1_RR000_RV1.txt")),
    P1_RR50RV1=front(os.path.join(BD, "results/EXT_COST/FUN_MIP/j309_1/FUN_MIP_j309_1_RR050_RV1.txt")),
    P2_RR50RV1=front(os.path.join(BD, "results/EXT_COST/FUN_MIP_P2/j309_1/FUN_MIP_P2_j309_1_RR050_RV1.txt")))
out["p123_points"] = {k: len(v) for k, v in p.items()}
fig, axes = plt.subplots(1, 2, figsize=(10, 3.8))
panels = [(["P1_RV0", "P3_RV0"], "RR000_RV0（休暇なし）: P1 と P3"),
          (["P1_RV1", "P2_RV1", "P3_RV1"], "RR000_RV1（休暇あり）: P1・P2・P3")]
sty = {"P1": ("o-", COL["MIP"], "P1（分割なし）"), "P2": ("s-", "#d93025", "P2（休暇時のみ中断）"),
       "P3": ("^-", "#188038", "P3（任意中断・時間切れ解）")}
for ax, (keys, t) in zip(axes, panels):
    for k in keys:
        if p[k]:
            mk, c, lab = sty[k.split("_")[0]]
            ax.plot(*zip(*p[k]), mk, ms=3.5, color=c, label=lab)
    ax.set_title("j309_1 " + t, fontsize=9); ax.set_xlabel("メイクスパン"); ax.legend(fontsize=7)
axes[0].set_ylabel("総コスト")
fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig6_p1_p2_p3.png"), dpi=150); plt.close(fig)

json.dump(out, open(os.path.join(A, "summary.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(json.dumps(out, ensure_ascii=False, indent=1)[:6000])
