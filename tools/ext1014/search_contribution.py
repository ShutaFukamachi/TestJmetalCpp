"""
search_contribution.py -- 「初期解に頼らず、探索で工期側を良くできているか」を測る。

各条件で、初期集団（4戦略×100個体）の最小メイクスパンが MIP の最小工期に届いていなかったものだけを取り出し、
最終前線の最小メイクスパンがどれだけ縮んだか（探索の寄与）を手法ごとに比べる。
使い方: python search_contribution.py tag1 tag2 ...   （既定: t1014base t1014pr t1014rg t1014prrg t1014is t1014isrg）
"""
import os, sys, glob, csv, statistics as S
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
TAGS = sys.argv[1:] or ["t1014base", "t1014pr", "t1014rg", "t1014prrg", "t1014is", "t1014isrg"]
rows = {(r["instance"], r["cond"]): r for r in csv.DictReader(open(os.path.join(BD, "analysis", "ext1014", "variants_rows.csv")))}
NAME = {"t1014base": "b2", "t1014pr": "pr", "t1014rg": "rg", "t1014prrg": "prrg", "t1014is": "is", "t1014isrg": "isrg", "t1014isrgp": "isrgp"}
for tag in TAGS:
    n = reach = 0; closed = []
    for (p, c), r in rows.items():
        if not r["mip_min_ms"]:
            continue
        mm = float(r["mip_min_ms"])
        init = []
        for f in glob.glob(os.path.join(BD, "results", "EXT_COST", "TRACE", p, f"TRACE_{p}_{c}_S*_{tag}.csv")):
            for ln in open(f):
                q = ln.split(",")
                if q[0] == "init" and float(q[3]) < 1e8:
                    init.append(float(q[3]))
        if not init:
            continue
        i0 = min(init)
        if i0 <= mm:
            continue
        fin = float(r[f"{NAME[tag]}_min_ms"])
        n += 1; reach += fin <= mm
        closed.append((i0 - fin) / (i0 - mm))
    print(f"{tag:10s}: conditions where the initial population missed MIP's min makespan: {n:3d}; "
          f"final reached MIP in {reach:3d} ({reach/n:.0%}); share of the initial gap closed by search: mean {S.mean(closed):.0%}")
