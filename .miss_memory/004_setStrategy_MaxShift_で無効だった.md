# 004: RCPSP_Problem_MaxShift::setStrategy が事実上 no-op だった

## 発生日
2026-05-20

## バグの概要

`setStrategy(s)` を呼んでも `RCPSP_Problem_MaxShift` の evaluate() が使う `halfT`（max_shift 上限）が変わらず、
strategy による上限切り替えが機能していなかった。

## 根本原因

基底クラスの `setStrategy` は `strategy_` フィールドを更新するだけ。
しかし `RCPSP_Problem_MaxShift::evaluate()` 内では：

```cpp
// 修正前（バグあり）
const int halfT = std::max(1, T / 4);  // strategy_ を無視してハードコード
```

さらに変数の上限（`upperLimit_[n..2n-1]`）も strategy 変更後に更新されないため、
NSGA-II の初期解生成や変異も T/4 固定のまま動いていた。

## 修正内容

1. `getEffectiveHalfT()` メソッドを追加（strategy に応じた上限を返す）：
   ```cpp
   int RCPSP_Problem_MaxShift::getEffectiveHalfT() const {
       int T = getHorizon();
       switch (strategy_) {
           case 1: return 0;
           case 2: return std::max(1, T / 8);
           case 3: return std::max(1, T / 4);
           case 4: return std::max(1, T / 2);
           default: return std::max(1, T / 4);
       }
   }
   ```

2. `setStrategy` を override し、`upperLimit_` も更新：
   ```cpp
   void RCPSP_Problem_MaxShift::setStrategy(int s) {
       strategy_ = s;
       int halfT = getEffectiveHalfT();
       int n = getNumJobs();
       for (int i = n; i < 2 * n; ++i)
           upperLimit_[i] = (double)halfT;
   }
   ```

3. `evaluate()`, `createCostExtremeSolution()`, `createRandomTopoSolution()` で
   ハードコードの `T/4` を `getEffectiveHalfT()` に変更。

4. `MaxShiftMutation` でも同様に `getEffectiveHalfT()` を使用。

## 教訓

- **派生クラスで基底クラスのメソッドをオーバーライドする際、基底の動作だけでは不十分な副作用（upperLimit_ の更新など）を必ず派生クラス側で実装する**
- strategy を切り替えるメソッドがある場合、それに依存する他のすべての状態（変数の上限、ハードコード値）を連動して更新すること
- ハードコードの数値（T/4 など）を使うより、それを計算するメソッドを 1 か所に集約して参照するほうが保守しやすい
