# MIP ε-制約法のメイクスパン偏り問題

## 症状
`solvePareto()` で生成した Pareto フロントがメイクスパン最小端に偏り、
コスト最小端が欠落していた。

## 原因
1. `makespanSlack=40` による固定上限制約（探索範囲 = C*_ms + 40 まで）
2. `no_improve_streak >= 5` による早期打ち切り（コストが連続5回改善しないと停止）
   → 時変コストのプラトー区間（改善なしが続く）を越えた先に低コスト解が存在しても発見できない

## 修正
`RCPSP_MIP_Solver::solvePareto()` を以下に変更:
1. まず `solveCost(horizon)` でコスト最小解を取得 → `ms_at_min_cost` を確定
2. ε 探索上限を `max(ms_at_min_cost, C*_ms + makespanSlack)` に設定
3. 早期打ち切りルール（5回連続改善なし）を廃止

## 確認事項
- j3011_1 RR=0.00 RV=off: 旧 max_ms=86, 新 max_ms=158 → NSGA-II と同等範囲を網羅
- MIP 最小コスト 324,817 < MaxShiftDur 326,349 → 厳密解の優位性を確認
