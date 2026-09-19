# 022: Serial SGS デコーダの末尾フォールバックが先行制約違反の phantom を生む

## 発見日
2026-07-23（Parallel SGS 実装の A/B ハーネスで、フロント全解を検証して発覚）

## 症状
- `PSGS`（main_ParallelSGS_AB.cpp）で **Serial 条件のフロントに稀に先行制約違反**が出る
  （j309_1 RR050, 8000eval/戦略で 1試行あたり 0〜9 解、prec 違反 1〜3件、res=0）。
- 特に危険な現れ方: **ms* が真の最適 125 を「達成」したように見えるが実は違反付き**
  ＝ .miss_memory/018 と同型の phantom（「最適値を下回る/ちょうど到達＝実行不能の兆候」）。
- **Parallel SGS 条件では一切出ない**（clean）。Serial 分岐固有。

## 根本原因（RCPSP_Problem_MaxShift.cpp:179-188, evaluate の serial ブロック）
```cpp
int t_mak = est;
while (t_mak < T && !canPlace(j, t_mak)) ++t_mak;
if (t_mak >= T) {                       // [est, T) に置けない
    t_mak = std::max(0, T - d);
    while (t_mak >= 0 && !canPlace(j, t_mak)) --t_mak;  // ★ 後方探索
    ...
}
```
時変容量(RR>0)で `[est, T)` に連続 d スロットが取れないジョブが稀に生じると、末尾から
**後方探索して任意の実行可能スロットに置く**。この位置は `est` より前になり得るため、
`start[j] < finish[pred]` となり先行制約を破る。makespan も不正に小さくなる。

## 影響範囲
- Serial デコーダを使う**全ランナー**（NSGAEncCpp の MaxShift 側、A3AB、E17 の GA 部）で
  同じ phantom が混入し得る。
- ただし A3AB は **ms* 1解しか feasibility 検証していなかった**ため、非 ms* の phantom を
  見逃していた。過去の「139 が床」等の結論は ms* 解を個別に feasibility 検証済みなので不変。

## 対処（2026-07-23 時点）
- **ハーネス側で回避**: PSGS はフロント上の全解を checkFeasibility し、**実行不能解を
  非支配計算の前に除外**してから FUN 出力・ms* 算出する（serial/parallel 両方に適用＝公平）。
  → serial が phantom で不当に良く見えるのを防ぐ。
- **デコーダ本体は未修正**（今回のタスク=生成スキーム A/B の範囲外）。本修正するなら選択肢:
  1. フォールバックを「est 以降のみ後方探索」に限定（est 未満には絶対置かない）。
  2. `[est, T)` に置けなければ makespan を伸ばす（T を動的拡張）か、その解を 1e9 で捨てる
     （Parallel 分岐と同じ扱い）。
- Parallel SGS 分岐は「窓内に置けなければ次の決定点へ持ち越し／最後まで置けなければ 1e9」
  なので phantom を出さない設計になっている。

## 教訓
- **手法比較のフロントは ms* だけでなく全解を feasibility 検証する**。端点1解の検証では
  非端点の phantom を見逃す。
- 「最適値ちょうど/以下」を見たら、まず実行可能性を疑う（018・019 と同じ反射）。
- フォールバック配置は「制約を保つ範囲でのみ」行う。任意スロット後方探索は先行制約を壊す。
- 関連: [[018]]（FBI backward pass の phantom）, [[019]]（真の最適125の確定）。
