# 010: startTimesCache_ は無用かつ有害 → 削除

## 発生日
2026-06-10

## バグの概要
`RCPSP_Problem` が `mutable std::map<Solution*, std::vector<int>> startTimesCache_` を持ち、
`evaluate()` のたびにキャッシュエントリを追加していた。
1ストラテジー (50,000 eval) で最大 50,000 エントリー（j120 で ~27 MB）が蓄積し、
かつキャッシュヒットが実際には一度も起きないという状態だった。

## 根本原因

### キャッシュが役に立たない理由
NSGA-II では評価した解をそのまま population に保持するのではなく、
交叉後のオフスプリングは `new Solution(c1)` でコピーしてから保持する。
コピーはポインタが変わるため `startTimesCache_.find(copy)` は必ずミスヒット。

実際のアクセスパス:
1. `evaluate(c1)` → `startTimesCache_[c1] = start` (ヒット候補登録)
2. `offspringPopulation->add(new Solution(c1))` → コピーが保持される
3. `delete offs[0]` (= `delete c1`) → キャッシュのキーがダングリングポインタ化
4. コピーのポインタでキャッシュを引くと必ずミスヒット

### 危険性: アドレス再利用による誤ヒット
削除済みアドレスに新解が `new` で割り当てられると、
古いエントリがヒットして誤った開始時刻が返る正確性バグが発生しうる。

### 代替手段は既に存在していた
`solution->startTimes_` フィールドがコピーコンストラクタで引き継がれるため、
キャッシュなしでも正確な開始時刻を返せる。
`computeStartTimes()` の2番目のフォールバックがこれを担当している。

## 修正内容
- `RCPSP_Problem.h`: `startTimesCache_` フィールド削除、`clearStartTimesCache()` を空 no-op に変更、`#include <map>` 削除
- `RCPSP_Problem.cpp`: `startTimesCache_[solution] = start` 行を削除、`computeStartTimes()` からキャッシュ参照ブロックを削除
- 呼び出し元 (main.cpp 等) の `clearStartTimesCache()` 呼び出しはコンパイル互換のため残置（no-op）

## 教訓
- `Solution*` をキャッシュキーにするのは NSGA-II のコピー主体の設計と相性が悪い
- `solution->startTimes_` はコピーコンストラクタで引き継がれるため、キャッシュは不要
- 50,000 eval × 122 jobs で ~27 MB を消費するが、キャッシュは一度もヒットしない
- 新たにポインタキーのキャッシュを導入する際は「同じポインタが再利用されるか」を必ず確認する
