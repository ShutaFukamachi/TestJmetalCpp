"""
make_report_1028.py -- 10/21 ゼミ指摘への対応の報告ページ（1枚の HTML、図は埋め込み）
数値は analysis/ext1014/seminar1028_numbers.json と各集計 JSON から読む。
出力: cmake-build-release/analysis/ext1014/report_1028.html
"""
import os, json, base64, csv, html
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
A = os.path.join(BD, "analysis", "ext1014")
N = json.load(open(os.path.join(A, "seminar1028_numbers.json"), encoding="utf-8"))
OPT = {}
for k, fn in [("enc", "seminar1028_enc.json"), ("tbc", "seminar1028_tbc.json"), ("sh2", "seminar1028_short2.json"), ("sh3", "seminar1028_short3.json"), ("dl", "seminar1028_dl.json"), ("mr", "seminar1028_mipratio.json"), ("nf", "seminar1028_newfinal.json"),
              ("p23", "seminar1028_p23.json"), ("sz3", "size_summary_base_prbwisrgp_prtbw3.json"),
              ("sz4", "size_summary_base_prbwisrgp_prtbw3_dl.json"),
              ("rem", "seminar1029_rem.json"), ("p23f", "seminar1029_p23f.json")]:
    p = os.path.join(A, fn)
    if os.path.exists(p):
        OPT[k] = json.load(open(p, encoding="utf-8"))


def img(rel, alt):
    p = os.path.join(BD, rel)
    if not os.path.exists(p):
        return f'<p class="missing">（図 {html.escape(rel)} はまだありません）</p>'
    b = base64.b64encode(open(p, "rb").read()).decode()
    return f'<figure><img src="data:image/png;base64,{b}" alt="{html.escape(alt)}"><figcaption>{html.escape(alt)}</figcaption></figure>'


def table(head, rows):
    h = "".join(f"<th>{html.escape(x)}</th>" for x in head)
    b = "".join("<tr>" + "".join(f"<td>{html.escape(str(c))}</td>" for c in r) + "</tr>" for r in rows)
    return f'<div class="tw"><table><thead><tr>{h}</tr></thead><tbody>{b}</tbody></table></div>'


D = N["diag"]; AB = N["ablation"]; HH = N["headtohead"]; SZ = N["size"]; SH = N["short"]
mean = lambda v: sum(v) / len(v)  # noqa: E731
J120 = OPT["sz3"]["j120"]
if "sz4" in OPT:
    J120 = OPT["sz4"]["j120"]
_t = list(csv.DictReader(open(os.path.join(A, "timing_1024.csv"), encoding="utf-8")))
TT = {r["method"]: float(r["seconds"]) for r in _t if r["instance"] == "j1201_1"}
TT2 = {r["method"]: float(r["seconds"]) for r in OPT.get("dl", {}).get("timing", []) if r["instance"] == "j1201_1"}
kpis = [
    ("元の NSGA-II で見逃されていた良い作業リスト", f"{D['ratio']}倍", f"子100万体あたり {D['child_list_new_per_m']} 対 実際の更新 {D['child_better_per_m']}"),
    ("工期島の効き目の本体", "窓幅0の評価", f"1集団で子の窓幅を0にするだけで工期端 {AB['zw']['exact']}（工期島 {AB['isrgp']['exact']}）"),
    ("1集団か2集団か（事前登録の判定）", "2集団", f"1集団は前線中間部 {mean(HH['j30']['fin1']['mid']):.2f}% 対 {mean(HH['j30']['isrgp']['mid']):.2f}%、j120 HV {HH['hv_j120_fin1']['hv_diff']:+.4f}"),
    ("作業数120 の最良既知値との差（全60×2回）", f"{J120['gap']['base']:.1f}% → {J120['gap'].get('dl', J120['gap']['prtbw3']):.1f}%",
     f"最新の最終設定。前線全体は {J120['better'].get('dl', J120['better']['prtbw3'])}/{J120['n_cond']} 条件で改善"),
    *([("厳密解（MIP）に対する HV 比（j30、240条件）", f"{OPT['mr']['base']['mean']:.3f} → {OPT['mr']['dl']['mean']:.3f}",
        f"現在の手法 → 最新の最終設定（中央値 {OPT['mr']['dl']['median']:.3f}）。1.0 で MIP と同じ前線")] if "mr" in OPT else []),
    *([("前線中間部の工期差（j30、5回平均）", f"{sum(OPT['dl']['j30']['prtbw3']['mid']) / 5:.2f}% → {sum(OPT['dl']['j30']['dl']['mid']) / 5:.2f}%",
        "目標工期の遺伝子（10/24 夜、事前登録で採用）")] if "dl" in OPT else []),
    (("計算時間（j1201_1、1インスタンス）", f"{TT2['base']:.0f}秒 → {TT2['dl']:.0f}秒", f"現在の手法 → ＋目標工期（+{(TT2['dl'] / TT2['base'] - 1) * 100:.0f}%、10/24 夜に交互に測定）")
     if TT2 else ("計算時間（j1201_1、1インスタンス）", f"{TT['base']:.0f}秒 → {TT['prtbw3']:.0f}秒", f"現在の手法 → 新しい最終設定（+{(TT['prtbw3'] / TT['base'] - 1) * 100:.0f}%）。j30 ではほぼ同じ")),
]
ab_rows = [(lab, AB[m]["exact"], f"{AB[m]['reach']}%", "—" if not AB[m]["hv"] else f"{AB[m]['hv']['mean']:+.4f} ± {AB[m]['hv']['se2']:.4f}", v)
           for m, lab, v in [("base", "現在の手法", ""), ("rg", "部分再構築のみ", ""), ("ms", "(A) 親の20%を工期上位10%から", "不十分"),
                             ("zw", "(B) 子の17%の窓幅を0に", "不十分（HV）"), ("mszw", "(A)+(B)", "不十分（HV）"),
                             ("mate", "工期側の両親→窓幅0の子", "不十分（HV）"), ("matep", "　同左＋停滞で停止", "不十分（工期端）"),
                             ("matef", "　同左＋停止＋改善のみ受入", "不十分（工期端）"), ("matef0", "工期側の両親→窓幅0、改善のみ受入", "1集団で十分"),
                             ("isrgp", "工期島（2集団）", "比較の基準")]]
size_rows = [(f"作業数{k[1:]}", f"{v['n_gap']}×{v['runs']}回", f"{v['gap_base']:.2f}%", f"{v['gap_new']:.2f}%", f"+{v['hv']:.4f} ± {v['hv_2se']:.4f}", f"{v['better']}/{v['n_cond']}")
             for k, v in SZ.items()]
size_head = ["作業数", "インスタンス×回", "現在の手法", "最終設定", "HV 差（±2SE）", "改善した条件"]
size_fig = "figures/Seminar1028/D5_size.png"
if "sz3" in OPT:  # 新しい最終設定（時間ブロック窓）を加えた3手法の比較
    size_fig = "figures/Seminar1028/D5_size_3.png"
    size_head = ["作業数", "インスタンス×回", "現在の手法", "10/21 の最終設定", "新しい最終設定", "HV 差：10/21（±2SE）", "HV 差：新しい（±2SE）"]
    size_rows = [(f"作業数{k[1:]}", f"{v['n_gap']}×{v['runs']}回", f"{v['gap']['base']:.2f}%", f"{v['gap']['prbwisrgp']:.2f}%", f"{v['gap']['prtbw3']:.2f}%",
                  f"+{v['hv']['prbwisrgp']:.4f} ± {v['hv_2se']['prbwisrgp']:.4f}（{v['better']['prbwisrgp']}/{v['n_cond']}）",
                  f"+{v['hv']['prtbw3']:.4f} ± {v['hv_2se']['prtbw3']:.4f}（{v['better']['prtbw3']}/{v['n_cond']}）") for k, v in OPT["sz3"].items()]
if "sz4" in OPT:
    size_fig = "figures/Seminar1028/D5_size_4.png"
    size_head = ["作業数", "インスタンス×回", "現在の手法", "10/21", "時間ブロック", "＋目標工期", "HV 差：時間ブロック", "HV 差：＋目標工期"]
    size_rows = [(f"作業数{k[1:]}", f"{v['n_gap']}×{v['runs']}回", f"{v['gap']['base']:.2f}%", f"{v['gap']['prbwisrgp']:.2f}%", f"{v['gap']['prtbw3']:.2f}%",
                  f"{v['gap']['dl']:.2f}%", f"+{v['hv']['prtbw3']:.4f}（{v['better']['prtbw3']}/{v['n_cond']}）",
                  f"+{v['hv']['dl']:.4f}（{v['better']['dl']}/{v['n_cond']}）") for k, v in OPT["sz4"].items()]
nf_rows = []
dl_sec = ""
p23_rows = []
if "p23" in OPT:  # NSGA の HV ÷ MIP の HV（問題例 × 分割モード）
    _h = {(r["instance"], r["cond"], r["mode"]): r["hv_ratio"] for r in OPT["p23"]["rows"]}
    for _p in dict.fromkeys(r["instance"] for r in OPT["p23"]["rows"]):
        p23_rows.append((_p, *[f"{_h.get((_p, 'RR000_RV0', m), float('nan')):.3f} / {_h.get((_p, 'RR000_RV1', m), float('nan')):.3f}" for m in ["P1", "P2", "P3"]]))
if "dl" in OPT:  # 10/24 夜: 前線中間部の診断と目標工期の遺伝子
    DL = OPT["dl"]; CH = DL["chain"]; mn = lambda v: sum(v) / len(v)  # noqa: E731
    ch_rows = [(w, f"{CH[k]['ms']:.0f}", f"{CH[k]['dur']:.0f}", f"{CH[k]['delay']:.0f}", f"{CH[k]['rwait']:.0f}", f"{CH[k]['delay_off']:.0f}")
               for k, w in [("MIP", "MIP（厳密解）"), ("prtbw3", "新しい最終設定（時間ブロック）"), ("dl", "＋目標工期")]]
    j30 = DL["j30"]
    dl_rows = [("j30 前線中間部の工期差（5回平均）", f"{mn(j30['prtbw3']['mid']):.2f}%", f"{mn(j30['dl']['mid']):.2f}%", "基準 (l)：5回すべてが 8.43% 未満 → 合格"),
               ("j30 工期端=MIP（/240）", f"{mn(j30['prtbw3']['exact']):.1f}", f"{mn(j30['dl']['exact']):.1f}", "基準 (m)：221 以上 → 合格"),
               ("j30 HV（＋目標工期 − 新しい最終設定）", "—", f"{DL['j30_hv']['mean']:+.4f} ± {DL['j30_hv']['se2']:.4f}", f"{DL['j30_hv']['better']}/{DL['j30_hv']['better'] + DL['j30_hv']['worse']} 条件で改善"),
               ("j120（20×3回）HV", "—", f"{DL['j120']['mean']:+.4f} ± {DL['j120']['se2']:.4f}", f"{DL['j120']['better']}/{DL['j120']['better'] + DL['j120']['worse']} 条件で改善"),
               ("j120 最良既知との差", f"{DL['j120']['bks_prtbw3']:.2f}%", f"{DL['j120']['bks_dl']:.2f}%", "")]
    for size, v in DL.get("size", {}).items():
        if size == "j120" and v["n_cond"] < 300:
            continue
        dl_rows.append((f"{size}（HV、最良既知との差）", f"{v['bks_prtbw3']:.2f}%", f"{v['bks_dl']:.2f}%（HV {v['hv']:+.4f} ± {v['hv_2se']:.4f}）", f"{v['better']}/{v['better'] + v['worse']} 条件で改善"))
    ex = DL.get("explore", {}); cal = DL.get("dlcal")
    var_rows = [("目標工期だけ（作業ごとの窓幅なし）", f"{ex['dlonly']['mid']:.1f}%", f"{ex['dlonly']['exact']:.1f}", f"{ex['dlonly']['hv']['mean']:+.4f}", "大きく悪化：作業ごとの窓幅は必要")] if ex else []
    if ex:
        var_rows.append(("目標工期の変異 0.5（本命 0.2）", f"{ex['dlm5']['mid']:.2f}%", f"{ex['dlm5']['exact']:.1f}", f"{ex['dlm5']['hv']['mean']:+.4f}", "0.2 の方が良い（差は小さい）"))
    if cal:
        var_rows.append(("暦（休暇日）を考えた最遅開始", f"{mn(cal['mid']):.2f}%", f"{mn(cal['exact']):.1f}", f"{cal['hv']['mean']:+.4f}", "中間部の基準に届かず不採用（HV は改善）"))
    rp = DL.get("rep")
    if rp:
        var_rows.append(("　同上の再検証（新しい5回ずつ）", f"{mn(rp['dlcal_mid']):.2f}%（目標工期のみ {mn(rp['dl_mid']):.2f}%）", f"{mn(rp['dlcal_exact']):.1f}", f"{rp['hv']['mean']:+.4f}", "中間部が悪い → 不採用を確定"))
    o1 = DL.get("dl1p")
    if o1:
        var_rows.append(("工期島なしの1集団＋目標工期（10/21 指摘3）", f"{mn(o1['mid']):.2f}%", f"{mn(o1['exact']):.1f}", f"{o1['hv']['mean']:+.4f}",
                         f"前線全体は同等（j120 HV {o1['j120_hv']:+.4f}）だが工期端が落ちる（j120 最良既知との差 {o1['j120_bks_dl']:.2f}% → {o1['j120_bks']:.2f}%）→ 2集団のまま"))
    dc = DL.get("dlc")
    if dc:
        var_rows.append(("コスト側の親選択（20% をコスト上位10% から）", f"{mn(dc['mid']):.2f}%", f"{mn(dc['exact']):.1f}", f"{dc['hv']['mean']:+.4f}",
                         f"コスト端の差 {mn(dc['dl_costend']):.3f}% → {mn(dc['costend']):.3f}% に縮むが中間部が薄くなり不採用"))
    rv = DL.get("gap_by_rv_r1", {})
    dl_sec = (
        '<section><h2>9. 前線中間部の差はどこから来るか、max_shift をどう決めるか（10/24 夜）</h2>'
        f'<p>同じコスト以下で比べた中間部の点 {DL["pairs"]:,} 組（j30 全48×6条件）を調べた。遅らせた作業の数（MIP {DL["delay_counts"]["MIP"]["n_delayed"]:.0f}・NSGA {DL["delay_counts"]["NSGA"]["n_delayed"]:.0f}）も、'
        f'遅らせの合計（{DL["delay_counts"]["MIP"]["sum_delay"]:.0f} 対 {DL["delay_counts"]["NSGA"]["sum_delay"]:.0f} コマ）もほぼ同じ。'
        '違いは、最後の作業から逆にたどった「鎖」（工期を決めている作業の列）の上の遅らせだった。</p>'
        + img("figures/Seminar1028b/M1_chain.png", "中間部の工期の内訳。工期の大半は鎖上の遅らせ。NSGA が長い分の大部分も鎖上の遅らせ")
        + table(["", "工期", "鎖上の所要時間", "鎖上の遅らせ", "鎖上の資源待ち", "鎖の外の遅らせ"], ch_rows)
        + '<p>MIP は「工期の上限 ε の中でコストを下げる」ので、遅らせは余裕のある作業に回る。今の復号は工期の目標を持たないので、'
          '各作業が窓の中で一番安い時刻を選び、それが鎖に積み上がる。そこで<strong>各個体に目標工期 D を1つ持たせ、各作業の窓の右端を '
          '「D から先行関係で逆算した最遅開始」で打ち切る</strong>（目標工期の遺伝子）。窓幅は「作業ごとの窓幅」と「目標工期に対する余裕」の小さい方になる。</p>'
        + img("figures/Seminar1028b/M2_deadline.png", "目標工期の遺伝子：j30 の中間部（各回）・工期端、HV の差（j30・j120）")
        + table(["項目", "新しい最終設定", "＋目標工期", "メモ"], dl_rows)
        + '<p class="note">事前登録した基準 (l)(m)(n) をすべて満たしたので採用。ただし仕組みは予想と少し違い、鎖上の遅らせそのものは減らず、'
          '鎖の外（余裕のある作業）への遅らせが増え、鎖の所要時間と資源待ちが短くなる形で効いた。'
          + (f'探索全体で、窓を持つ作業の配置の約49%で目標工期が窓を打ち切っていた（打ち切り幅は平均75コマ、休暇ありでは108コマ）。新しく5回走らせ直しても中間部 {mn(DL["rep"]["dl_mid"]):.2f}%、工期端 {mn(DL["rep"]["dl_exact"]):.1f} で効果は再現した。' if DL.get("rep") else "")
          + '</p>'
        + table(["試したもの（j30）", "中間部", "工期端", "HV（対 新しい最終設定）", "判定"], var_rows)
        + (f'<p class="note">中間部の差は休暇ありの条件で大きい（1回目どうし、RV0 / RV1：新しい最終設定 {rv["prtbw3"]["RV0"]:.1f}% / {rv["prtbw3"]["RV1"]:.1f}%、'
           f'＋目標工期 {rv["dl"]["RV0"]:.1f}% / {rv["dl"]["RV1"]:.1f}%）。休暇日は全資源の容量が0で作業がまたげないため、暦を考えた最遅開始も試したが、'
           '5回平均では基準に届かなかった。</p>' if "prtbw3" in rv and "dl" in rv else "")
        + '</section>\n')
if "nf" in OPT:
    NF = OPT["nf"]; a, b = NF["j30"]["isrgp"], NF["j30"]["prtbw3"]
    nf_rows = [("j30 工期端=MIP（/240、5回平均）", f"{a['exact']:.1f}", f"{b['exact']:.1f}", "基準 (i)：10/21 の最小回 223 以上"),
               ("j30 前線中間部の工期差", f"{a['mid']:.2f}%", f"{b['mid']:.2f}%", "基準 (j)：5回すべてが 10/21 の最良回 9.59% より良い"),
               ("j30 コスト最小端の差", f"{a['costend']:.3f}%", f"{b['costend']:.3f}%", ""),
               ("j30 HV（新しい − 10/21）", "—", f"{NF['j30_hv']['mean']:+.4f} ± {NF['j30_hv']['se2']:.4f}", f"{NF['j30_hv']['better']}/{NF['j30_hv']['better'] + NF['j30_hv']['worse']} 条件で改善"),
               ("j120（20×3回）最良既知との差", f"{NF['j120']['bks_old']:.2f}%", f"{NF['j120']['bks_new']:.2f}%", "工期端はほぼ同じ"),
               ("j120 HV（新しい − 10/21）", "—", f"{NF['j120']['hv']:+.4f} ± {NF['j120']['hv_2se']:.4f}", f"{NF['j120']['better']}/{NF['j120']['n']} 条件で改善")]
TF = "timing_1024.csv" if os.path.exists(os.path.join(A, "timing_1024.csv")) else "timing_1023.csv"
tim = list(csv.DictReader(open(os.path.join(A, TF), encoding="utf-8")))
tim_rows = []
for p in dict.fromkeys(r["instance"] for r in tim):
    g = {r["method"]: r["seconds"] for r in tim if r["instance"] == p}
    tim_rows.append((p, g.get("base"), g.get("prbwisrgp"), g.get("prtbw3", "—")) if TF == "timing_1024.csv"
                    else (p, g.get("base"), g.get("isrgp"), g.get("prbwisrgp"), g.get("prtbw3", "—")))
tim_head = ["インスタンス", "現在の手法（秒）", "10/21 の最終設定（秒）", "新しい最終設定（秒）"] if TF == "timing_1024.csv" else \
    ["インスタンス", "現在の手法（秒）", "工期島＋部分再構築（秒）", "10/21 の最終設定（秒）", "新しい最終設定（秒）"]
short_rows = [("評価回数 1/4", SH["q_j30_exact"], SH["q_j30_hv"], f"{SH['q_j120_bks']}%", SH["q_j120_hv"]),
              *([("新しい最終設定・評価回数 1/4", OPT["sh2"]["q_j30_exact"], OPT["sh2"]["q_j30_hv"], f"{OPT['sh2']['q_j120_bks']}%", OPT["sh2"]["q_j120_hv"])] if "sh2" in OPT else []),
              *([("＋目標工期・評価回数 1/4", OPT["sh3"]["q_j30_exact"], OPT["sh3"]["q_j30_hv"], f"{OPT['sh3']['q_j120_bks']}%", OPT["sh3"]["q_j120_hv"])] if "sh3" in OPT else []),
              *([("＋目標工期・評価回数 1/2", OPT["sh3"]["h_j30_exact"], OPT["sh3"]["h_j30_hv"], f"{OPT['sh3']['h_j120_bks']}%", OPT["sh3"]["h_j120_hv"])] if "h_j30_hv" in OPT.get("sh3", {}) else []),
              ("評価回数 1/2", SH["h_j30_exact"], SH["h_j30_hv"], f"{SH['h_j120_bks']}%", SH["h_j120_hv"]),
              ("（参考）現在の手法・全評価", SH["base_j30_exact"], "—", f"{SH['base_j120_bks']}%", "—")]
qa = [
    ("工期島も本体も操作は同じなのに、なぜ工期島では良い解が出るのか", "元の NSGA-II の親と子を数えた（j30 全1,152実行）",
     f"良い作業リストを持つ子は生まれているが、受け継いだ窓幅で工期が伸び見逃される（{D['ratio']}倍）。工期島は窓幅0で評価するので見逃さない。工期側の親は {D['parents_near']}% 選ばれている"),
    ("max_shift を0にするのは NSGA-II でもできそう", "1集団で子の窓幅を0にする（zw）", f"工期端 {AB['zw']['exact']}（工期島 {AB['isrgp']['exact']}）で可能。ただし前線全体（HV）が下がる"),
    ("理想は1集団。親の何%かをメイクスパン上位から選ぶなど", "9通りの1集団版を事前登録の基準で判定", "工期端だけなら1集団で同等。最終設定どうしでは前線中間部と j120 の HV で2集団が上 → 2集団を標準に"),
    ("1集団でだめなら理由を", "停止の仕方・受け入れ方・予算の使い方を比べた",
     "途中段階の窓幅0の作業リストをためる場所が要る。1集団ではコストで負けて捨てられるので、予算を使い続けるか早く止まるかになる。工期島は約30世代で止まり予算の約1.4%しか使わない"),
    ("コストも似たように", "親の20%をコスト上位10%から", f"コスト最小端の差 {N['cost_sel']['costend_mszw']}% → {N['cost_sel']['costend_mszwc']}%。ただし中間部が薄くなる（{N['cost_sel']['mid']}%）"),
    ("計算時間は？", "他の計算を止め、手法を交互に1インスタンスずつ測定", f"評価回数は同じ。j30 では差なし（約40秒）、j120 では現在の手法 {TT['base']:.0f}秒、10/21 の最終設定 {TT['prbwisrgp']:.0f}秒、新しい最終設定 {TT['prtbw3']:.0f}秒"),
    ("元の NSGA-II でも最良解は残るはず、なぜ工期側が悪かった", "同上の数え上げ", "最良解は残るが、その子は相手の親の窓幅を受け継ぐので工期が伸びた子しか生まれない"),
    ("j90、j120 ではどうか", "作業数30・60・90・120 で比較", f"作業数が増えるほど効果が大きい（j120: {J120['gap']['base']:.1f}% → {J120['gap']['prtbw3']:.1f}%、HV +{J120['hv']['prtbw3']:.4f}）"),
]
past = [
    ("9/10", "短時間でより良い品質の解", "達成（評価回数1/2で上回る）", f"工期端は評価回数1/4で現在の手法より良い（j120 {SH['q_j120_bks']}% 対 {SH['base_j120_bks']}%）。前線全体は今の評価回数近くが必要。新しい最終設定では 1/4 での不足が縮み（j120 HV {SH['q_j120_hv'].split('（')[0]} → {OPT.get('sh2', {}).get('q_j120_hv', '').split('（')[0]}）、＋目標工期では 1/4 で j120 が全評価の現在の手法と同等、1/2 では j30・j120 とも全評価の現在の手法より良い（j30 HV {OPT.get('sh3', {}).get('h_j30_hv', '')}）"),
    ("9/10", "サイズを変えても MaxShift 系が常に良いか", "確認済み" if "enc" in OPT else "実行中", OPT.get("enc", {}).get("summary", "SchedObj を j90・j120 で実行中")),
    ("9/10", "P2・P3 をソルバーで解いて比較", OPT.get("p23", {}).get("status", "一部"), OPT.get("p23", {}).get("summary", "j309_1 のみ。他のインスタンスは MIP の計算量が大きく未実施")),
    ("10/1", "max_shift をどう決めるか", "答えを提示（§9）", "ブロック窓変異 → 時間ブロック窓 → 目標工期の遺伝子。窓幅 = 作業ごとの小さい窓幅と、目標工期から逆算した余裕の小さい方（j30 中間部 9.65 → 7.98%）" + (f"。確認: {OPT['tbc']['summary']}" if "tbc" in OPT else "")),
    ("10/1", "提案手法が既存手法より優れている理由", "済", "SchedObj は窓幅が評価ごとの乱数で子に受け継がれない"),
    ("10/14", "初期解・探索・仮説検証・初期解の分布", "済", "10/21 に報告"),
]
rem_sec = ""
if "rem" in OPT:
    p23f_rows = []
    if "p23f" in OPT:
        _f = {(r["instance"], r["cond"], r["mode"]): r for r in OPT["p23f"]["rows"]}
        for _p in dict.fromkeys(r["instance"] for r in OPT["p23f"]["rows"]):
            p23f_rows.append((_p, *[" / ".join(f"{_f[(_p, c, m)]['hv_cur']}→{_f[(_p, c, m)]['hv_final']}" if (_p, c, m) in _f else "—"
                                             for c in ["RR000_RV0", "RR000_RV1"]) for m in ["P1", "P2", "P3"]]))
        _m6 = [r for r in OPT["p23f"]["rows"] if r.get("hv_mip600") not in ("", None)]
    rem_sec = ('<section><h2>11. 残っていた課題5つ（10/28 夜〜10/29 朝）</h2><p class="note">各施策の基準は実行前に '
               'tools/ext1028/PREREG_20261028.md に書いて固定。比べる相手は最新の最終設定（＋目標工期、dl）。j30 は全48×6条件を各5回（課題5は3回）、j120 は20インスタンス×各3回（課題5は2回）。</p>'
               + table(["課題", "原因（診断で分かったこと）", "施策", "基準", "結果", "判定"], OPT["rem"]["rows"])
               + ('<h3>P1・P2・P3 の HV 比（MIP ＝ 1）：現在の手法 → 最終設定（RV0 / RV1）</h3>' + table(["問題例", "P1", "P2", "P3"], p23f_rows)
                  + img("figures/Splitting_EXT/p123_final_j309_1.png", "j309_1：MIP（黒）、NSGA-II 現在の手法（灰）、最終設定（橙）。差が最も大きかった問題例")
                  + '<p class="note">HV は MIP・現在の手法・最終設定の3者を合わせた範囲で正規化。HV 比が 1 を少し超えるのは、MIP を工期の刻み（10〜26点）でしか解いていないため（刻みの間を NSGA の点が埋める）。両端の工期は一致。'
                  + (f'P3 の MIP を600秒で解き直した条件（j309_1）：' + "、".join(f"{r['cond'][-3:]} 最適性証明 {r['mip_proven']}→{r['mip600_proven']}、HV 比 {r['hv_mip600']}" for r in _m6 if r['mode'] == 'P3') if _m6 else "")
                  + '</p>' if p23f_rows else "")
               + '</section>')
page = f"""<title>10/28 ゼミ報告</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Noto+Sans+JP:wght@400;500;700&family=Zen+Kaku+Gothic+New:wght@700&display=swap">
<style>
/* 研究報告の1枚ページ: 1カラム、図は白い台紙（matplotlib の図が白背景のため） */
:root {{ --bg:#f5f6f3; --fg:#1f2421; --muted:#5d665f; --line:#d9ded7; --card:#ffffff; --accent:#b3261e;
  --font-body:"Noto Sans JP","Hiragino Sans","Yu Gothic",sans-serif; --font-head:"Zen Kaku Gothic New","Noto Sans JP",sans-serif; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ --bg:#161917; --fg:#e7ebe6; --muted:#a3ada5; --line:#323a34; --card:#1f2521; --accent:#f2b8b5; color-scheme:dark; }} }}
:root[data-theme="dark"] {{ --bg:#161917; --fg:#e7ebe6; --muted:#a3ada5; --line:#323a34; --card:#1f2521; --accent:#f2b8b5; color-scheme:dark; }}
body {{ background:var(--bg); color:var(--fg); font-family:var(--font-body); line-height:1.7; }}
.wrap {{ max-width:1100px; margin:0 auto; padding-inline:16px; padding-block:28px 64px; display:flex; flex-direction:column; gap:28px; }}
h1,h2 {{ font-family:var(--font-head); text-wrap:balance; margin:0; }}
h1 {{ font-size:1.7rem; }} h2 {{ font-size:1.25rem; border-bottom:2px solid var(--accent); padding-bottom:4px; }}
.lead {{ color:var(--muted); max-width:70ch; margin:6px 0 0; }}
.kpis {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(200px,1fr)); gap:12px; }}
.kpi {{ background:var(--card); border:1px solid var(--line); border-radius:8px; padding:12px 14px; display:flex; flex-direction:column; gap:4px; }}
.kpi .k {{ font-size:.8rem; color:var(--muted); }} .kpi .v {{ font-size:1.45rem; font-weight:700; color:var(--accent); font-variant-numeric:tabular-nums; }}
.kpi .s {{ font-size:.8rem; color:var(--muted); }}
section {{ display:flex; flex-direction:column; gap:12px; min-width:0; }}
.tw {{ overflow-x:auto; }}
table {{ border-collapse:collapse; width:100%; font-size:.86rem; background:var(--card); }}
th,td {{ border:1px solid var(--line); padding:6px 8px; vertical-align:top; text-align:left; }}
th {{ background:color-mix(in srgb, var(--accent) 10%, var(--card)); }}
td {{ font-variant-numeric:tabular-nums; }}
figure {{ margin:0; background:#fff; border:1px solid var(--line); border-radius:6px; padding:8px; }}
figure img {{ display:block; width:100%; height:auto; }}
figcaption {{ color:#4a524c; font-size:.82rem; margin-top:4px; }}
.missing {{ color:var(--muted); }} .note {{ font-size:.85rem; color:var(--muted); }}
</style>
<div class="wrap">
<header><h1>10/21 の指摘への対応（10/28 ゼミ）</h1>
<p class="lead">工期島はなぜ効くのか、1つの集団でできるか。判定の基準は実行前に tools/ext1023/PREREG_20261023.md に書いて固定。j30 は全48インスタンス×6条件を各3〜5回、j60・j90・j120 は各2回。</p></header>
<div class="kpis">{''.join(f'<div class="kpi"><span class="k">{html.escape(k)}</span><span class="v">{html.escape(v)}</span><span class="s">{html.escape(s)}</span></div>' for k, v, s in kpis)}</div>
<section><h2>指摘と対応</h2>{table(["指摘", "やったこと", "結果"], qa)}</section>
<section><h2>1. 元の NSGA-II は良い作業リストを見逃していた</h2>{img("figures/Seminar1028/D1b_missed_lists.png", "子100万体あたり：実際に集団の最良工期を更新した子と、作業リスト（窓幅0）なら更新していた子")}</section>
<section><h2>2. 工期島の何が効いているか（1集団で一つずつ再現）</h2>{img("figures/Seminar1028/D2_ablation.png", "工期端・探索の到達・HV（工期島との差）")}{table(["手法", "工期端=MIP（/240）", "初期で届かず探索で到達", "HV（工期島との差）", "判定"], ab_rows)}</section>
<section><h2>3. 最終設定どうし：1集団 vs 2集団</h2>{img("figures/Seminar1028/D3_onepop_vs_twopop.png", "j30 の工期端・中間部（各5回）と HV の差（j30・j120）")}
<p class="note">工期島は約30世代で止まり、使う評価は全体の約1.4%（50世代目の評価回数：本体だけ 5,100、工期島あり 5,820。480世代目 48,100 と 48,820）。1集団版は子の17%を最後まで工期側に使うか、同じ止め方では早く止まりすぎる。</p></section>
<section><h2>4. 作業数を増やしても効くか</h2>{img(size_fig, "容量一定の条件で PSPLIB 最良既知値（j30 は最適値）との差、全条件の HV")}{table(size_head, size_rows)}</section>
<section><h2>5. 計算時間</h2>{(table(["インスタンス", "現在の手法（秒）", "時間ブロック（秒）", "＋目標工期（秒）"],
    [(i, *[next((r["seconds"] for r in OPT["dl"]["timing"] if r["instance"] == i and r["method"] == m), "—") for m in ["base", "prtbw3", "dl"]])
     for i in dict.fromkeys(r["instance"] for r in OPT["dl"]["timing"])]) + '<p class="note">10/24 夜に同じ時間帯に交互に測り直した値（上の表）。目標工期で窓の右端が打ち切られるので、時間ブロックだけのときより速い。下の表と図は 10/24 朝の測定。</p>') if "dl" in OPT and OPT["dl"].get("timing") else ""}<p class="note">10/23 に測った値と同じ時間帯に測り直した値が 1.3〜1.4倍ずれたので（PC の状態による）、手法をインスタンスごとに交互に実行して測り直した。集団の平均窓幅（評価回数で重み付け、j1201_1 の24実行）は現在の手法 110、10/21 の最終設定 120（+9%）、新しい最終設定 123（+12%）。窓幅が大きいほど復号で調べる開始時刻が増えるので、10/21 の最終設定の時間増（+8%）はこれで説明できるが、新しい最終設定の +23% は半分ほどしか説明できない（残りは未確認）。</p>{img("figures/Seminar1028/D4_timing.png", "1インスタンス（6条件×4戦略×各5万評価）の壁時計時間")}{table(tim_head, tim_rows)}</section>
<section><h2>6. 前回言いそびれた：ブロック窓変異と、その次の一歩</h2>{img("figures/Seminar1021/F6_blockwin.png", "ブロック窓変異：連続する数作業の窓幅を一緒に動かすと前線中間部が縮む")}{img("figures/Seminar1028/D6_timeblock.png", "まとめる作業を「開始時刻が近い作業」にすると、さらに中間部が縮み、j120 の前線全体も良くなる（事前登録の判定で採用）")}{img("figures/Seminar1021/F8_blockwin_rate.png", "ブロック窓変異の確率を上げるほど中間部が縮む（探索的）")}
<h3>新しい最終設定：10/21 の最終設定のブロック窓を時間ブロック窓に置き換え</h3>{table(["項目", "10/21 の最終設定", "新しい最終設定", "メモ"], nf_rows) if nf_rows else ""}
<p class="note">新しい最終設定 = 優先規則の初期解（半分）＋工期島（20体、30世代停滞で停止）＋部分再構築（10%）＋時間ブロック窓変異（30%）。採用の判定は実行前に事前登録した基準 (i) j30 の工期端の5回平均が 10/21 の最終設定の最小回以上 (j) 中間部の5回平均が 10/21 の最良回より小さい (k) HV が j30・j120 とも下がらない、で行い、3つとも満たした。</p></section>
<section><h2>7. 短時間で良い品質の解（9/10 の指摘）</h2><p class="note">HV は現在の手法（全評価回数）との差。1行目・2行目は 10/21 の最終設定。</p>{table(["設定と評価回数", "j30 工期端=MIP", "j30 HV（現在・全評価との差）", "j120 最良既知との差", "j120 HV"], short_rows)}</section>
<section><h2>8. P2・P3 をソルバーで解いて比較（9/10 の指摘、{len(p23_rows)}問題例）</h2><p>NSGA-II（現在の手法）の HV ÷ MIP の HV。1 未満なら MIP の前線が上。MIP は各点120秒で打ち切り、P3 の MIP は P1 の解を初期解に与えている。</p>{table(["問題例", "P1（RV0 / RV1）", "P2（RV0 / RV1）", "P3（RV0 / RV1）"], p23_rows)}{img("figures/Splitting_EXT/p123_mip_vs_nsga_j3021_1.png", "j3021_1（3例目）：MIP（黒）と NSGA-II 現在の手法（橙）")}{img("figures/Splitting_EXT/p123_mip_vs_nsga_j3011_1.png", "j3011_1：MIP（黒）と NSGA-II 現在の手法（橙）。上段は容量一定（RV0）、下段は休暇あり（RV1）")}{img("figures/Splitting_EXT/p123_mip_vs_nsga_j309_1.png", "（前回）j309_1：同じ比較に SchedObj・MaxShift も重ねたもの")}<p class="note">{html.escape(OPT.get("p23", {}).get("summary", ""))}。問題例の数が少ないので一般化はしない。</p></section>
{dl_sec}<section><h2>10. 過去の指摘の確認</h2>{table(["ゼミ", "指摘", "状態", "内容"], past)}</section>{rem_sec}
<p class="note">公式の結果（results/FUN_MIP、results/FUN_ENC）とコスト表は変更していない。新しいオプションはすべて既定で OFF。</p>
</div>"""
out = os.path.join(A, "report_1028.html")
open(out, "w", encoding="utf-8").write(page)
print(out, os.path.getsize(out) // 1024, "KB")
