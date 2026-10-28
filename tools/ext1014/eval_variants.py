"""
eval_variants.py -- 10/14 ゼミ指摘（初期解と探索）の A/B 評価

比べるもの（すべて拡張コスト表 costs_ext、MaxShiftDur、P1）:
  base   : 公式設定（10/1 の再計算）            *_MaxShiftDur.txt
  rp1    : 公式設定の再実行（10/1）              *_MaxShiftDur_rp1.txt
  b2     : 公式設定の再実行（今回のコード）      *_MaxShiftDur_t1014base.txt
  pr     : 初期解の半分を優先規則＋ランダム性    *_MaxShiftDur_t1014pr.txt
  rg     : 部分再構築変異（確率0.1）            *_MaxShiftDur_t1014rg.txt
  prrg   : pr と rg の両方                       *_MaxShiftDur_t1014prrg.txt
ノイズの基準: base・rp1・b2 は同じ設定なので、その差がノイズ。
  各手法の値から mean(base, rp1) を引き、b2 の同じ差（= ノイズ）と比べる。

指標:
  ms_gap_end : 前線の最小メイクスパンと MIP の最小メイクスパンの差（%）
  ms_gap_mid : MIP 前線の各点と同じかより安いコストで NSGA が出せる最小メイクスパンの差（平均 %）
  hv         : 全手法と MIP を合わせて正規化した共通 HV（参照点 1.1, 1.1）
出力: analysis/ext1014/variants_rows.csv と標準出力の要約
"""
import os, sys, csv, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BD = os.path.join(ROOT, "cmake-build-release")
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(BD)
import gap48_analysis as G  # noqa: E402

E = os.path.join("results", "EXT_COST")
CONDS = ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]
TAGS = {"base": "", "rp1": "_rp1", "b2": "_t1014base", "pr": "_t1014pr", "rg": "_t1014rg", "prrg": "_t1014prrg"}
if len(sys.argv) > 1:   # 追加の手法: name=_suffix
    for a in sys.argv[1:]:
        k, v = a.split("=")
        TAGS[k] = v
OUT = os.path.join(BD, "analysis", "ext1014")
os.makedirs(OUT, exist_ok=True)


def front(path, sm):
    if not os.path.exists(path):
        return None
    pts, _ = G.load_valid_points(path, sm)
    return G.pareto_front(pts) if pts else None


def mid_gap(f, mip):
    g = []
    for ms, c in mip:
        m = G.min_ms_at_or_below_cost(f, c)
        if m is not None:
            g.append((m - ms) / ms)
    return S.mean(g) if g else None


rows = []
for x in range(1, 49):
    p = f"j30{x}_1"; sm = os.path.join("j30.sm", p + ".sm")
    for c in CONDS:
        F = {t: front(os.path.join(E, "FUN_ENC", p, f"SCHED_ENC_{p}_{c}_MaxShiftDur{s}.txt"), sm) for t, s in TAGS.items()}
        if not all(F.values()):
            continue
        mip = front(os.path.join(E, "FUN_MIP", p, f"SCHED_MIP_{p}_{c}.txt"), sm) or []
        pts = [q for f in F.values() for q in f] + mip
        b = (min(q[0] for q in pts), max(q[0] for q in pts), min(q[1] for q in pts), max(q[1] for q in pts))
        mm = min(q[0] for q in mip) if mip else None
        row = dict(instance=p, cond=c, mip_min_ms=mm)
        for t, f in F.items():
            row[f"{t}_min_ms"] = min(q[0] for q in f)
            row[f"{t}_gap_end"] = (row[f"{t}_min_ms"] - mm) / mm if mm else None
            row[f"{t}_gap_mid"] = mid_gap(f, mip) if mip else None
            row[f"{t}_hv"] = G.hypervolume_2d(G.normalize_front(f, *b), (1.1, 1.1))
        rows.append(row)

with open(os.path.join(OUT, "variants_rows.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.DictWriter(fp, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)


def stat(v):
    m = S.mean(v); sd = S.stdev(v) if len(v) > 1 else 0
    return (f"{m:+.4f} (SE {sd/len(v)**0.5:.4f})  better {sum(1 for x in v if x > 1e-9):3d}"
            f"  worse {sum(1 for x in v if x < -1e-9):3d}")


print(f"conditions: {len(rows)} ({len({r['instance'] for r in rows})} instances)")
wm = [r for r in rows if r["mip_min_ms"]]
print(f"--- with MIP: {len(wm)} conditions ---")
for t in TAGS:
    g = [r[f"{t}_gap_end"] for r in wm]
    gm = [r[f"{t}_gap_mid"] for r in wm if r[f"{t}_gap_mid"] is not None]
    print(f"[{t:5s}] end gap: mean {S.mean(g):.2%}  exact {sum(1 for x in g if x <= 1e-9):3d}"
          f"  >5% {sum(1 for x in g if x > 0.05):2d}  >10% {sum(1 for x in g if x > 0.10):2d}"
          f" | mid gap: mean {S.mean(gm):.2%}")
print("--- HV difference from mean(base, rp1) (all conditions) ---")
for t in TAGS:
    if t in ("base", "rp1"):
        continue
    d = [r[f"{t}_hv"] - (r["base_hv"] + r["rp1_hv"]) / 2 for r in rows]
    print(f"[{t:5s}] {stat(d)}" + ("   <- noise reference" if t == "b2" else ""))
print("--- mid gap difference (percentage points) from mean(base, rp1) ---")
for t in TAGS:
    if t in ("base", "rp1"):
        continue
    d = [-(r[f"{t}_gap_mid"] - (r["base_gap_mid"] + r["rp1_gap_mid"]) / 2) * 100 for r in wm
         if None not in (r[f"{t}_gap_mid"], r["base_gap_mid"], r["rp1_gap_mid"])]
    print(f"[{t:5s}] improvement {stat(d)}" + ("   <- noise reference" if t == "b2" else ""))
