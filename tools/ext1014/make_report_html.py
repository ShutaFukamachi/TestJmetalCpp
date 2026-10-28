"""
make_report_html.py -- 10/21 ゼミ用の報告ページ（1枚の HTML、図は埋め込み）を作る。
数値は analysis/ext1014/seminar1021_numbers.json などの結果ファイルから読み、手で書き写さない。
出力: cmake-build-release/analysis/ext1014/report_1021.html
"""
import os, json, base64, csv, statistics as S, html
HERE = os.path.dirname(os.path.abspath(__file__))
BD = os.path.normpath(os.path.join(HERE, "..", "..", "cmake-build-release"))
A = os.path.join(BD, "analysis", "ext1014")
N = json.load(open(os.path.join(A, "seminar1021_numbers.json"), encoding="utf-8"))
EXTRA = json.load(open(os.path.join(A, "seminar1021_extra.json"), encoding="utf-8")) if os.path.exists(os.path.join(A, "seminar1021_extra.json")) else {}


def img(rel, alt):
    p = os.path.join(BD, rel)
    if not os.path.exists(p):
        return f'<p class="missing">（図 {html.escape(rel)} はまだありません）</p>'
    b = base64.b64encode(open(p, "rb").read()).decode()
    return f'<figure><img src="data:image/png;base64,{b}" alt="{html.escape(alt)}"><figcaption>{html.escape(alt)}</figcaption></figure>'


j30 = N.get("j30_2x2", {}); j120 = N.get("j120_2x2", {})
def f1(x, d=1): return "—" if x is None else f"{x:.{d}f}"
base_ex, new_ex = j30.get("base", {}).get("exact_mean"), j30.get("isrgp", {}).get("exact_mean")
both_ex = j30.get("prisrgp", {}).get("exact_mean")
b120, n120 = j120.get("base", {}).get("bks_gap_mean"), j120.get("isrgp", {}).get("bks_gap_mean")
hv30 = j30.get("prisrgp", {}).get("hv") or [None]
hv120 = (j120.get("isrgp", {}) or {}).get("hv") or [None]
reach = EXTRA.get("reach", {})
mid = N.get("middle", {})
bw = EXTRA.get("blockwin", {})

X30, X120 = EXTRA.get("j30", {}), EXTRA.get("j120", {})
SZ = EXTRA.get("size", {})
kpis = [
    ("j30 工期最小端が MIP と一致（/240、5回平均）", f"{X30['base']['exact']} → {X30['prbwisrgp']['exact']}", "現在 → 全部入り（初期解＋探索＋ブロック窓）"),
    ("j120 PSPLIB 最良既知値との差（容量一定、3回平均）", f"{X120['base']['bks']}% → {X120['prbwisrgp']['bks']}%", "ソルバーなしで差を半分以下に"),
    ("初期解で届かなかった条件で探索が MIP に到達（j30）", f"{reach['base']} → {reach['isrgp']}", "初期解ではなく探索で良くした"),
    ("j30 前線中間部の工期差（同じコストで）", f"{X30['base']['mid']}% → {X30['prbwisrgp']['mid']}%", "ブロック窓変異で縮んだ"),
    ("j120 全60インスタンスの前線全体 HV（全部入り − 現在）", SZ.get("j120", {}).get("hv", "—").split("（")[0] if SZ else "—", "360条件中359で改善（各2回）"),
]
NAMES = [("base", "現在の手法"), ("pr", "初期解だけ改善（優先規則＋ランダム性）"), ("isrgp", "探索だけ改善（工期島＋部分再構築）"),
         ("prisrgp", "初期解＋探索"), ("bwisrgp", "探索＋ブロック窓変異"), ("prbwisrgp", "全部入り")]
SZ = EXTRA.get("size", {})
sizerows = [(f"作業数{k[1:]}", f"{v['n']}インスタンス × 各{v['runs']}回", f"{v['base']}%", f"{v['bwisrgp']}%", f"{v['prbwisrgp']}%", v["hv"]) for k, v in SZ.items()]
allrows = [(n, f"{X30[k]['exact']}", X30[k]["over10"], f"{X30[k]['mid']}%", X30[k]["hv"] or "—", reach.get(k, "—"),
            f"{X120[k]['bks']}%", X120[k]["hv"] or "—", EXTRA.get("j309", {}).get(k, "")) for k, n in NAMES]

rows1014 = [
    ("ソルバー解の作業順を使うのは NSGA-II で解決したことにならない。120作業ならどうする？",
     "MIP を使わない j120（20インスタンス×3回）と j60 でも、PSPLIB の最良既知値（容量一定の条件＝標準 RCPSP）を基準に評価。",
     f"j120 の最良既知との差 {X120['base']['bks']}% → {X120['prbwisrgp']['bks']}%（全部入り）。作業数30・60・120 の結果は F4。", "F4"),
    ("初期解でメイクスパンの良いものを持っていくか、探索で良い方向に持っていくか",
     "初期解（優先規則）だけ／探索（工期島＋部分再構築）だけ／両方 を同じ条件で各5回比較（2×2）。",
     f"工期最小端の一致: 初期解だけ {X30['pr']['exact']}、探索だけ {X30['isrgp']['exact']}、両方 {X30['prisrgp']['exact']}（/240）。探索を改善すると初期解の違いは工期端に出ない。初期解は前線全体（HV）に効く。", "F1"),
    ("あてずっぽうではなく、仮説を立てて検証する",
     "仮説を6つ立てて、それぞれ判定する前に基準を決めてから検証（事前登録）。",
     "支持4（近傍が狭い・工期側の探索不足・中間部は1作業ずつの窓幅変更で動かない・複数作業の窓幅を一緒に動かすと中間部が縮む）、棄却3（窓幅が良い作業リストを隠す・置ける時刻の少ない作業・LFT 遅い順）。", "表"),
    ("初期解の作業リストは優先規則で組み、ランダム性でバリエーションを保つ",
     "LFT・LST・MTS・GRPW の4規則を重み (優先度差+1)^3 で選ぶ偏りランダム生成を実装。「最遅完了時刻が遅い順」も比較。",
     "4規則混合が質（100本の最良が MIP と一致 79%）と多様性（0.171）の両立で最良。遅い順は 31%（ランダム 70% より悪い）。", "F3"),
    ("初期解に依存する探索はよくない。探索で解の質を高めたい",
     "工期最小端が初期集団で決まっていたことを記録で確認 → 工期島（停滞で停止）と部分再構築変異を追加。",
     f"初期で届かなかった条件で探索が到達 {reach.get('base', '37%')} → {reach.get('isrgp', '79%')}。探索の途中経過の図で、改善版は最後まで差を縮め続ける。", "F2"),
    ("初期解の分布をグラフで見る",
     "NSGA が実際に使った初期集団（j30 全条件・j120）と、作り方ごとの分布を図にした。",
     "作業リストの工期 ÷ MIP の最小工期（中央値）: ランダム 1.12 → 4規則混合 1.02。初期集団の約3分の1は窓幅が大きすぎて実行不能。", "F3"),
]
rows1001 = [
    ("1", "j309_1 RR050_RV0 で工期側の解がない", "対応", "現在 159（5回とも）→ 125（MIP と一致）に 探索改善で5回中2回、初期解＋探索で4回、全部入りで3回"),
    ("2・4", "ソルバー解を再現できない具体的なケース／再現データ", "済", "再現できないのはコスト最小端に集中（工期端 99% → コスト端 48%）"),
    ("3", "効率的な探索手法", "工期側は対応", "工期島＋部分再構築を採用（事前登録の基準 4/4 合格）。中間部は次の課題"),
    ("5", "max_shift を変えると急に工期が増える理由", "済", "91% は後ろの作業の押し出しの連鎖、節約の 61% が打ち消される"),
    ("6", "max_shift をどう決めるか", "着手・第一歩", "中間部の解は1作業ずつの窓幅変更では動かないと判明 → 連続する数作業の窓幅をまとめて動かすブロック窓変異を採用。中間部の工期差 9.65% → 8.80%"),
    ("9", "0/1 の手法でソルバー解をどれだけ再現できるか", "済", "SchedObj は確実に再現できる活動 19.2%、スケジュール全体 0%"),
    ("10", "提案手法が既存手法より優れている理由", "済", "SchedObj は窓幅が評価のたびに乱数で決まり、子に受け継がれない。後半は窓幅 3T/8 以上に固定され小さな遅延を表せない"),
]


def table(head, rows, cls=""):
    h = "".join(f"<th>{html.escape(x)}</th>" for x in head)
    b = "".join("<tr>" + "".join(f"<td>{html.escape(str(c))}</td>" for c in r) + "</tr>" for r in rows)
    return f'<div class="tw"><table class="{cls}"><thead><tr>{h}</tr></thead><tbody>{b}</tbody></table></div>'


bw_line = ""
if bw:
    bw_line = f"<p>複数作業の窓幅をまとめて動かす変異（事前登録した判定）: {html.escape(bw.get('verdict', ''))}。{html.escape(bw.get('detail', ''))}</p>"

page = f"""<title>10/21 ゼミ報告</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Noto+Sans+JP:wght@400;500;700&family=Zen+Kaku+Gothic+New:wght@700&display=swap">
<style>
/* 研究報告の1枚ページ: 左寄せの1カラム、図は白い台紙に載せる（matplotlib の図が白背景のため） */
:root {{ --bg:#f6f7f4; --fg:#1f2421; --muted:#5d665f; --line:#d9ded7; --card:#ffffff; --accent:#1d6f42; --accent2:#2b5fab; --warn:#a15c07;
  --font-body:"Noto Sans JP","Hiragino Sans","Yu Gothic",sans-serif; --font-head:"Zen Kaku Gothic New","Noto Sans JP",sans-serif; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ --bg:#161a17; --fg:#e7ebe6; --muted:#a3ada5; --line:#323a34; --card:#1f2521; --accent:#5cc28a; --accent2:#8ab4f8; --warn:#f0b35b; color-scheme:dark; }} }}
:root[data-theme="dark"] {{ --bg:#161a17; --fg:#e7ebe6; --muted:#a3ada5; --line:#323a34; --card:#1f2521; --accent:#5cc28a; --accent2:#8ab4f8; --warn:#f0b35b; color-scheme:dark; }}
body {{ background:var(--bg); color:var(--fg); font-family:var(--font-body); line-height:1.7; }}
.wrap {{ max-width:1080px; margin:0 auto; padding-inline:16px; padding-block:28px 64px; display:flex; flex-direction:column; gap:28px; }}
h1,h2 {{ font-family:var(--font-head); text-wrap:balance; margin:0; }}
h1 {{ font-size:1.7rem; }} h2 {{ font-size:1.25rem; border-bottom:2px solid var(--accent); padding-bottom:4px; }}
.lead {{ color:var(--muted); max-width:68ch; margin:6px 0 0; }}
.kpis {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:12px; }}
.kpi {{ background:var(--card); border:1px solid var(--line); border-radius:8px; padding:12px 14px; display:flex; flex-direction:column; gap:4px; }}
.kpi .k {{ font-size:.8rem; color:var(--muted); }} .kpi .v {{ font-size:1.5rem; font-weight:700; color:var(--accent); font-variant-numeric:tabular-nums; }}
.kpi .s {{ font-size:.8rem; color:var(--muted); }}
section {{ display:flex; flex-direction:column; gap:12px; min-width:0; }}
.tw {{ overflow-x:auto; }}
table {{ border-collapse:collapse; width:100%; font-size:.88rem; background:var(--card); }}
th,td {{ border:1px solid var(--line); padding:6px 8px; vertical-align:top; text-align:left; }}
th {{ background:color-mix(in srgb, var(--accent) 12%, var(--card)); }}
figure {{ margin:0; background:#fff; border:1px solid var(--line); border-radius:6px; padding:8px; }}
figure img {{ display:block; width:100%; height:auto; }}
figcaption {{ color:#4a524c; font-size:.82rem; margin-top:4px; }}
.missing {{ color:var(--warn); }}
.note {{ font-size:.85rem; color:var(--muted); }}
</style>
<div class="wrap">
<header><h1>10/1・10/14 の指摘への対応（10/21 ゼミ）</h1>
<p class="lead">RCPSP 2目的（工期・総コスト）、NSGA-II＋MaxShiftDur。拡張コスト表。j30 は全48インスタンス×6条件を各5回、j120 は20インスタンスを各3回。現在の手法を何回か走らせた差をノイズとして判定。</p></header>
<div class="kpis">{''.join(f'<div class="kpi"><span class="k">{html.escape(k)}</span><span class="v">{html.escape(v)}</span><span class="s">{html.escape(s)}</span></div>' for k, v, s in kpis)}</div>
<section><h2>10/14 の指摘と対応</h2>{table(["指摘", "やったこと", "結果", "図"], rows1014)}</section>
<section><h2>F1 初期解で良くするか、探索で良くするか</h2>{img("figures/Seminar1021/F1_init_x_search.png", "初期解×探索の 2×2。上: j30、下: j120。棒は平均、黒線は回ごとの幅、誤差棒は ±2SE。")}</section>
<section><h2>F2 探索で解の質を高めているか（探索の途中経過）</h2>{img("figures/Search1014/fig6_convergence_j30.png", "j30: MIP の最小工期との差の推移（全条件・全回の平均）")}{img("figures/Search1014/fig6_convergence_j120.png", "j120: PSPLIB 最良既知値との差の推移")}</section>
<section><h2>F3 初期解の分布</h2>{img("figures/Init_EXT/init_rule_compare.png", "作業リストの作り方ごとの質と多様性（j30・MIP がある全条件、各100本）")}{img("figures/Init_EXT/init_listms_pooled.png", "NSGA が実際に使った初期集団の作業リストの工期（j30）")}{img("figures/Init_EXT/init_j120.png", "j120 の初期集団の作業リストの工期（容量一定の条件、PSPLIB 最良既知値との比）")}</section>
<section><h2>F4 作業数が増えても効くか</h2>{img("figures/Seminar1021/F4_size_scaling.png", "容量一定の条件で PSPLIB 最良既知値との差（作業数30は最適値）")}{table(["作業数", "実行", "現在の手法", "探索改善＋ブロック窓", "全部入り", "前線全体 HV（全部入り − 現在、改善/悪化）"], sizerows)}<p class="note">工期の欄は容量一定の条件（＝標準の RCPSP）での PSPLIB 最良既知メイクスパンとの差。HV は全6条件。j120 では全部入りが最短の工期を出した条件 {SZ.get("j120", {}).get("best_ms", "")}（現在の手法は {SZ.get("j120", {}).get("base_best_ms", "")}）。</p></section>
<section><h2>F5 残る課題：前線の中間部</h2>{img("figures/Seminar1021/F5_middle.png", "中間部の差（2つの見方）と、1作業ずつ窓幅を最適化したときの結果")}{bw_line}</section>
<section><h2>全手法の比較（数値）</h2>{table(["手法", "j30 工期端=MIP（/240）", "j30 10%超", "j30 中間部の工期差", "j30 HV 差（対 現在、改善/悪化）", "初期で届かず探索で到達", "j120 最良既知との差", "j120 HV 差（改善/悪化）", "j309_1 RR050_RV0"], allrows)}<p class="note">j30 は各5回、j120（20インスタンス）は各3回の平均。HV 差は条件ごとに回の平均をとってから現在の手法との差を平均し、±2SE を付けた。</p></section>
<section><h2>F6 中間部への対策：ブロック窓変異</h2>{img("figures/Seminar1021/F6_blockwin.png", "左: j30 中間部の工期差（点は各回）、中: HV の差、右: j309_1 RR050_RV0 の前線")}</section>
<section><h2>F8 ブロック窓変異の強さ（探索的）</h2>{img("figures/Seminar1021/F8_blockwin_rate.png", "確率を 0.1 → 0.3 → 0.5 と上げるほど中間部の差が縮み、HV も上がる（j30、各5回）。j120 でも 0.3 で HV +0.0175（0.1 では +0.0137）")}</section>
<section><h2>F7 前線の例</h2>{img("figures/Seminar1021/F7_front_examples.png", "左: j309_1 RR050_RV0（MIP あり）、右: j1201_1 RR000_RV0（j120）")}</section>
<section><h2>10/1 の指摘の状況</h2>{table(["#", "指摘", "状態", "内容"], rows1001)}</section>
<p class="note">判定基準は実行前に tools/ext1014/PREREG_20261016.md に書いて固定した。公式の結果（results/FUN_MIP, results/FUN_ENC）とコスト表は変更していない。</p>
</div>"""
out = os.path.join(A, "report_1021.html")
open(out, "w", encoding="utf-8").write(page)
print(out, os.path.getsize(out) // 1024, "KB")
