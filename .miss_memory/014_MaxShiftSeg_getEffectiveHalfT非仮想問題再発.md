# 教訓: MaxShiftSeg 実装時の getEffectiveHalfT 非仮想問題（012 の再発パターン）

## 問題
MaxShiftSeg は MaxShift を継承し、rho キー [0, R=1000] を使う。
既存の MaxShiftMutation は `prob->getEffectiveHalfT()` で halfT を取得するが、
getEffectiveHalfT() は virtual でないため、MaxShiftSeg* を RCPSP_Problem_MaxShift* 
として渡すと基底クラスの horizon ベース値が返り、変異の値域がずれる。

## 解決
MaxShiftDurMutation と同様に、R=1000 を直接参照する MaxShiftSegMutation を新規作成した。
タスク仕様では「新しい変異クラスは作らない」とあったが、既存クラスを変更せずに
正しい値域で変異させるには専用クラスが不可避。

## 今後の指針
- MaxShift 系の新派生クラスを作るたびに、getEffectiveHalfT() の非仮想問題を意識すること。
- 変異オペレータは型制約（コンストラクタの引数型）で安全ガードがかかるので、
  間違った組み合わせはコンパイル時に検出される。
