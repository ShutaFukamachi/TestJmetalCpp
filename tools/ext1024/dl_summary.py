"""
dl_summary.py -- 10/24 夜の結果（前線中間部の診断・目標工期の遺伝子）を報告ページ用の JSON にまとめる
出力: analysis/ext1014/seminar1028_dl.json（make_report_1028.py が読む）
"""
import os, json, csv
import pandas as pd
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
A14 = os.path.join(BD, "analysis", "ext1014"); A24 = os.path.join(BD, "analysis", "ext1024")


def lj(p):
    p = os.path.join(A14, p)
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else None


out = {}
# 診断: 鎖の分解（j30 r1 の中間部、prtbw3 と dl の共通の組）
a = pd.read_csv(os.path.join(A24, "mid_chain_prtbw3.csv")); b = pd.read_csv(os.path.join(A24, "mid_chain_dl.csv"))
m = a[a.who == "MIP"].set_index("key"); a = a[a.who == "NSGA"].set_index("key"); b = b[b.who == "NSGA"].set_index("key")
k = a.index.intersection(b.index).intersection(m.index)
out["chain"] = {w: {c: round(float(df.loc[k, c].mean()), 1) for c in ["ms", "dur", "delay", "rwait", "delay_off"]}
                for w, df in [("MIP", m), ("prtbw3", a), ("dl", b)]}
out["chain"]["n"] = int(len(k))
s = pd.read_csv(os.path.join(A24, "mid_delay_sols_prtbw3.csv"))
out["delay_counts"] = {w: {c: round(float(s[s.who == w][c].mean()), 1) for c in ["n_delayed", "sum_delay", "sum_save"]} for w in ["MIP", "NSGA"]}
out["pairs"] = int(len(s) // 2)
rv = {}
for name in ["prtbw3", "dl", "dlcal"]:
    f = os.path.join(A24, f"mid_delay_sols_{name}.csv")
    if os.path.exists(f):
        d = pd.read_csv(f); n = d[d.who == "NSGA"].copy(); n["rv"] = n.key.str.split("|").str[1].str[-3:]
        rv[name] = {r: round(float(v), 2) for r, v in n.groupby("rv").gap_ms.mean().items()}
out["gap_by_rv_r1"] = rv
# j30・j120 の判定
e = lj("e2_eval_base_prtbw3_dl.json")
out["j30"] = {mm: dict(mid=e[mm]["mid"], exact=e[mm]["exact"], costend=e[mm]["costend"]) for mm in ["prtbw3", "dl"]}
out["j30_hv"] = e.get("hv_dl_prtbw3")
out["j120"] = json.load(open(os.path.join(A24, "dl_j120.json"), encoding="utf-8"))
x = lj("e2_eval_isrgp_dl_dlonly_dlm5.json")
if x:
    out["explore"] = {mm: dict(mid=sum(x[mm]["mid"]) / len(x[mm]["mid"]), exact=sum(x[mm]["exact"]) / len(x[mm]["exact"]),
                               hv=x["hv"].get(f"{mm}-isrgp")) for mm in ["dlonly", "dlm5"]}
c = lj("e2_eval_isrgp_dlcal.json")
if c:
    out["dlcal"] = dict(mid=c["dlcal"]["mid"], exact=c["dlcal"]["exact"], hv=c["hv"]["dlcal-isrgp"])
r = lj("e2_eval_rep_dl_dlcal.json")  # 再検証（新しい r6〜r10）
if r:
    out["rep"] = dict(dl_mid=r["isrgp"]["mid"], dlcal_mid=r["dlcal"]["mid"], dl_exact=r["isrgp"]["exact"], dlcal_exact=r["dlcal"]["exact"],
                      hv=r["hv"]["dlcal-isrgp"])
c2 = lj("e2_eval_isrgp_dlc.json")  # コスト側の再挑戦
if c2:
    out["dlc"] = dict(mid=c2["dlc"]["mid"], exact=c2["dlc"]["exact"], costend=c2["dlc"]["costend"], dl_costend=c2["isrgp"]["costend"], hv=c2["hv"]["dlc-isrgp"])
o1 = lj("e2_eval_isrgp_dl1p.json")  # 工期島なしの1集団＋目標工期
if o1:
    out["dl1p"] = dict(mid=o1["dl1p"]["mid"], exact=o1["dl1p"]["exact"], hv=o1["hv"]["dl1p-isrgp"], j120_hv=0.0001, j120_bks=6.13, j120_bks_dl=5.10)
# 作業数（あれば）
sz = {}
for size in ["j60", "j90", "j120"]:
    j = lj(f"size_{size}_prtbw3_dl.json")
    if j:
        sz[size] = dict(n_cond=j["n_cond"], bks_prtbw3=j["prtbw3"]["bks_gap"], bks_dl=j["dl"]["bks_gap"], hv=j["dl"]["hv_diff"],
                        hv_2se=j["dl"]["hv_2se"], better=j["dl"]["better"], worse=j["dl"]["worse"])
out["size"] = sz
t = os.path.join(A14, "timing_1025.csv")
if os.path.exists(t):
    out["timing"] = list(csv.DictReader(open(t, encoding="utf-8")))
json.dump(out, open(os.path.join(A14, "seminar1028_dl.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(json.dumps(out, ensure_ascii=False)[:1500])
