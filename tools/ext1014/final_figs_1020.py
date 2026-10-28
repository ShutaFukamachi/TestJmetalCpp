"""
final_figs_1020.py -- 10/21 ゼミ用のまとめ図（10/14 の指摘への回答）。データがそろっていない図は飛ばす。
出力: figures/Seminar1021/ と analysis/ext1014/seminar1021_numbers.json
  F1_init_x_search.png  : 初期解×探索の 2×2（j30 工期端・HV、j120 最良既知との差・HV）
  F2_convergence.png    : 探索の進み方（j30 / j120）
  F4_size_scaling.png   : 作業数 30・60・120 での PSPLIB 最良既知との差（容量一定の条件）と HV 差
  F5_middle.png         : 中間部（2つの見方、1作業ずつの窓幅最適化の結果、ブロック窓変異の結果）
  F7_front_examples.png : 前線の例（j309_1 RR050_RV0、j120 の1例）
"""
import os, sys, csv, json, glob, statistics as S
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
E = os.path.join("results", "EXT_COST")
OUT = os.path.join("figures", "Seminar1021"); os.makedirs(OUT, exist_ok=True)
A = os.path.join("analysis", "ext1014")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
COL = {"base": "#80868b", "pr": "#e8710a", "isrgp": "#188038", "prisrgp": "#1a73e8", "bwisrgp": "#a142f4", "prbwisrgp": "#c5221f"}
LAB = {"base": "現在の手法", "pr": "初期解だけ改善\n（優先規則）", "isrgp": "探索だけ改善\n（工期島＋部分再構築）",
       "prisrgp": "両方", "bwisrgp": "探索改善＋\nブロック窓変異", "prbwisrgp": "全部入り\n（初期解＋探索＋ブロック窓）"}
NUM = {}


def load_front(p, c, suffix, size):
    f = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur{suffix}.txt")
    if not os.path.exists(f):
        return None
    pts, _ = G.load_valid_points(f, os.path.join(f"{size}.sm", p + ".sm"))
    return G.pareto_front(pts) if pts else None


def mip_front(p, c):
    f = os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt")
    if not os.path.exists(f):
        return []
    pts, _ = G.load_valid_points(f, os.path.join("j30.sm", p + ".sm"))
    return G.pareto_front(pts) if pts else []


def bks(size):
    fn = {"j30": "psplib_j30opt.txt", "j60": "psplib_j60hrs.txt", "j120": "psplib_j120hrs.txt"}[size]
    return {l.split()[0]: int(l.split()[1]) for l in open(os.path.join(HERE, fn), encoding="utf-8") if l.strip() and not l.startswith("#")}


def collect(size, insts, methods, reps, with_mip):
    """条件ごとに、全手法・全回（＋MIP）で正規化した共通 HV と、各回の最小工期を集める"""
    rows = []
    for p in insts:
        for c in CONDS:
            F = {(m, r): load_front(p, c, f"_t1016_{m}_r{r}", size) for m in methods for r in range(1, reps + 1)}
            if not all(F.values()):
                continue
            mip = mip_front(p, c) if with_mip else []
            pts = [q for f in F.values() for q in f] + mip
            b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
            row = dict(p=p, c=c, mip=mip[0][0] if mip else None)
            for (m, r), f in F.items():
                row[(m, r, "ms")] = f[0][0]
                row[(m, r, "hv")] = G.hypervolume_2d(G.normalize_front(f, *b), (1.1, 1.1))
            rows.append(row)
    return rows


def hv_diff(rows, m, reps):
    d = [S.mean(r[(m, k, "hv")] for k in range(1, reps + 1)) - S.mean(r[("base", k, "hv")] for k in range(1, reps + 1)) for r in rows]
    return S.mean(d), 2 * S.stdev(d) / len(d) ** .5, sum(1 for v in d if v > 0), sum(1 for v in d if v < 0)


# ---------------- F1: 初期解×探索 ----------------
M4 = ["base", "pr", "isrgp", "prisrgp"]
j30 = collect("j30", [f"j30{x}_1" for x in range(1, 49)], M4, 5, True)
j120 = collect("j120", [f"j120{x}_1" for x in range(1, 21)], M4, 3, False)
B120 = bks("j120")
if j30:
    fig, axs = plt.subplots(2, 2, figsize=(11, 7.2))
    wm = [r for r in j30 if r["mip"]]
    ex = {m: [sum(1 for r in wm if r[(m, k, "ms")] <= r["mip"]) for k in range(1, 6)] for m in M4}
    x = np.arange(len(M4))
    ax = axs[0][0]
    ax.bar(x, [S.mean(ex[m]) for m in M4], color=[COL[m] for m in M4])
    for i, m in enumerate(M4):
        ax.plot([i, i], [min(ex[m]), max(ex[m])], color="k", lw=1.5)
        ax.text(i, max(ex[m]) + 2, f"{S.mean(ex[m]):.1f}", ha="center", fontsize=9)
    ax.set_ylim(150, len(wm) + 10); ax.set_xticks(x); ax.set_xticklabels([LAB[m] for m in M4], fontsize=8)
    ax.set_ylabel(f"MIP の最小工期と一致した条件数（/{len(wm)}）"); ax.set_title("j30：工期最小端（5回の平均、線は5回の幅）", fontsize=10)
    ax.grid(axis="y", alpha=.3)
    ax = axs[0][1]
    hv = {m: hv_diff(j30, m, 5) for m in M4[1:]}
    ax.bar(x[1:], [hv[m][0] for m in M4[1:]], yerr=[hv[m][1] for m in M4[1:]], color=[COL[m] for m in M4[1:]], capsize=4)
    for i, m in enumerate(M4[1:], 1):
        ax.text(i, hv[m][0] + hv[m][1] + .0002, f"+{hv[m][0]:.4f}\n改善{hv[m][2]}/悪化{hv[m][3]}", ha="center", fontsize=8)
    ax.axhline(0, color="k", lw=.8); ax.set_xticks(x); ax.set_xticklabels([LAB[m] for m in M4], fontsize=8)
    ax.set_ylabel("共通 HV の差（現在の手法との差、±2SE）"); ax.set_title("j30：前線全体（276条件）", fontsize=10); ax.grid(axis="y", alpha=.3)
    ax.set_ylim(0, ax.get_ylim()[1] * 1.15)
    NUM["j30_2x2"] = {m: dict(exact_mean=S.mean(ex[m]), exact_runs=ex[m], hv=hv.get(m)) for m in M4}
    if j120:
        rr = [r for r in j120 if r["c"] == "RR000_RV0"]
        g = {m: [S.mean((r[(m, k, "ms")] - B120[r["p"]]) / B120[r["p"]] * 100 for r in rr) for k in range(1, 4)] for m in M4}
        ax = axs[1][0]
        ax.bar(x, [S.mean(g[m]) for m in M4], color=[COL[m] for m in M4])
        for i, m in enumerate(M4):
            ax.plot([i, i], [min(g[m]), max(g[m])], color="k", lw=1.5)
            ax.text(i, max(g[m]) + .2, f"{S.mean(g[m]):.1f}%", ha="center", fontsize=9)
        ax.set_xticks(x); ax.set_xticklabels([LAB[m] for m in M4], fontsize=8)
        ax.set_ylabel("PSPLIB 最良既知値との差（%）"); ax.set_title(f"j120：工期最小端（容量一定の {len(rr)} 条件、3回）", fontsize=10)
        ax.grid(axis="y", alpha=.3)
        ax = axs[1][1]
        hv2 = {m: hv_diff(j120, m, 3) for m in M4[1:]}
        ax.bar(x[1:], [hv2[m][0] for m in M4[1:]], yerr=[hv2[m][1] for m in M4[1:]], color=[COL[m] for m in M4[1:]], capsize=4)
        for i, m in enumerate(M4[1:], 1):
            ax.text(i, hv2[m][0] + hv2[m][1] + .0002, f"{hv2[m][0]:+.4f}\n改善{hv2[m][2]}/悪化{hv2[m][3]}", ha="center", fontsize=8)
        ax.axhline(0, color="k", lw=.8); ax.set_xticks(x); ax.set_xticklabels([LAB[m] for m in M4], fontsize=8)
        ax.set_ylabel("共通 HV の差（±2SE）"); ax.set_title(f"j120：前線全体（{len(j120)} 条件）", fontsize=10); ax.grid(axis="y", alpha=.3)
        ax.set_ylim(0, ax.get_ylim()[1] * 1.15)
        axs[1][0].set_ylim(0, axs[1][0].get_ylim()[1] * 1.08)
        NUM["j120_2x2"] = {m: dict(bks_gap_mean=S.mean(g[m]), bks_gap_runs=g[m], hv=hv2.get(m)) for m in M4}
    else:
        axs[1][0].axis("off"); axs[1][1].axis("off")
    fig.suptitle("初期解で良くするか、探索で良くするか（初期解×探索の組み合わせ）")
    fig.tight_layout(); fig.savefig(os.path.join(OUT, "F1_init_x_search.png"), dpi=130); plt.close(fig)
    print("F1 done")

# ---------------- F4: 作業数ごと ----------------
sizes = []
for size, n, reps, mm in [("j30", 48, 5, ["base", "bwisrgp", "prbwisrgp"]), ("j60", 48, 2, ["base", "bwisrgp", "prbwisrgp"]),
                          ("j120", 60, 1, ["base", "bwisrgp", "prbwisrgp"])]:
    bk = bks(size); res = {m: [] for m in mm}; hvd = []
    for x in range(1, n + 1):
        p = f"{size}{x}_1"
        rr = reps
        F = {(m, r): load_front(p, "RR000_RV0", f"_t1016_{m}_r{r}", size) for m in mm for r in range(1, rr + 1)}
        if not all(F.values()):
            continue
        for m in mm:
            res[m].append(S.mean((F[(m, r)][0][0] - bk[p]) / bk[p] * 100 for r in range(1, rr + 1)))
    for m in mm:
        if res[m]:
            sizes.append((size, m, S.mean(res[m]), len(res[m])))
if sizes:
    fig, ax = plt.subplots(figsize=(8, 4))
    ss = ["j30", "j60", "j120"]; w = .27
    for i, m in enumerate(["base", "bwisrgp", "prbwisrgp"]):
        v = [next((s[2] for s in sizes if s[0] == z and s[1] == m), np.nan) for z in ss]
        nn = [next((s[3] for s in sizes if s[0] == z and s[1] == m), 0) for z in ss]
        ax.bar(np.arange(3) + (i - 1) * w, v, w, color=COL[m], label=LAB[m].replace("\n", ""))
        for k, (vv, n_) in enumerate(zip(v, nn)):
            if not np.isnan(vv):
                ax.text(k + (i - 1) * w, vv + .1, f"{vv:.1f}%\n(n={n_})", ha="center", fontsize=7)
    ax.set_ylim(0, max(s_[2] for s_ in sizes) * 1.18)
    ax.set_xticks(range(3)); ax.set_xticklabels(["作業数30", "作業数60", "作業数120"])
    ax.set_ylabel("PSPLIB 最良既知値との差（%）")
    ax.set_title("容量一定の条件（＝標準の RCPSP）での工期最小端：作業数が増えても効果が続くか", fontsize=10)
    ax.legend(fontsize=8); ax.grid(axis="y", alpha=.3); fig.tight_layout()
    fig.savefig(os.path.join(OUT, "F4_size_scaling.png"), dpi=130); plt.close(fig)
    NUM["size_scaling"] = sizes
    print("F4 done", sizes)

# ---------------- F5: 中間部 ----------------
fig, axs = plt.subplots(1, 3, figsize=(14, 4))
tv = list(csv.DictReader(open(os.path.join(A, "mid_two_views.csv"), encoding="utf-8")))
pos = sorted({r["position"] for r in tv})
for ax, key, title, yl in [(axs[0], "h", "横の差：同じコストで工期が何%長いか", "工期の差（%）"),
                           (axs[1], "v", "縦の差：同じ工期でコストが何%高いか", "コストの差（%）")]:
    for i, m in enumerate(["base", "isrgp"]):
        v = [float(next(r["mean_gap_pct"] for r in tv if r["view"] == key and r["method"] == m and r["position"] == q)) for q in pos]
        ax.bar(np.arange(len(pos)) + (i - .5) * .38, v, .38, color=COL[m], label=LAB[m].replace("\n", ""))
    ax.set_xticks(range(len(pos))); ax.set_xticklabels(pos, fontsize=8); ax.set_xlabel("前線の位置（0=工期最小端、1=コスト最小端）")
    ax.set_ylabel(yl); ax.set_title(title, fontsize=10); ax.legend(fontsize=7); ax.grid(axis="y", alpha=.3)
md = list(csv.DictReader(open(os.path.join(A, "mid_diagnose.csv"), encoding="utf-8")))
ok = [r for r in md if r["nsga_list_winopt"] and float(r["nsga_cost"]) > float(r["mip_cost"]) + 1e-6]
a1 = S.mean((float(r["nsga_cost"]) - float(r["mip_cost"])) / float(r["mip_cost"]) * 100 for r in ok)
a2 = S.mean((min(float(r["nsga_list_winopt"]), float(r["nsga_cost"])) - float(r["mip_cost"])) / float(r["mip_cost"]) * 100 for r in ok)
a3 = S.mean((float(r["mip_list_winopt"]) - float(r["mip_cost"])) / float(r["mip_cost"]) * 100 for r in md if r["mip_list_winopt"])
ax = axs[2]
ax.bar([0, 1, 2], [a1, a2, a3], color=["#188038", "#81c995", "#5f6368"])
for i, v in enumerate([a1, a2, a3]):
    ax.text(i, v + .05, f"{v:+.2f}%", ha="center", fontsize=9)
ax.set_xticks([0, 1, 2]); ax.set_xticklabels(["NSGA の解\nそのまま", "NSGA の作業リストで\n窓幅を1作業ずつ最適化", "MIP の作業リストで\n窓幅を1作業ずつ最適化"], fontsize=8)
ax.set_ylabel("同じ工期での MIP とのコスト差（%）"); ax.set_title(f"中間部の解（{len({(r['instance'], r['cond']) for r in md})} 条件・{len(md)} 点）", fontsize=10)
ax.grid(axis="y", alpha=.3)
NUM["middle"] = dict(nsga=a1, nsga_list_winopt=a2, mip_list_winopt=a3, n_points=len(md))
fig.suptitle("前線の中間部：差はどこにあり、1作業ずつ窓幅を変えて縮むか")
fig.tight_layout(); fig.savefig(os.path.join(OUT, "F5_middle.png"), dpi=130); plt.close(fig)
print("F5 done")

# ---------------- F7: 前線の例 ----------------
fig, axs = plt.subplots(1, 2, figsize=(12, 4.4))
for ax, (p, c, size) in zip(axs, [("j309_1", "RR050_RV0", "j30"), ("j1201_1", "RR000_RV0", "j120")]):
    m = mip_front(p, c) if size == "j30" else []
    if m:
        ax.plot(*zip(*m), "k.-", lw=1.4, ms=5, label=f"MIP（最小工期 {m[0][0]:.0f}）")
    for meth in ["base", "isrgp", "prisrgp"]:
        f = load_front(p, c, f"_t1016_{meth}_r1", size)
        if f:
            ax.plot(*zip(*f), ".-", color=COL[meth], lw=1.4, ms=3, label=f"{LAB[meth].replace(chr(10), '')}（最小工期 {f[0][0]:.0f}）")
    if size == "j120":
        ax.axvline(B120[p], color="k", ls="--", lw=1, label=f"PSPLIB 最良既知 {B120[p]}")
    ax.set_xlabel("メイクスパン"); ax.set_ylabel("総コスト"); ax.set_title(f"{p} {c}（各手法の1回目）", fontsize=10)
    ax.legend(fontsize=7); ax.grid(alpha=.3)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "F7_front_examples.png"), dpi=130); plt.close(fig)
print("F7 done")
json.dump(NUM, open(os.path.join(A, "seminar1021_numbers.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1, default=str)

# ---------------- F6: ブロック窓変異（中間部への対策の第一歩） ----------------
bwj = {}
for k in ["j30", "j120", "all"]:
    f = os.path.join(A, f"bw_eval_{k}.json")
    if os.path.exists(f):
        bwj.update(json.load(open(f, encoding="utf-8")))
if "j30" in bwj:
    fig, axs = plt.subplots(1, 3, figsize=(14, 4))
    mr = bwj["j30"]["mid_runs"]
    ax = axs[0]
    for i, m in enumerate(["isrgp", "bwisrgp"]):
        ax.scatter([i] * len(mr[m]), mr[m], s=50, color=COL[m], zorder=3)
        ax.plot([i - .2, i + .2], [S.mean(mr[m])] * 2, color=COL[m], lw=2)
        ax.text(i + .22, S.mean(mr[m]), f"{S.mean(mr[m]):.2f}%", va="center", fontsize=9)
    ax.set_xticks([0, 1]); ax.set_xticklabels(["探索改善（工期島＋部分再構築）", "＋ブロック窓変異"], fontsize=8)
    ax.set_xlim(-.5, 1.8); ax.set_ylabel("中間部（位置0.2〜0.8）の工期の差（%）")
    ax.set_title("j30：前線中間部の MIP との差（点は各回）", fontsize=10); ax.grid(axis="y", alpha=.3)
    ax = axs[1]
    vals = [(s, bwj[s]) for s in ["j30", "j120"] if s in bwj]
    ax.bar(range(len(vals)), [v["hv_diff"] for _, v in vals], yerr=[v["hv_2se"] for _, v in vals], color=COL["bwisrgp"], capsize=4)
    for i, (s_, v) in enumerate(vals):
        ax.text(i, v["hv_diff"] + v["hv_2se"] + .0003, f"+{v['hv_diff']:.4f}\n改善{v['better']}/悪化{v['worse']}", ha="center", fontsize=8)
    ax.axhline(0, color="k", lw=.8); ax.set_xticks(range(len(vals))); ax.set_xticklabels([f"{s_}（{v['n_cond']}条件）" for s_, v in vals])
    ax.set_ylabel("共通 HV の差（＋ブロック窓 − 探索改善、±2SE）"); ax.set_title("前線全体", fontsize=10); ax.grid(axis="y", alpha=.3)
    ax = axs[2]
    p, c = "j309_1", "RR050_RV0"
    m = mip_front(p, c)
    if m:
        ax.plot(*zip(*m), "k.-", lw=1.3, ms=4, label="MIP")
    for meth in ["base", "isrgp", "bwisrgp"]:
        f = load_front(p, c, f"_t1016_{meth}_r1", "j30")
        if f:
            ax.plot(*zip(*f), ".-", color=COL[meth], lw=1.3, ms=3, label=LAB[meth].replace("\n", ""))
    ax.set_xlabel("メイクスパン"); ax.set_ylabel("総コスト"); ax.set_title(f"{p} {c}（1回目）", fontsize=10); ax.legend(fontsize=7); ax.grid(alpha=.3)
    fig.suptitle("中間部への対策：作業リストで連続する数作業の窓幅を一緒に動かす（ブロック窓変異）")
    fig.tight_layout(); fig.savefig(os.path.join(OUT, "F6_blockwin.png"), dpi=130); plt.close(fig)
    NUM["blockwin"] = bwj
    print("F6 done")
json.dump(NUM, open(os.path.join(A, "seminar1021_numbers.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1, default=str)

# ---------------- F8: ブロック窓の強さ（探索的） ----------------
rate = [(0.0, "isrgp", 9.64, 0.0026, 0.0008, 224.0), (0.1, "bwisrgp", 8.90, 0.0066, 0.0016, 222.2),
        (0.3, "bw3isrgp", 8.20, 0.0090, 0.0012, 222.4), (0.5, "bw5isrgp", 7.99, 0.0115, 0.0032, 224.2)]
# 値は eval_repeats.py j30 base,isrgp,bwisrgp,bw3isrgp,bw5isrgp の出力（各5回）
fig, axs = plt.subplots(1, 2, figsize=(10, 3.6))
xs = [r[0] for r in rate]
axs[0].plot(xs, [r[2] for r in rate], "o-", color="#a142f4", lw=2)
for r in rate:
    axs[0].text(r[0], r[2] + .04, f"{r[2]:.2f}%", ha="center", fontsize=9)
axs[0].axhline(10.23, color="#80868b", ls="--", lw=1); axs[0].text(0.25, 10.1, "現在の手法 10.23%", fontsize=8, color="#5f6368")
axs[0].set_xlabel("ブロック窓変異の確率"); axs[0].set_ylabel("中間部の工期差（%）"); axs[0].set_title("j30：前線中間部の MIP との差", fontsize=10)
axs[0].grid(alpha=.3); axs[0].set_ylim(7.5, 10.6)
axs[1].errorbar(xs, [r[3] for r in rate], yerr=[r[4] for r in rate], fmt="o-", color="#a142f4", lw=2, capsize=4)
axs[1].axhline(0, color="k", lw=.8)
axs[1].set_xlabel("ブロック窓変異の確率"); axs[1].set_ylabel("共通 HV の差（対 現在、±2SE）"); axs[1].set_title("j30：前線全体", fontsize=10)
axs[1].grid(alpha=.3)
fig.suptitle("ブロック窓変異の強さ（探索的な追加実行、各5回。確率0 = 工期島＋部分再構築のみ）")
fig.tight_layout(); fig.savefig(os.path.join(OUT, "F8_blockwin_rate.png"), dpi=130); plt.close(fig)
print("F8 done")
