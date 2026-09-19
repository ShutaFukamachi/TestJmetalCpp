# 025: コスト表を flat 正本 costs/costs_<prefix>.csv に一本化＋ランダム生成フォールバック禁止

## 発生日
2026-07-28（欠陥2＝系列間コスト表ドリフトの根治タスク）

## 症状（欠陥2の再確認）
MIP/MaxShift/SchedObj と MaxShiftDur が**別々のコスト表**で目的値を計算しており、
コスト比較が無効だった（MaxShiftDur が MIP より安い＝厳密解に勝つ、等の矛盾）。

## 根本原因（2つ）
1. **正本パスの取り違え**: `RCPSP_Problem` コンストラクタが COST_CSV_PATH を
   **ネスト** `costs/<prefix>/costs_<prefix>.csv` に設定していた。しかし真の正本は
   **フラット** `costs/costs_<prefix>.csv`。両者は**内容が異なる**（全5インスタンスで DIFFER）。
   - 実測確認: MaxShiftDur の SCHED を再計算すると **flat に 105/105 一致・nested に 0/105**。
     → MaxShiftDur は flat で生成されていた。MIP/MaxShift/SchedObj は nested(誤表)で生成されていた。
2. **黙示のランダム生成フォールバック**: `generateCostSeries()` は正本ロード失敗時に
   **ランダム表を生成して COST_CSV_PATH へ上書き**していた。これが「系列ごとに別表」を
   量産する温床（nested の誤表はこれが作った）。

## 修正（src/problems/RCPSP_Problem.cpp）
1. コンストラクタの COST_CSV_PATH を **flat** `costs/costs_<prefix>.csv` に変更。
2. `generateCostSeries()` を「正本ロードのみ・失敗時は**エラーで停止（throw std::runtime_error）**」に変更。
   ランダム生成・正本上書きを**完全撤廃**（`<stdexcept>` 追加、`writeCostTableToCSV(COST_CSV_PATH)` 呼び出し削除）。
   正本不在なら `cost table not found: <path>` を stderr に出して abort。

## 検証（実測・PASS）
- 再生成 MIP j3011_1 RR000_RV0: ms=55 → cost=**375,241**（旧誤表では 417,518）。
  MaxShiftDur=378,787 より**安く MIP が支配**（受入基準#3）。SCHED 再計算 **54/54 一致**。
- 正本 CSV・MaxShiftDur 結果は**バイト不変**（mtime 変化なし、上書きログ 0）＝受入基準#4。
- 検証ツール `tools/verify_cost_consistency.py`: MaxShiftDur は j3011_1/j3017_1 とも正本一致
  （105/105・118/118）→ **温存で安全**（安全弁は不要）。MaxShift/SchedObj は 0/N ドリフト＝要再生成。

## ツール・運用（新規）
- `tools/verify_cost_consistency.py`: 全 SCHED_* の記録コストを正本で再計算し照合＋先行制約検証。
  CORE 系列(MIP/MaxShift/SchedObj/MaxShiftDur)の不一致のみ exit ゲート、実験系列(P2/P3/LS/NF/Seg)は info。
- `tools/cost_recompute.py`: 単一 SCHED×CSV の照合（正本特定用）。
- `tools/regenerate_canonical.sh`: MIP+NSGA 全5再生成→ファイル整理(results/FUN_ENC/<prefix>/へ移動、
  MaxShiftDur は NSGAEncCpp が生成しないので温存)→検証→図。**MIP は数時間規模**。
- 新ターゲット MIPVERIFY/MSVERIFY（[[024]]）は capacity_t 実行可能性検証に流用可。

## 教訓
- **グローバル状態（コスト表）に依存する手法比較は、全系列が同一表を使ったことを毎回自動検証する**
  （[[017]] の「常設スクリプト化すべき」を実装した）。SCHED の開始時刻＋正本 CSV で再計算照合。
- **「無ければ黙って生成」フォールバックは比較実験の毒**。不在は明確なエラーで止める（暗黙生成禁止）。
- 正本の**場所**は実測で確定する（flat vs nested のようにヘッダ同一でも中身が違う罠）。
- NSGAEncCpp は cwd 直下に書くので results/FUN_ENC/<prefix>/ へ移動が要る（図の参照先）。
- 関連: [[016]][[017]][[024]]。
