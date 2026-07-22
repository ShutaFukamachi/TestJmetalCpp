# 教訓: FrontLocalSearch で capacityAtTime が protected のため再現が必要

## 問題
`RCPSP_Problem::capacityAtTime(int k, int t)` は `protected` であるため、
外部モジュール `FrontLocalSearch` からは直接呼び出せない。

## 解決
`getCapacityT()` と `getCapacity()` は public なので、
`canPlaceWithout()` 内で同じロジックを再現した:
```cpp
if (!cap_t.empty() && k < cap_t.size() && tau < cap_t[k].size())
    capAtTau = cap_t[k][tau];
else
    capAtTau = cap[k];
```

## 注意点
- `capacityAtTime` のロジックが将来変更された場合、FrontLocalSearch 側も同期が必要。
- 局所探索の結果は必ず `verifySchedule` で検証すること（ロジック乖離の早期検出）。
- oneOpt で usage テーブルから j を除去→再配置する際、
  除去は `usage[k][tau] -= demand[j][k]` で行う。負にならないことは
  元スケジュールが正当であることが前提（evaluate 通過済み）。
