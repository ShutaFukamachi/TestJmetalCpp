# NSGA-II コスト探索バイアスの修正

## 症状
RV=1（バケーションあり）条件でMIPソルバーより大幅に劣る Pareto フロント。
コスト軸（resource cost）に向かう解が生成されにくい。

## 原因（MaxShift / MaxShiftDur 共通）

### 初期集団バイアス
- MaxShift: 40%の個体が `max_shift ∈ Uniform[0, T/100]` という超小さい窓→ほぼEST配置と同等
- MaxShiftDur: 40%の個体が `rho ∈ Uniform[0, R/25=40]` → 比率空間の4%しか探索しない
- RV=1 でホライゾンが3.5倍以上になるとこの問題がさらに悪化

### 変異ゼロリセット過剰
- zeroResetProb = 0.70 → 変異のたびに70%の確率で max_shift/rho を0に戻す
- 集団全体がメイクスパン最小方向に引き戻され続ける

## 修正内容

### `src/problems/RCPSP_Problem_MaxShift.cpp` - createRandomTopoSolution()
```
変更前:  30% zeros, 40% Uniform[0, T/100], 30% Uniform[0, halfT]
変更後:  20% zeros, 40% Uniform[0, T/8],   40% Uniform[0, halfT]
```

### `src/problems/RCPSP_Problem_MaxShiftDur.cpp` - createRandomTopoSolution()
```
変更前:  30% zeros, 40% Uniform[0, R/25],  30% Uniform[0, R]
変更後:  20% zeros, 40% Uniform[0, R/8],   40% Uniform[0, R]
```

### `src/operators/mutation/MaxShiftMutation.h`
```
変更前:  zeroResetProb = 0.70
変更後:  zeroResetProb = 0.40
```

### `src/operators/mutation/MaxShiftDurMutation.h`
```
変更前:  zeroResetProb = 0.70
変更後:  zeroResetProb = 0.40
```

## 確認事項
- MaxShiftDur の rho キーは比率空間 [0, R=1000] で定義されるためホライゾン膨張の影響を直接受けない
  → ただし R/25 (4%) は極端に小さいため R/8 (12.5%) に変更した
- ビルド後に `NSGAEncCpp` ターゲットを再実行して RV=1 の Pareto フロントが改善されたことを確認すること
