# 018: FBI backward pass の順序バグで実行不能スケジュール（makespanが最適を下回る虚数）

## 発生日
2026-07-22

## 症状
B5 FBI（Forward-Backward Improvement）を MaxShift の evaluate() に統合したところ、
j309_1 RR050 で ms*=117（A1+FBI）や 125 という **MIP 最適(125) 以下** の makespan が出た。
「FBI が MIP に勝った」ように見えたが、実際は **precedence 違反 3 件を含む実行不能解**だった。

## 根本原因
FBI の backward pass（右詰め）で、処理順序を **finish 時刻の降順**にしていた。
「後続を必ず先に配置する（＝reverse-topological）」ことを finish 降順で代用していたが、
**複数の先行を持つジョブ**（例: job 20 ← 12/18/19）では、strategy ごとに max_shift が
異なると finish 順が topological 順と一致せず、先行ジョブが後続より先に処理され、
後続の位置制約が適用されないまま先行が右詰めされて `start[pred] > start[succ]` が発生した。

backward の順序:
```cpp
// バグ: finish 降順を主キー（topological 保証がない）
if (finIn[a] != finIn[b]) return finIn[a] > finIn[b];
return topoRank[a] > topoRank[b];
```

## 修正
**reverse-topological（topoRank 降順）を主キー**にし、後続が必ず先に配置されるよう保証。
同一 topoRank 内は finish 降順で右詰め品質を確保:
```cpp
if (topoRank[a] != topoRank[b]) return topoRank[a] > topoRank[b];
return finIn[a] > finIn[b];
```
（topoRank は Kahn で算出。pred の rank < succ の rank が保証される）
修正後は全試行 prec=0 res=0、FBI の makespan は 117/125 の虚数から **実行可能な 139** に是正。

## 教訓
- **「最適値を下回る目的値」は実行可能性バグの最有力サイン**（016/017 と同じ轍）。
  手法を評価する前に必ず SCHED/開始時刻から先行・資源制約を検証すること。
- justification 系（右詰め/左詰め）の処理順序は **必ず topological / reverse-topological を
  主キー**にする。finish/start 時刻だけの順序は、時変容量やコスト探索で順序が崩れると
  precedence を破る。
- A/B ハーネス（main_A3_AB.cpp）に checkFeasibility を常設し、ms* 解の prec/res 違反を毎回出力
  するようにした。今後の手法追加でも実行可能性を自動チェックすること。
