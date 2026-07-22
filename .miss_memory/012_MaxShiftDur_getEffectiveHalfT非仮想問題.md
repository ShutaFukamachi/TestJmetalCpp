# 012: RCPSP_Problem_MaxShift::getEffectiveHalfT() が virtual でない問題

## 発見日
2026-06-17

## 問題
`RCPSP_Problem_MaxShiftDur` を `RCPSP_Problem_MaxShift` の派生クラスとして実装する際、
`MaxShiftMutation` のコンストラクタが `prob->getEffectiveHalfT()` を呼んで
`halfT_` を初期化するが、この関数が **virtual でない** ため、
`RCPSP_Problem_MaxShiftDur*` を `RCPSP_Problem_MaxShift*` として渡しても
基底クラスの実装（T/4, T/8 など）が呼ばれてしまう。

```cpp
// MaxShiftMutation.h のコンストラクタ
MaxShiftMutation(double p, RCPSP_Problem_MaxShift *prob) : probability(p) {
    halfT_ = prob->getEffectiveHalfT();  // 非仮想 → 基底クラス版が呼ばれる
}
```

`RCPSP_Problem_MaxShiftDur` では `vars[n..2n-1]` のキーが [0, R=1000] の
比率キーなので、`MaxShiftMutation` は上限 R ではなく T/4 などで
再サンプリングしてしまい、比率空間を正しく探索できない。

## 解決策
`MaxShiftMutation` は変更せず、専用の `MaxShiftDurMutation` を新規作成する。
`MaxShiftDurMutation` は `R = RCPSP_Problem_MaxShiftDur::R` (=1000) を
直接 constexpr 参照して上限を固定する。

```cpp
// MaxShiftDurMutation.cpp
static constexpr int R = RCPSP_Problem_MaxShiftDur::R;
std::uniform_int_distribution<int> rho_dist(0, R);
```

## 教訓
- 派生クラスで挙動を変えたいメソッドは **必ず基底クラスで virtual にする**
- 既存ファイルを変更できない制約がある場合は、専用オペレータクラスを新規作成して対応する
- `getEffectiveHalfT()` のような「設定を返す」メソッドは virtual にして
  派生クラスで override できるようにしておくべきだった
- `virtual` を付け忘れたまま新しいエンコーディングを追加すると、
  変異オペレータが誤った範囲でサンプリングするバグが起きる
