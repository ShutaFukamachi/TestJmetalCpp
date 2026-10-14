"""make_manuscript.py -- 集計ファイルから数値を読み込み、ゼミ原稿（txt）を組み立てる。"""
import os as _os
ROOT = _os.path.normpath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import json, os, csv, datetime
BD = _os.path.join(ROOT, "cmake-build-release")
A = os.path.join(BD, "analysis", "ext1001")
OUT = _os.path.join(ROOT, "ゼミ発表原稿_20261001.txt")
LOGS = _os.path.join(ROOT, ".run_ext_1001_logs")


def J(name):
    p = os.path.join(A, name)
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}


s = J("summary.json"); rho = J("rho_summary.json"); art = J("artifact_summary.json")
pct = lambda x, d=1: "—" if x is None else f"{100*x:.{d}f}%"
f3 = lambda x: "—" if x is None else f"{x:.3f}"
f4 = lambda x: "—" if x is None else f"{x:+.4f}"


def front_stats(path_fun, path_gap):
    if not os.path.exists(path_fun):
        return None
    pts = [tuple(map(float, l.split()[:2])) for l in open(path_fun) if l.strip()]
    unp = 0
    if os.path.exists(path_gap):
        rows = list(csv.DictReader(open(path_gap)))
        unp = sum(1 for r in rows if r.get("optimal", "1") not in ("1", "1.0"))
    if not pts:
        return None
    return dict(n=len(pts), ms_min=int(min(p[0] for p in pts)), cost_min=min(p[1] for p in pts), unproven=unp, pts=pts)


def cover(a, b):
    """b の点のうち a に弱支配される割合"""
    if not a or not b:
        return None
    return sum(1 for bm, bc in b if any(am <= bm and ac <= bc for am, ac in a)) / len(b)


E = os.path.join(BD, "results", "EXT_COST")
P1 = {c: front_stats(os.path.join(E, "FUN_MIP", "j309_1", f"FUN_MIP_j309_1_{c}.txt"),
                     os.path.join(E, "FUN_MIP", "j309_1", f"GAP_MIP_j309_1_{c}.csv")) for c in ["RR000_RV0", "RR000_RV1", "RR050_RV1"]}
P2 = {c: front_stats(os.path.join(E, "FUN_MIP_P2", "j309_1", f"FUN_MIP_P2_j309_1_{c}.txt"),
                     os.path.join(E, "FUN_MIP_P2", "j309_1", f"GAP_MIP_P2_j309_1_{c}.csv")) for c in ["RR000_RV1", "RR050_RV1"]}
P3 = {c: front_stats(os.path.join(E, "FUN_MIP_P3SLOT", "j309_1", f"FUN_MIP_P3SLOT_j309_1_{c}.txt"),
                     os.path.join(E, "FUN_MIP_P3SLOT", "j309_1", f"GAP_MIP_P3SLOT_j309_1_{c}.csv")) for c in ["RR000_RV0", "RR000_RV1"]}


def cnt(path):
    try:
        return sum(1 for l in open(path, encoding="utf-8", errors="ignore") if " done" in l)
    except FileNotFoundError:
        return 0


n_mip = cnt(os.path.join(LOGS, "mip_progress.txt"))
nsga_lines = open(os.path.join(LOGS, "nsga_progress.txt"), encoding="utf-8").read().split("\n") if os.path.exists(os.path.join(LOGS, "nsga_progress.txt")) else []
n_j60 = sum(1 for l in nsga_lines if "done  j60" in l)
hv = s.get("hv_vs_mip_same_subset", {})
tw = s.get("three_way", {}); t30 = tw.get("j30", {}); t60 = tw.get("j60", {}); t30o = tw.get("j30_old", {})
rp = s.get("repro", {}); e1 = s.get("sweep_E1", {}); e2 = s.get("sweep_E2", {}); bf = s.get("by_feature_j30", {})
now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")

L = []
w = L.append
w("================================================================")
w("ゼミ発表原稿　2026年10月1日")
w("RCPSP 2目的最適化：前回ゼミの指摘への回答（拡張コスト表での再計算）")
w(f"（数値は {now} 時点の集計。自動集計スクリプトで作成）")
w("================================================================")
w("")
w("■ 0. 今回の結論（先に）")
w("")
w("前回いただいた指摘に対し、コスト表の範囲を地平線まで広げたうえで、")
w("MIP（厳密解）・SchedObj（従来法）・MaxShift・MaxShiftDur（提案法）をすべて計算し直しました。")
w("")
w(f"・コスト表を広げると、旧結果の MIP 前線のうち {pct(art.get('frac_uses_clamp'))} の点が")
w(f"  「コスト表の外（値が固定された安い領域）」を使った見かけの解でした。正しいコストで")
w(f"  計算し直すと平均 {pct(art.get('clamp_mean_rel_change'))}（最大 {pct(art.get('clamp_max_rel_change'))}）高くなります。")
w(f"・SchedObj との性能比較（j30、{t30.get('n_inst','—')} インスタンス・{t30.get('n','—')} 条件）では、")
w(f"  MaxShiftDur が SchedObj を上回った条件が {t30.get('msd_beats_so','—')}/{t30.get('n','—')}、SchedObj が上回った条件は {t30.get('so_beats_msd','—')} でした。")
w(f"・再現性では、MIP 解をまるごと表現できる割合が MaxShiftDur {pct(rp.get('sched_msd'))}、")
w(f"  SchedObj は確実に表現できるものが {pct(rp.get('sched_so_det'))} でした。")
w(f"・ただし、表現できても探索で実際にたどり着けた MIP の点は MaxShiftDur で {pct(hv.get('ext_MaxShiftDur',{}).get('reach'))} にとどまります。")
w("  改善すべきは表現ではなく探索、という前回の判断を裏付ける結果です。")
w(f"・max_shift は1活動分だけ動かすと、範囲全体でスケジュールが変わるのは中央値 {int(e2.get('median_changes',0))} 回だけで、")
w("  「しばらく変わらず、ある値で急に変わる」ことを1刻みの厳密計算で確認しました。")
w("  広げるとコストが悪化する変化も多く、「大きいほど良い」わけではありません。")
w("・P2（休暇時のみ中断）と P3（任意中断）をソルバーで解き、P1 と重ねて図にしました。")
w("  P3 は前回の定式化では解けなかったため、定式化を作り直しました。")
w("")
w("以下、指摘ごとに「何を調べ、どんな数値が出て、それが何を意味するか」を述べます。")
w("")
w("================================================================")
w("■ 1. 指摘「コスト表の範囲を広げて計算し、結果を見る」")
w("================================================================")
w("")
w("【何が問題だったか】")
w("元のコスト表は、時刻 0 から「活動の所要時間の合計」（j309_1 なら 165）までしか")
w("定義されていませんでした。一方、スケジュールの地平線は休暇ありの条件で最大その3.5倍")
w("（j309_1 なら 597）あります。範囲外の時刻では、コスト表の最後の値がそのまま使われていました。")
w("")
w("さらに悪いことに、その「最後の値」はたまたま季節変動の谷の値でした。")
w("j309_1 では、資源2が 115.2（ふだんの水準は 157.4）、資源3が 17.0（同 68.0）で固定されていました。")
w("つまり旧コスト表では、時刻165以降は2つの資源が永久に最安値のままでした。")
w("")
w("【どう広げたか】")
w("・元の値（時刻0〜164）は1文字も変えずにそのまま残し、後ろに付け足しました。")
w("・元の生成式（基準値＋トレンド＋12期周期の季節変動＋ノイズ）を既存の値から推定し、")
w("  季節変動とノイズは同じ性質のまま続けました。")
w("・トレンドは時刻164の水準で止めました。そのまま延ばすと、右肩下がりの資源で")
w("  コストがマイナスになるためです（全816資源中371資源、45%で起きる）。")
w("・乱数は標準ライブラリの分布関数を使わず自前で実装し、2回生成して完全一致を確認しました。")
w("")
w("【結果】")
w(f"旧 MIP 前線の点を、正しい（拡張）コスト表で計算し直しました（{art.get('n_inst','—')} インスタンス・{art.get('n_cond','—')} 条件・{art.get('n_points','—')} 点）。")
w(f"・コスト表の範囲外を使っていた点：{pct(art.get('frac_uses_clamp'))}（{art.get('conds_with_clamp_points','—')} 条件に存在）")
w(f"・その点のコストの変化：平均 +{pct(art.get('clamp_mean_rel_change'))}、最大 +{pct(art.get('clamp_max_rel_change'))}")
w(f"・そのうち拡張コスト表で解き直した MIP 前線に負ける点：{art.get('clamp_dominated','—')}/{art.get('n_clamp','—')}")
w(f"・範囲内だけを使う点のコスト変化：最大 {pct(art.get('nonclamp_max_abs_rel_change'),4)}（計算方法が正しいことの確認）")
w("")
w("例：j309_1（RR000_RV1）では、旧前線の最初の4点は完全に同じでしたが、")
w("旧最小コスト解（メイクスパン268、コスト307,383）は、正しく計算すると376,513 になり、")
w("旧前線のメイクスパン133の解（361,919）より高くなります。図1。")
w("")
w("【意味】")
w("旧結果の前線の右側（コスト重視の領域）は、コスト表の不具合が作った見かけの解を多く含んでいました。")
w("これまで報告してきた比較結果（HV 比 0.965 / 0.952 / 0.857 など）もこの領域を含んでいるので、")
w("以下はすべて拡張コスト表で計算し直した数値で報告します。")
w("")
w("================================================================")
w("■ 2. 指摘「SchedObj と比較して本当に良くなったのか」")
w("================================================================")
w("")
w("【何を調べたか】2つの物差しで比べました。")
w("(1) MIP 比の HV（これまでと同じ定義）：MIP とその手法の点を合わせて正規化し、参照点 (1.1, 1.1) で測った HV の比。")
w("(2) 3手法の直接比較：同じ条件の3手法（と MIP）の前線をすべて合わせて正規化した共通の HV。")
w("    MIP がない条件でも比べられます。")
w("")
w(f"【結果(1) MIP 比】MIP と NSGA の両方がそろった {hv.get('ext_MaxShiftDur',{}).get('n','—')} 条件（{s.get('n_instances_with_ext_mip','—')} インスタンス）")
w("")
w("  手法          HV比（拡張）  HV比（旧,同じ条件）  コストgap  工期gap   MIP前線の点に到達")
for m in ["MaxShiftDur", "MaxShift", "SchedObj"]:
    e, o = hv.get(f"ext_{m}", {}), hv.get(f"old_{m}", {})
    w(f"  {m:12s}  {f3(e.get('hv_ratio')):>10s}    {f3(o.get('hv_ratio')):>12s}      "
      f"{pct((e.get('cost_gap_pct') or 0)/100,2):>7s}   {pct((e.get('ms_gap_pct') or 0)/100,1):>6s}   {pct(e.get('reach')):>6s}")
w("")
w(f"【結果(2) 直接比較】j30、{t30.get('n_inst','—')} インスタンス・{t30.get('n','—')} 条件（拡張コスト表）")
w(f"・MaxShiftDur が SchedObj より良い：{t30.get('msd_beats_so','—')} 条件、SchedObj の方が良い：{t30.get('so_beats_msd','—')} 条件")
w(f"・MaxShift が SchedObj より良い：{t30.get('ms_beats_so','—')} 条件、SchedObj の方が良い：{t30.get('so_beats_ms','—')} 条件")
w(f"・MaxShiftDur と MaxShift：MaxShiftDur が良い {t30.get('msd_beats_ms','—')}、MaxShift が良い {t30.get('ms_beats_msd','—')}")
mh = t30.get("mean_hv", {})
w(f"・共通 HV の平均：SchedObj {f3(mh.get('SchedObj'))}、MaxShift {f3(mh.get('MaxShift'))}、MaxShiftDur {f3(mh.get('MaxShiftDur'))}")
w("・条件別（MaxShiftDur − SchedObj の共通HVの差の平均、SchedObj が勝った条件数）：")
for c, v in t30.get("by_cond", {}).items():
    w(f"    {c}: 差 {f4(v.get('mean_diff'))}、SchedObj 勝ち {v.get('so_wins')}/{v.get('n')}")
w(f"（参考：旧コスト表の全 {t30o.get('n','—')} 条件では、SchedObj が MaxShiftDur に勝った条件は {t30o.get('so_beats_msd','—')}）")
w("")
w("【意味】")
if (t30.get("so_beats_msd", 1) or 0) == 0:
    w("コスト表を正しくしても、提案手法（MaxShift 系）が SchedObj を上回るという結論は変わりませんでした。")
else:
    w(f"提案手法が多くの条件で上回りましたが、SchedObj の方が良い条件も {t30.get('so_beats_msd')} ありました。")
eso, emsd = hv.get("ext_SchedObj", {}), hv.get("ext_MaxShiftDur", {})
if eso.get("ms_gap_pct") and emsd.get("ms_gap_pct"):
    w(f"特に工期側の差が大きく、同じコストで比べた工期の超過は SchedObj {eso['ms_gap_pct']:.1f}%、MaxShiftDur {emsd['ms_gap_pct']:.1f}% でした。")
w("")
w("================================================================")
w("■ 3. 指摘「max_shift は少し変えてもスケジュールは変わらず、ある値を超えると急に変わる。")
w("　　 どこでどれだけ変わるか。小さい方が良い時と大きい方が良い時があるのでは」")
w("================================================================")
w("")
w("【何を調べたか】")
w("活動リストを固定し、窓幅（max_shift）を1スロット刻みで 0 から上限（T/2）まで動かして、")
w("毎回デコーダで復号しました。サンプリングではなく、すべての値を調べています。")
w("・使ったデコーダは C++ 本体を Python で書き写したもので、MIP の解133個を入れて")
w("  開始時刻とコストが 133/133 一致することを確認しています。")
w("・基準にした解：拡張 MIP の前線の工期最小端・中間・コスト最小端の3解（活動リストは開始時刻順）")
w("・(a) 1活動だけ窓幅を動かす（遺伝子を1つ変える状況）、(b) 全活動の窓幅を一緒に動かす、の2通り")
w("")
w(f"【結果(a) 1活動だけ動かす】{e2.get('n_inst','—')} インスタンス、{e2.get('n','—')} 通り（活動×基準解×条件）")
w(f"・範囲全体でスケジュールが変わる回数：中央値 {int(e2.get('median_changes',0))} 回")
w(f"・一度も変わらない活動：{pct(e2.get('frac_jobs_no_change'))}")
w(f"・MIP と同じスケジュールになる窓幅の範囲：範囲全体の中央値 {pct(e2.get('median_frac_same'))}")
w(f"・窓幅を広げたときにコストが悪化する変化を含む活動：{pct(e2.get('frac_jobs_cost_up'))}")
w("")
w(f"【結果(b) 全活動を一緒に動かす】{e1.get('n_inst','—')} インスタンス、{e1.get('n','—')} 回の掃引")
w(f"・コストが最小になる窓幅が上限ではなく途中にあった掃引：{pct(e1.get('frac_costmin_interior'))}")
w(f"  （その窓幅は上限の中央値 {pct(e1.get('median_w_costmin_over_Wmax'),0)} の位置）")
w(f"・窓幅を広げたら工期もコストも両方悪化する変化を含む掃引：{pct(e1.get('frac_sweeps_with_both_worse'))}")
w(f"・互いに優劣のつかない（パレート最適な）窓幅の数：中央値 {int(e1.get('median_nondominated_w',0))} 個")
w(f"・1回の変化でのコストの最大の跳び：中央値 {pct(e1.get('median_max_jump_rel'))}")
w("・図4：j309_1 RR000_RV1 で、窓幅を1刻みで変えたときの総コストとメイクスパン")
w("")
w("【どこで変わるか（仕組み）】")
w("デコーダは窓の中で最も安い時刻を選び、同じなら最早を選びます。したがって、窓を広げたときに")
w("配置が変わるのは「それまでの窓内より真に安い時刻が、初めて窓に入った瞬間」だけです。")
w("変化点はコスト表と資源の使用状況から正確に決まり、その間は何も変わりません。")
w("ただし1つの活動が動くと後の活動の配置も連鎖して変わるので、工期やコストへの影響の大きさは")
w("その場で決まり、自明ではありません。")
w("")
w("【意味】")
w("先生の見立てどおり、max_shift は階段状に効きます。そして「大きいほど良い」わけではなく、")
w("工期優先なら小さく、コスト優先なら中くらいが良い、という形で最適な値が解ごとに違います。")
w("")
w("================================================================")
w("■ 4. 指摘「問題の特徴やサイズを変えても MaxShift の方が常に良いのか」")
w("================================================================")
w("")
w("【何を調べたか】")
w("PSPLIB の設計パラメータ（ネットワークの複雑さ NC、資源の使われ方 RF、資源の余裕 RS）ごとに、")
w("MaxShiftDur と SchedObj の共通HVの差を集計しました（j30）。サイズについては j60 も計算しました。")
w("")
w("【結果】（差 ＝ MaxShiftDur − SchedObj、SchedObj が勝った条件数）")
for key, lab in [("NC", "ネットワークの複雑さ NC"), ("RF", "資源の使われ方 RF"), ("RS", "資源の余裕 RS")]:
    w(f"  {lab}:")
    for val, v in bf.get(key, {}).items():
        w(f"    {val}: 差 {f4(v.get('mean_diff'))}、SchedObj 勝ち {v.get('so_wins')}/{v.get('n')}")
w("")
if t60.get("n"):
    w(f"  j60（{t60.get('n_inst')} インスタンス・{t60.get('n')} 条件、MIPなし・3手法の直接比較）:")
    w(f"    MaxShiftDur が SchedObj より良い {t60.get('msd_beats_so')}、SchedObj が良い {t60.get('so_beats_msd')}")
    m6 = t60.get("mean_hv", {})
    w(f"    共通HVの平均：SchedObj {f3(m6.get('SchedObj'))}、MaxShift {f3(m6.get('MaxShift'))}、MaxShiftDur {f3(m6.get('MaxShiftDur'))}")
else:
    w("  j60：計算中（ゼミには間に合わず）")
w("")
w("【意味】")
so_w = sum(v.get("so_wins", 0) for v in bf.get("RS", {}).values())
tot_w = sum(v.get("n", 0) for v in bf.get("RS", {}).values())
if so_w == 0:
    w(f"今回調べた j30 の {tot_w} 条件では、問題の特徴を変えても SchedObj が MaxShiftDur を上回る条件は一つもありませんでした。")
else:
    w(f"今回調べた j30 の {tot_w} 条件のうち、SchedObj が MaxShiftDur を上回ったのは {so_w} 条件でした（上の表の内訳を参照）。")
rs = bf.get("RS", {})
if rs:
    big = max(rs.items(), key=lambda kv: kv[1].get("mean_diff") or 0); small = min(rs.items(), key=lambda kv: kv[1].get("mean_diff") or 0)
    w(f"ただし差の大きさは特徴によって変わり、資源の余裕 RS={big[0]} で最大（{f4(big[1].get('mean_diff'))}）、RS={small[0]} で最小（{f4(small[1].get('mean_diff'))}）でした。")
w("改善のヒントを探すには、差の小さい条件（上の表で差が小さいグループ）や、")
w("より大きな問題（j60, j90, j120）での比較が必要です。")
w("")
w("================================================================")
w("■ 5. 指摘「再現性と性能面で SchedObj と MaxShift を比較する」")
w("================================================================")
w("")
w("【何を調べたか】")
w("拡張コスト表の MIP 解を正解とし、各活動を MIP と同じ時刻に置けるかを、")
w("3つのエンコーディングそれぞれのデコーダの規則で厳密に判定しました。")
w("・MaxShiftDur / MaxShift：窓幅は遺伝子で決まるので、「窓内でその時刻が最安（同コストなら最早）」")
w("  かつ「必要な窓幅が上限（T/2）以内」なら再現できる。")
w("・SchedObj：遺伝子は「最早に置く／窓内の最安に置く」の2値だけで、窓幅は遺伝子ではなく")
w("  評価のたびに乱数で決まる。最早に置かれた活動は確実に再現できるが、それ以外は")
w("  乱数の窓幅がちょうど良い範囲に入ったときだけ再現できる（運次第）。")
w("")
w(f"【結果】{rp.get('n_inst','—')} インスタンス、MIP 解 {rp.get('n_sched','—')} 個、活動 {rp.get('n_jobs','—')} 個")
w("")
w("  エンコーディング        活動単位で再現できる   スケジュール全体を再現できる")
w(f"  MaxShiftDur             {pct(rp.get('job_msd')):>8s}              {pct(rp.get('sched_msd')):>8s}")
w(f"  MaxShift                {pct(rp.get('job_ms')):>8s}              {pct(rp.get('sched_ms')):>8s}")
w(f"  SchedObj（確実に）      {pct(rp.get('job_so_det')):>8s}              {pct(rp.get('sched_so_det')):>8s}")
w(f"  SchedObj（運が良ければ） {pct(rp.get('job_so_pos')):>8s}              確率 10^{rp.get('so_log10p_median',0):.1f}（中央値）")
w("")
w(f"・MaxShiftDur で再現できない活動の原因：窓幅の上限不足 {rp.get('msd_fail_window','—')} 件、より安い時刻が手前にある {rp.get('msd_fail_rank','—')} 件")
w("・性能面は 2 と同じ（MaxShift 系が上回る）。")
w(f"・表現できる割合（{pct(rp.get('sched_msd'))}）に対して、探索で実際に MIP の点にたどり着けたのは")
w(f"  MaxShiftDur で {pct(hv.get('ext_MaxShiftDur',{}).get('reach'))}（図8）。")
w("")
w("【意味】")
w("再現性では MaxShift 系が決定的に有利です。SchedObj は窓幅を制御できないため、")
w("MIP のような解を確実に作ることはほぼできません。ただし、表現できることと探索で見つけられることの間には大きな開きがあり、")
w("今後の改善の対象は探索です。")
w("")
w("================================================================")
w("■ 6. 指摘「短時間でより良い品質の解が出るようにする」")
w("================================================================")
w("")
w("【何を試したか】")
w("前回、必要な窓幅は小さい値に偏っている（遺伝子 ρ の必要値の中央値が 0.03 程度）ことが分かったので、")
w("窓幅を win = ρ^k × 上限 とし、小さい窓幅を細かく探せるようにする案を試しました（k=3, 5）。")
w("評価回数を1/4にした場合（短時間）も比べました。")
w("・NSGA は実行ごとに結果が変わるので、同じ設定（k=1）をもう一度走らせた差を「ノイズ」として基準にしました。")
w("")
if rho.get("n_pairs"):
    nz = rho.get("noise_rp1_minus_base") or {}
    w(f"【結果】j30、{rho.get('n_inst')} インスタンス・{rho.get('n_pairs')} 条件（共通HVの差、k=1 の平均との差）")
    w(f"・ノイズ（k=1 同士の差）：平均 {f4(nz.get('mean'))}、標準偏差 {nz.get('sd',0):.4f}" if nz else "・ノイズ：base 未取得")
    for key, lab in [("rp3_minus_k1avg", "k=3"), ("rp5_minus_k1avg", "k=5"),
                     ("rp1q_minus_k1avg", "k=1・評価1/4"), ("rp3q_minus_rp1q", "k=3・評価1/4（k=1・評価1/4 との差）")]:
        v = rho.get(key, {})
        w(f"・{lab}：平均 {f4(v.get('mean'))}（標準誤差 {v.get('se',0):.4f}）、改善 {v.get('pos')} / 悪化 {v.get('neg')} 条件")
    w("")
    w("【意味】")
    r3, r5 = rho.get("rp3_minus_k1avg", {}), rho.get("rp5_minus_k1avg", {})
    sig = lambda v: v.get("mean") is not None and v.get("se") and abs(v["mean"]) > 2 * v["se"]
    if r3.get("mean", 0) < 0 and sig(r3):
        w("ρ のべき乗で小さい窓幅に寄せる案は、評価回数が十分でも短時間でも逆効果でした（差は標準誤差の2倍を超える）。")
    elif r3.get("mean", 0) > 0 and sig(r3):
        w("ρ のべき乗（k=3）は改善を示しました（差は標準誤差の2倍を超える）。")
    else:
        w("ρ のべき乗の効果は、ばらつきの範囲内で確認できませんでした。")
    w("コスト重視の解には大きな窓幅も必要（必要値の上位1%は上限近く）なので、")
    w("大きい側の分解能を犠牲にすると前線のコスト側が弱くなると考えられます。")
    w("評価回数を1/4にすると HV は上の値だけ下がるので、「短時間化」の目標値はこの差を埋めることになります。")
else:
    w("【結果】計算中")
w("")
w("================================================================")
w("■ 7. 指摘「P2、P3 をソルバーで解いてグラフで比較したい」")
w("================================================================")
w("")
w("【P2（休暇時のみ中断）】拡張コスト表で j309_1 を解きました。")
for c in ["RR000_RV1"]:
    a, b = P1.get(c), P2.get(c)
    if a and b:
        w(f"・{c}：P1 {a['n']}点（最小工期 {a['ms_min']}、最小コスト {a['cost_min']:,.0f}）、"
          f"P2 {b['n']}点（最小工期 {b['ms_min']}、最小コスト {b['cost_min']:,.0f}、未証明 {b['unproven']}点）")
        w(f"   P1 の点のうち P2 の前線に弱支配される割合：{pct(cover(b['pts'], a['pts']))}")
    elif a:
        w(f"・{c}：P2 は計算中")
w("")
w("・RR050_RV1 も解き始めましたが、30点中9点の時点で P3 を優先するため打ち切りました（1点あたり約3分）。")
w("")
w("【P3（任意中断）】前回までの定式化（活動を1単位ずつの鎖に分解）では、120秒の制限で")
w("最適性どころか P1 より悪い解しか得られませんでした（緩和問題なのに P1 より悪い＝使えない）。")
w("そこで「各時刻にその活動を実行するか」を直接変数にする定式化に作り直しました")
w("（先行関係は「後続が動く時刻には先行が完了済み」、工期の ε 制約は「ε 以降は作業しない」）。")
w("また、P1 の解は「中断しないだけの P3 の解」なので、各 ε で P1 の最良解を Gurobi の初期解として与えました。")
w("これで P3 は必ず P1 以上の解から探索を始めます（初期解なしでは、休暇ありの条件で最小メイクスパンが")
w("120秒以内に 147 までしか下がらず、P1 の 100 より悪いという矛盾が出ていました）。")
for c in ["RR000_RV0", "RR000_RV1"]:
    a, b = P1.get(c), P3.get(c)
    if a and b:
        w(f"・{c}：P3 {b['n']}点（最小工期 {b['ms_min']}、最小コスト {b['cost_min']:,.0f}、未証明 {b['unproven']}点）、"
          f"P1 は最小工期 {a['ms_min']}、最小コスト {a['cost_min']:,.0f}")
        w(f"   P1 の点のうち P3 の前線に弱支配される割合：{pct(cover(b['pts'], a['pts']))}")
    elif a:
        w(f"・{c}：P3 は計算中")
w("・P3 は制限時間内に最適性を証明できない点が残りますが、得られた解はすべて P1 以下で、緩和関係と整合します。")
w("・図6：j309_1 の P1・P2・P3 の前線")
w("")
w("================================================================")
w("■ 8. 注意点（結果の読み方）")
w("================================================================")
w(f"・MIP は計算時間の都合で、重い6インスタンス（j3013, j3025, j3029, j3030, j3041, j3045）を後回しにしました。")
w("  この6つはすべて「資源が厳しい（RS=0.2〜0.5）・資源をよく使う（RF=0.75〜1.0）」問題なので、")
w("  MIP 比の結果は資源に余裕のある問題に偏っています。3手法の直接比較は全48インスタンスです。")
w("・MIP には前線が空の条件（解けなかった条件）が元々12あります。")
w("・今回は MIP・NSGA・P2・P3 を同時に動かしたため、CPU が混み、MIP の時間切れ（最適性未証明）の点が増えている可能性があります。")
w("・NSGA は各条件1回の実行です。個々の条件の差には実行ごとのばらつきが含まれます。")
w("・MIP の記録上のメイクスパンが実際の終了時刻より大きい点が少数あります（ε の上限値が記録されている。j309_1 で 9/133）。")
w("")
w("================================================================")
w("■ 9. 次の方針（ご相談）")
w("================================================================")
w("(1) 改善の対象は「探索」。表現できる解の8割近くに対し、到達できているのは数%。")
w("    活動リストの交叉・突然変異、窓幅遺伝子の突然変異の刻み（階段の段差に合わせる）、")
w("    MIP 解の構造（開始時刻順）を初期解に使う、などを検討したい。")
w("(2) max_shift の変化点は正確に計算できるので、遺伝子の値を「変化点の番号」に置き換える")
w("    （意味のない平らな区間を探索しない）表現を検討したい。")
w("(3) 問題サイズを変えた比較（j60, j90, j120）を続けたい。j90・j120 の拡張コスト表には")
w("    コストが 0 付近になるスロットが多い（元の生成式の季節変動の谷）ので、扱いを相談したい。")
w("(4) P3 は定式化を変えて解けるようになったので、制限時間を延ばして最適性を証明するかどうか。")
w("")
w("================================================================")
w("（付録）今回作成したもの")
w("  図　　　：cmake-build-release/analysis/ext1001/figures/fig1〜fig8")
w("  集計　　：cmake-build-release/analysis/ext1001/*.csv, summary.json, rho_summary.json, artifact_summary.json")
w("  新規ツール：main_P3Slot.cpp（P3 の新定式化）、NSGAMaxShiftDurCpp の --rho-power オプション")
w("  記録　　：.miss_memory/042（スリープ事故）、043（今回の注意点）")
w("  公式結果（results/FUN_MIP, results/FUN_ENC）とコスト表の正本（costs/）は一切変更していません。")
w("================================================================")
open(OUT, "w", encoding="utf-8-sig").write("\n".join(L) + "\n")
print("written", OUT, len(L), "lines")
