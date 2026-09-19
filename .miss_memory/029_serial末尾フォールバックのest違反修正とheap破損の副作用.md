# 029: Serial SGS 末尾フォールバックの est 違反を修正 → 副作用で heap 破損が新規発生・対処

## 発見日
2026-08-10〜11（[[022]] で発見されていた「serial デコーダ末尾フォールバックが先行制約違反の
phantom を生む」バグの本修正。48インスタンス実測で 864行中82行・1,742解（該当行の21%）に
影響していたことを受けて着手）

## 修正① 末尾フォールバックが est を下回って配置する問題（原因①）

### 症状（修正前）
`RCPSP_Problem.cpp`（SchedObj）/ `RCPSP_Problem_MaxShift.cpp`（MaxShift）/
`RCPSP_Problem_MaxShiftDur.cpp`（MaxShiftDur）/ `RCPSP_Problem_MaxShiftSeg.cpp` の
serial SGS で、`[est, T)` にジョブを置けないとき、ホライゾン末尾から **est を無視して 0 まで**
後退探索していた:
```cpp
if (t_mak >= T) {
    t_mak = std::max(0, T - d);
    while (t_mak >= 0 && !canPlace(j, t_mak)) --t_mak;   // est を無視
    if (t_mak < 0) { /* 1e9 */ }
}
```
時変容量(RR>0)で稀に `[est, T)` に置けないジョブが生じると、**est より前**に配置され、
先行制約違反のスケジュールが「実行可能な良い解」として出力されていた。

### 修正
後退探索の下限を `est` に限定し、`[est, T-d]` に置けなければ実行不能(1e9)として扱う:
```cpp
if (t_mak >= T) {
    t_mak = T - d;
    while (t_mak >= est && !canPlace(j, t_mak)) --t_mak;
    if (t_mak < est) { /* 1e9 として扱う（est を破ってまで解を作らない）*/ }
}
```
対象: `RCPSP_Problem.cpp`（SchedObj）, `RCPSP_Problem_MaxShift.cpp`（MaxShift）,
`RCPSP_Problem_MaxShiftDur.cpp`（MaxShiftDur。dummy 側は既に est 修正済みだった）,
`RCPSP_Problem_MaxShiftSeg.cpp`。

## 修正② ダミー終端の開始時刻が 0 固定される問題（原因②・軽微）
所要時間0のジョブについて `start[j]=finish[j]=0` としていたため、ダミー終了ノードが t=0 に
記録され、先行ジョブの完了時刻より前になって「違反」と誤判定されていた（スケジュール自体・
目的値は正しく、記録上の問題のみ）。`est` を dummy 判定より前に計算する順序に変更し、
`start[j]=finish[j]=est` に修正。
対象: `RCPSP_Problem.cpp`（evaluate() 本体・computeStartTimes() フォールバック双方）,
`RCPSP_Problem_MaxShift.cpp`, `RCPSP_Problem_Splitting.cpp`, `RCPSP_Problem_Splitting_MaxShift.cpp`,
`RCPSP_Problem_Setup.cpp`（未使用ターゲットだが横断確認で発見・修正）。
`RCPSP_Problem_MaxShiftDur.cpp` / `RCPSP_Problem_MaxShiftSeg.cpp` / `RCPSP_Problem_Splitting_MaxShiftDur.cpp`
は既に `est` 版で実装済みだった。

## P2/P3（Splitting系）は原因①の対象外と判断
`RCPSP_Problem_Splitting.cpp` / `_Splitting_MaxShift.cpp` / `_Splitting_MaxShiftDur.cpp` の
P2/P3 配置ロジックを確認したが、**「ホライゾン末尾から est を無視して後退探索する」パターンは
存在しない**。P2/P3 は `[est, T)` を前方(t=est→T)にのみ走査し、置けなければ 1e9 で打ち切る
設計（分割実行が前提のため、そもそも「一箇所に連続配置できない」を後退探索で救う必要がない）。
よって原因①の修正は不要と判断し、原因②（ダミー0固定）のみ修正した。

## 修正③（想定外の副作用）: heap 破損 (STATUS_HEAP_CORRUPTION 0xC0000374)

### 症状
修正①適用後、j3033_1 RR050_RV1（MaxShift/MaxShiftDur）で `NSGAEncCpp.exe` /
`NSGAMaxShiftDurCpp.exe` が **無言でクラッシュ**（bash 上は exit code 127、
PowerShell `Start-Process` で確認すると `ExitCode=-1073740940` = `0xC0000374`
= `STATUS_HEAP_CORRUPTION`）。ログは "Loading instance... RV=1" の直後で完全に停止し、
エラーメッセージなし。イベントログにも Application Error は記録されなかった。

### 切り分け
- 単独ハーネス `MSVERIFY`（j3033_1, RR=0.5, RV=1, 100000 evals/戦略）は**再現せず**
  （exit code 0）。→ 単一条件・単一プロセスでは起きない。
- 実際にクラッシュする `NSGAEncCpp` は、同一プロセス内で **P1_SchedObj → P1_MaxShift** を
  連続実行するランナーで、P1_SchedObj が `combined_valid=0`（全個体 1e9）で完了した直後、
  P1_MaxShift の評価開始でクラッシュしていた。

### 根本原因（推定・完全特定はできず、対症療法で解消）
修正①により、**この条件では「ほぼ全ジョブが `[est, T-d]` に置けず 1e9 を返す」ケースが激増**した
（旧コードは `[0, T-d]` の広い範囲を後退探索するため、ほぼ必ず「先行制約を破ってでも」何かの
配置に成功し、1e9 早期 return はごく稀にしか発生しなかった）。この早期 return では
`solution->startTimes_` / `solution->execSlots_` が**更新されずに return**していたため、
「1度も正常評価されたことのない Solution（デフォルト構築で空）」と「前世代の古い（別サイズ・別内容の）
配置結果が残ったままの Solution」が大量に発生する状態になった。この状態が、population 全体が
本質的に縮退した状況（全個体が同一の 1e9,1e9 目的関数値）と組み合わさったときに、
NSGA-II 側の何らかの処理（Ranking／Crowding／many-to-one ログ集計等、正確な箇所は未特定）で
不整合なメモリアクセスを起こし heap を破損させたと推定される。

### 対処（採用した修正）
1e9 を返す直前に、その時点までの部分的な配置結果で `startTimes_` / `execSlots_` を
**必ず n 要素に確定させてから** return するよう変更（4ファイル共通）:
```cpp
if (t_mak < est) {
    solution->startTimes_ = startArr;          // 部分的でも必ずサイズ n で確定
    solution->execSlots_.assign(n, {});
    solution->setObjective(0, 1e9);
    solution->setObjective(1, 1e9);
    return;
}
```
適用後、j3033_1 RR050_RV1 / j3036_1 RR025_RV1（後述、同型のクラッシュが2件目として発覚）とも
`ExitCode=0` で完走することを確認した。

### 教訓
- **「実行不能を検出する頻度を変える」変更は、たとえロジック的に正しくても、
  下流コードが「まれにしか通らない分岐」に依存している場合に安定性リスクを生む**。
  今回は「1e9 早期 return 時に startTimes_/execSlots_ が未更新のまま」という
  “ほぼ無害だったはずの手抜き” が、発生頻度の変化によって heap 破損として顕在化した。
- 早期 return パスは、**normal path と同じくキャッシュ用フィールドを毎回確定させる**のが安全。
  「どうせ 1e9 で捨てられるから」という理由でスキップするのは危険（後段が
  「評価済みなら populated されているはず」という暗黙の前提を持ちうる）。
- **クラッシュの再現は最小構成では失敗することがある**。単一条件・単一プロセスの
  ハーネス（MSVERIFY）では再現せず、本番と同じ「複数条件・複数エンコーディングを
  同一プロセスで連続実行する」流れでのみ再現した。切り分け時は「本番と同じシーケンスを
  保ったまま条件を絞る」ことを優先すべきだった（今回は heap 破損の性質上、根本原因の
  完全特定より対症療法での解消を優先した）。
- Windows での原因不明のクラッシュは、まず `Start-Process -PassThru -Wait` で
  `$proc.ExitCode` を 16進表示し `STATUS_*` コードを特定するのが早い
  （git-bash 経由の `$?` は 127 等の不正確な値しか返らない）。

## 検証結果（4ケース、すべて違反0を確認）
`tools/verify_precedence.py`（新規作成。ダミーを含む全ペアを検査し、
「実ジョブ↔実ジョブ」「実ジョブ↔ダミー」「ダミー↔ダミー」を区別して集計。
`cost>=1e8`（実行不能ペナルティ）の解は「部分的にしか埋まっていない start 配列」を
持つため検査対象から除外する）で確認:

| ケース | 修正前 | 修正後 | フロント変化 |
|---|---|---|---|
| j3025_1 RR050_RV0 MaxShiftDur | 100/100解 違反 | **0件** | ms\* 240→254（正直な悪化） |
| j3033_1 RR050_RV1 MaxShift | 200/200解 違反 | **0件** | フロントが空に（MIPも Infeasible、一致） |
| j3036_1 RR025_RV1 MaxShiftDur | 200/200解 違反 | **0件** | フロントが空に（MIPも Infeasible、一致） |
| j3011_1 RR000_RV0 MaxShift（ダミーのみ） | 114/114解 違反 | **0件** | ms\* 55→54（誤差範囲、実質不変） |

j3025_1・j3033_1・j3036_1・j3011_1 の CORE系列（MaxShiftDur/P1_MaxShift/P1_SchedObj）全体
（cost<1e8 の解 8036件）で cost 不一致・先行制約違反ともに **0件**を確認
（`verify_cost_consistency.py --prefix-regex`）。

## 追記（2026-08-12）: 48インスタンス全体で再実行・再検証完了
`tools/run_48_comparison.sh`（NSGA のみ、MIP は `GAP_MIP_*.csv` 存在によりスキップ）で
全48インスタンス×6条件を再実行。所要時間: 試走(3件)493秒→全48件6146秒(≈102分)。
修正前の結果は `results/FUN_ENC_prefix029/` に退避済み、gap集計も
`analysis/gap48/gap_summary_prefix029.csv` に保存済み（修正前後比較の正本）。

**検証結果**:
- `tools/verify_precedence.py`（全48インスタンス、1152ファイル、MIP含む）:
  **先行制約違反 0件**（実ジョブ・ダミーとも、全カテゴリで 0/0）。
- `verify_cost_consistency.py` の cost>=1e8（実行不能ペナルティ）を除いた有効解 89,374件で
  **コスト不一致・先行制約違反ともに 0件**。
- 1e9 ペナルティ解は 14,400件で、これらは正しく「部分的な配置データ」を持つ想定内の挙動
  （`verify_precedence.py` 側で cost>=1e8 を検査対象外にして filter 済み）。

**gap分布への影響（修正前後比較、pooled across 6 conditions）**:

| 手法 | 修正前 平均/中央値/最大 | 修正後 平均/中央値/最大 |
|---|---|---|
| MaxShiftDur | 0.491% / 0.422% / 1.680% | 0.503% / 0.414% / 1.975% |
| MaxShift    | 0.563% / 0.495% / 3.066% | 0.533% / 0.457% / 2.803% |
| SchedObj    | 1.473% / 1.376% / 4.696% | 1.457% / 1.392% / 4.599% |

数値はほぼ不変（±0.03pt程度）。**[[028]] の「改善優先度は低い」という結論は変わらない**。
先行制約違反の混入は多くの行で軽微（フロント全体のごく一部の点のみ汚染）だったため、
バグ修正が gap の分布に与えた影響は小さかった。

**除外行の変化（重要な副次的発見）**: 修正前は42行除外（MIP_INFEASIBLE 36 +
NSGA_NO_FEASIBLE 6）だったが、修正後は **36行のみ**（MIP_INFEASIBLE のみ、NSGA_NO_FEASIBLE
は 0）。j3025_1・j3033_1 の RR050_RV0 で、修正前は NSGA が全個体1e9で前線が空になっていたが、
修正後は正常に実行可能解を見つけるようになった。**新規に空になった前線は0件**
（空になった36条件は全て修正前から既知の MIP 側 "Infeasible" と一致する、真の実行不能条件）。
→ est を破ってでも解を作る旧フォールバックが、皮肉にも「探索を阻害していた」ケースが
存在したことを示唆する（不正な配置に固執して他の探索余地を狭めていた可能性）。

## やらなかったこと・今後
- heap 破損の完全な根本原因（NSGA-II 側のどのデータ構造がどう不整合になったか）は
  未特定。対症療法（早期 return 時のキャッシュ確定）で解消したが、同種の「早期 return
  頻度が変わる変更」を今後行う際は、まず全個体 1e9 に近い縮退条件で単体テストする
  ことを推奨する。
- 関連: [[018]]（最適値を下回る目的値=実行不能の兆候）, [[022]]（本バグの最初の発見）,
  [[026]][[027]]（MIP ε-breakpoint、この検証で「MIP も Infeasible」と一致することの
  確認に使用）, [[028]]（48インスタンス比較の結論、本修正後も不変）。
