# RV=on で NSGA-II が全解 1e+09 になるバグ（vacDay 抽選の誤り）

## 症状
RV=1 の一部条件（特に RR>0 または資源制約がタイトな問題例）で、
NSGA-II の全評価が 1e+09（実行不可能）を返す。

## 原因
`RCPSP_Problem::buildTimeVaryingCapacity()` 内の Step 7（休暇日設定）で、
`vacDay` の抽選が **資源ループ（k ループ）の内側** にあった。

```cpp
// 修正前（バグあり）
for (int k = 0; k < nRes; ++k) {
    // ...
    if (rv) {
        int vacDay = (u01(capRng) < 0.5) ? 0 : 7;  // ← 毎資源ごとに独立抽選
        for (int t = 0; t < T; ++t) {
            if (t % 14 == vacDay) capacity_t[k][t] = 0;
        }
    }
}
```

nRes=4 のとき、各資源が独立に vacDay ∈ {0, 7} を選ぶ。
vacDay=0 の資源と vacDay=7 の資源が混在すると、
**両方の制約を同時に満たさなければならないジョブにとって実質的な休暇頻度が 7 日ごとになる**。

- 最大連続使用可能スロット: 6（vacDay={0,7} が混在の場合）
- 活動の duration が 7 以上あると配置不可能な活動が存在し、スケジュール不能 → 1e+09

j3026_1、j309_1 など duration=7,8,9,10 の活動を持つ問題例で顕在化。

## 修正
`vacDay` の抽選を **k ループの外に移動** し、全資源共通で 1 回だけ決定する。

```cpp
// 修正後
int vacDay = 0;
if (rv) {
    std::uniform_real_distribution<> u01(0.0, 1.0);
    vacDay = (u01(capRng) < 0.5) ? 0 : 7;  // 全資源で共通の休暇日
}
for (int k = 0; k < nRes; ++k) {
    // ...
    if (rv) {
        for (int t = 0; t < T; ++t) {
            if (t % 14 == vacDay) capacity_t[k][t] = 0;
        }
    }
}
```

## 影響
- **この変更により capacity_t の生成結果が変わる**（乱数消費パターンが変わるため）
- 既存の MIP 結果も capacity_t に依存するので、同条件の比較には MIP も再実行が必要
- ただし NSGA-II と MIP は同じ `RCPSP_Problem` コンストラクタを使っているため、
  修正後はどちらも一貫した capacity_t を使用する

## 確認事項
- 修正後ビルド → NSGAEncCpp / NSGAMaxShiftDurCpp 再実行
- j3026_1 RR000_RV1 で有効解が得られることを確認
- MIP も再実行して新しい capacity_t に対応した厳密解を取得
