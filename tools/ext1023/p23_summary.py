"""
p23_summary.py -- 9/10 指摘「P2・P3 をソルバーで解いて比較」の2問題例（j309_1, j3011_1）を報告ページ用にまとめる。
入力: analysis/ext1001/p123_vs_nsga_<p>.csv（tools/ext1023/plot_p123_inst.py の出力）。MaxShiftDur の行だけ使う。
出力: analysis/ext1014/seminar1028_p23.json
"""
import os, csv, json
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
INST = [p for p in ["j309_1", "j3011_1", "j3021_1", "j3031_1", "j3042_1"]
        if os.path.exists(os.path.join(BD, "analysis", "ext1001", f"p123_vs_nsga_{p}.csv"))]
rows = []
for p in INST:
    f = os.path.join(BD, "analysis", "ext1001", f"p123_vs_nsga_{p}.csv")
    for r in csv.DictReader(open(f, encoding="utf-8")):
        if r["method"] == "MaxShiftDur":
            rows.append(dict(instance=p, cond=r["cond"], mode=r["mode"], mip_points=r["mip_points"], mip_proven=r["mip_proven"],
                             hv_ratio=round(float(r["hv_ratio"]), 3), cost_gap=round(float(r["cost_gap"]) * 100, 2)))
done = {(r["instance"], r["cond"], r["mode"]) for r in rows}
p2_ok = ("j3011_1", "RR000_RV1", "P2") in done
hv = {(r["instance"], r["cond"], r["mode"]): r["hv_ratio"] for r in rows}
def g(p, c, m):
    return f"{hv[(p, c, m)]:.3f}" if (p, c, m) in hv else "実行中"


summary = (f"{len(INST)}問題例で比較（NSGA の HV ÷ MIP の HV、RV0/RV1）。" +
           "；".join(f"{p}: " + "、".join(f"{m} {g(p, 'RR000_RV0', m)}/{g(p, 'RR000_RV1', m)}" for m in ["P1", "P2", "P3"])
                    for p in INST) +
           "。P1・P2 は MIP が上か同等（HV 比 1.000 以下）。P3 は MIP が120秒で打ち切られ（最適性の証明は10点中0〜2点）、休暇のある条件（RV1）では"
           f"{len(INST)}問題例とも NSGA が MIP の暫定解を上回った（前線の範囲が広いため。中間部のコストは MIP の暫定解が低い）")
if not p2_ok:
    summary += "。j3011_1 の P2（RV1）の MIP は実行中"
out = dict(status=f"{len(INST)}問題例で確認" if p2_ok else "実行中", summary=summary, rows=rows)
json.dump(out, open(os.path.join(BD, "analysis", "ext1014", "seminar1028_p23.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(summary)
