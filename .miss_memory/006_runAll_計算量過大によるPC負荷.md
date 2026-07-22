# 006: EncodingComparisonRunner_All::runAll() の計算量過大による PC クラッシュ

## 発生日
2026-05-23

## 症状
- main.cpp を実行すると PC が重くなり CLion が落ちる
- 翌朝 IDE を開くと "IDE error occurred" が表示される

## 原因
`EncodingComparisonRunner_All::runAll()` は以下の組み合わせで NSGA-II を実行する:

| 要素 | 数 |
|---|---|
| 条件（RR × RV） | 8 |
| スキーム（P1/P2/P3） | 3 |
| エンコーディング | 2 |
| ストラテジー数 | 4 |
| **NSGA-II 実行回数** | **192 回** |

`evalsPerStrategy=100000` の場合、総評価回数 = **1,920 万回**。
数時間〜十数時間 CPU を使い続け、OS・IDE を不安定にする。

加えて `NSGAII.cpp` の `genLog.flush()` が毎世代呼ばれており、
約 19 万回の同期 I/O が発生して負荷をさらに増大させていた。

## 修正内容
- `evalsPerStrategy`: 100,000 → 10,000（デバッグ時）
- `numStrategiesSchedObj/MaxShift`: 4 → 2（デバッグ時）
- `genLog.flush()`: 毎世代 → 50 世代ごと（`NSGAII.cpp`）

## 教訓・注意点
1. **本番実行前に必ずデバッグ規模（evals=10000, strategies=2）で動作確認すること**
2. **本番実行は CLion のデバッガからではなく、Release ビルドをターミナルから実行すること**
   - ターミナルからなら `Ctrl+C` でプロセスを即座に停止できる
   - CLion から実行すると IDE ごと固まるリスクがある
3. `runAll()` の条件数を絞りたい場合は `conditions` ベクタの一部をコメントアウトする
4. `evalsPerStrategy` の目安:
   - 動作確認: 5,000〜10,000
   - 中間評価: 30,000〜50,000
   - 本番: 100,000
