# 開発ルール

## 必須プロトコル（コード変更前に必ず実行）

- コード変更や実装を開始する前に、必ず `.miss_memory/` フォルダ内のすべての教訓ファイルに目を通し、過去のバグやミスを再発させないようにすること。
- 新たなバグを修正した際や、重要な実装上の注意点を発見した際は、必ず `.miss_memory/` 内に新しいファイルを作成して教訓を記録すること。

## プロジェクト概要

RCPSP（資源制約付きプロジェクトスケジューリング問題）の多目的最適化。NSGA-II による 2 種類のエンコーディング比較。

- **SchedObj エンコーディング**: `vars[0..n-1]`=活動リスト, `vars[n..2n-1]`=0/1（0=EST, 1=コスト探索）
- **MaxShift エンコーディング**: `vars[0..n-1]`=活動リスト, `vars[n..2n-1]`=max_shift 値 [0, upper]
- エンコーディングはどちらも **P1（作業分割なし、Serial Scheduling Scheme）**
- P2/P3 作業分割は `RCPSP_Problem_Splitting` クラス専用

## ビルドターゲット

- `NSGAEncCpp` : 2 エンコーディング比較ランナー（`main_NSGAII_RCPSP_EncodingComparison.cpp`）
- `TestJmetalCpp` : その他テスト（`main.cpp`）

## 主要ファイル

| ファイル | 役割 |
|---|---|
| `src/problems/RCPSP_Problem.cpp/h` | 基底問題クラス、SchedObj 評価、capacity_t 生成 |
| `src/problems/RCPSP_Problem_MaxShift.cpp/h` | MaxShift エンコーディング評価 |
| `src/problems/MaxShiftSensitivity.cpp/h` | max_shift 感度分析クラス |
| `src/metaheuristics/nsgaII/NSGAII.cpp` | NSGA-II 本体（エリート保存・交叉バイアス追加済み） |
| `main_NSGAII_RCPSP_EncodingComparison.cpp` | 比較ランナー本体（NSGAEncCpp ターゲット） |
| `main.cpp` | TestJmetalCpp ターゲット用（別用途） |
