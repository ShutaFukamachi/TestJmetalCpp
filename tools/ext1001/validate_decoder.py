"""デコーダ再現の検証: MIP 解を「開始時刻順の活動リスト + 窓幅 w=s*-t_mak」で復号し、
開始時刻と総コストが MIP の記録値と一致するかを確かめる。"""
import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import sys
from msd_decoder import load, start_order

prefix = sys.argv[1] if len(sys.argv) > 1 else "j309_1"
tot = ok_s = ok_c = 0
for cond in ["RR000_RV0", "RR000_RV1", "RR025_RV0", "RR025_RV1", "RR050_RV0", "RR050_RV1"]:
    try:
        cs = load(prefix, cond)
    except FileNotFoundError:
        continue
    for ms, cost, s in cs.sols:
        seq = start_order(s)
        res = cs.decode(seq, lambda j, tmak, s=s: s[j] - tmak)
        tot += 1
        if res is None:
            continue
        st, dms, dc = res
        ok_s += int(list(st) == s)
        ok_c += int(abs(dc - cost) <= 1.0 and dms == ms)
    print(cond, "cum solutions", tot, "starts_match", ok_s, "ms&cost_match", ok_c)
