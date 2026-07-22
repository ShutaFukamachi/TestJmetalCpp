# 002: setStrategy に override を付けたがコンパイルエラー

## 発生日
2026-05-20

## バグの概要

```
error C3668: 'RCPSP_Problem_MaxShift::setStrategy':
オーバーライド指定子 'override' を持つメソッドは、
基底クラス メソッドをオーバーライドしませんでした
```

## 根本原因

派生クラス `RCPSP_Problem_MaxShift` で `setStrategy` を `override` 付きで宣言したが、
基底クラス `RCPSP_Problem` の `setStrategy` が `virtual` でなかった。

```cpp
// RCPSP_Problem.h（修正前・バグあり）
void setStrategy(int s) { strategy_ = s; }  // virtual なし

// RCPSP_Problem_MaxShift.h（override を付けていた）
void setStrategy(int s) override;  // → コンパイルエラー
```

## 修正内容

基底クラスの `setStrategy` に `virtual` を追加する。

```cpp
// RCPSP_Problem.h（修正後）
virtual void setStrategy(int s) { strategy_ = s; }
```

## 教訓

- **派生クラスでオーバーライドしたいメソッドは、基底クラスで必ず `virtual` にする**
- `override` キーワードはコンパイラが「本当にオーバーライドできているか」を検証してくれる有用なキーワード。`virtual` を付け忘れていたら即座に検出できる
- 基底クラスに手を加えられる場合は `virtual` + `override` の組み合わせを使う
- 基底クラスが外部ライブラリなど変更不可の場合は `override` を外してシャドーイングにする（ただし危険）
