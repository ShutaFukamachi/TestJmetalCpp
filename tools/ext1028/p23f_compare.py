"""
p23f_compare.py -- 課題3: P1/P2/P3 で「現在の手法（p23）」と「最終設定（p23f）」の NSGA を MIP と比べる。
5問題例 × RR000_RV0/RV1 × P1/P2/P3。HV は MIP・p23・p23f を合わせた範囲で正規化（3者で同じ物差し）。
P3 の MIP は 120秒打ち切り（既存）と 600秒（--tag=tl600、あるものだけ）を並べる。
出力: analysis/ext1028/p23f_compare.csv, analysis/ext1014/seminar1029_p23f.json, figures/Splitting_EXT/p123_final_<p>.png
"""
import csv, os, sys, json
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gap48_analysis as G  # noqa: E402

plt.rcParams["font.family"] = ["Yu Gothic", "Meiryo", "MS Gothic", "sans-serif"]
plt.rcParams["axes.unicode_minus"] = False
E = os.path.join(BD, "results", "EXT_COST")
INST = ["j309_1", "j3011_1", "j3021_1", "j3031_1", "j3042_1"]
CONDS = ["RR000_RV0", "RR000_RV1"]
MODES = ["P1", "P2", "P3"]


def nsga(p, cond, mode, tag):
    t = f"{cond}_MaxShiftDur_{tag}" if mode == "P1" else f"{cond}_{mode}_MaxShiftDur_{tag}"
    fn = os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{t}.txt")
    if not os.path.exists(fn):
        return []
    pts, _ = G.load_valid_points(fn, os.path.join(BD, "j30.sm", p + ".sm"))
    return G.pareto_front(pts) if pts else []


def mip(p, cond, mode, suffix=""):
    if mode == "P2" and cond == "RR000_RV0":
        mode = "P1"   # 容量一定・休暇なしでは P2 と P1 は同じ問題
    d = {"P1": ("FUN_MIP", "MIP"), "P2": ("FUN_MIP_P2", "MIP_P2"), "P3": ("FUN_MIP_P3SLOT", "MIP_P3SLOT")}[mode]
    f = os.path.join(E, d[0], p, f"FUN_{d[1]}_{p}_{cond}{suffix}.txt")
    g = os.path.join(E, d[0], p, f"GAP_{d[1]}_{p}_{cond}{suffix}.csv")
    if not os.path.exists(f):
        return [], None
    pts = sorted(tuple(map(float, l.split()[:2])) for l in open(f) if l.strip())
    proven = None
    if os.path.exists(g):
        rows = list(csv.DictReader(open(g)))
        proven = (sum(1 for r in rows if r["optimal"] in ("1", "1.0")), len(rows))
    return G.pareto_front(pts), proven


def hv(front, b):
    return G.hypervolume_2d(G.normalize_front(front, *b), (1.1, 1.1)) if front else 0.0


rows = []
for p in INST:
    fig, axes = plt.subplots(2, 3, figsize=(15, 8.4))
    for r, cond in enumerate(CONDS):
        for c, mode in enumerate(MODES):
            m, proven = mip(p, cond, mode)
            m6, proven6 = mip(p, cond, mode, "_tl600") if mode == "P3" else ([], None)
            a, f = nsga(p, cond, mode, "p23"), nsga(p, cond, mode, "p23f")
            allp = m + a + f + m6
            if not (m and f):
                continue
            b = (min(x[0] for x in allp), max(x[0] for x in allp), min(x[1] for x in allp), max(x[1] for x in allp))
            hm = hv(m, b)
            row = dict(instance=p, cond=cond, mode=mode, mip_proven=f"{proven[0]}/{proven[1]}" if proven else "",
                       hv_cur=round(hv(a, b) / hm, 3) if a else "", hv_final=round(hv(f, b) / hm, 3),
                       hv_mip600=round(hv(m6, b) / hm, 3) if m6 else "",
                       mip600_proven=f"{proven6[0]}/{proven6[1]}" if proven6 else "")
            rows.append(row)
            ax = axes[r][c]
            ax.plot(*zip(*m), "-o", color="#202124", ms=3.5, lw=1.4, label="MIP（120秒）" + (f"・証明 {row['mip_proven']}" if proven else ""))
            if m6:
                ax.plot(*zip(*m6), "--D", color="#5f6368", ms=3, lw=1.0, label=f"MIP（600秒）・証明 {row['mip600_proven']}")
            if a:
                ax.plot(*zip(*a), "-^", color="#9aa0a6", ms=2.5, lw=1.0, label=f"NSGA 現在の手法（HV比 {row['hv_cur']}）")
            ax.plot(*zip(*f), "-s", color="#e8710a", ms=2.5, lw=1.1, label=f"NSGA 最終設定（HV比 {row['hv_final']}）")
            ax.set_title(f"{cond}  {mode}", fontsize=9); ax.grid(alpha=0.25); ax.legend(fontsize=7, loc="upper right")
            ax.set_xlabel("メイクスパン")
            if c == 0:
                ax.set_ylabel("総コスト")
    fig.suptitle(f"{p}：MIP と NSGA-II（現在の手法 / 最終設定）の比較　P1・P2・P3（拡張コスト表）\n"
                 "HV比 = NSGA の HV ÷ MIP（120秒）の HV。3者を合わせた範囲で正規化", fontsize=10)
    fig.tight_layout()
    os.makedirs(os.path.join(BD, "figures", "Splitting_EXT"), exist_ok=True)
    fig.savefig(os.path.join(BD, "figures", "Splitting_EXT", f"p123_final_{p}.png"), dpi=140); plt.close(fig)

os.makedirs(os.path.join(BD, "analysis", "ext1028"), exist_ok=True)
keys = ["instance", "cond", "mode", "mip_proven", "hv_cur", "hv_final", "hv_mip600", "mip600_proven"]
with open(os.path.join(BD, "analysis", "ext1028", "p23f_compare.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.DictWriter(fp, fieldnames=keys); w.writeheader(); w.writerows(rows)
for r in rows:
    print(r)
for mode in MODES:
    for cond in CONDS:
        xs = [r for r in rows if r["mode"] == mode and r["cond"] == cond and r["hv_cur"] != ""]
        if xs:
            up = sum(1 for r in xs if r["hv_final"] > r["hv_cur"])
            print(f"{mode} {cond}: 現在 {sum(r['hv_cur'] for r in xs)/len(xs):.3f} → 最終 {sum(r['hv_final'] for r in xs)/len(xs):.3f}（改善 {up}/{len(xs)}）")
json.dump(dict(rows=rows), open(os.path.join(BD, "analysis", "ext1014", "seminar1029_p23f.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
