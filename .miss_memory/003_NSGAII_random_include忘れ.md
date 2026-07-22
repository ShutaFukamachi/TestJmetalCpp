# 003: NSGAII.cpp に #include <random> を追加し忘れ

## 発生日
2026-05-20

## バグの概要

```
error C2039: 'mt19937': 'std' のメンバーではありません
error C2039: 'random_device': 'std' のメンバーではありません
```

## 根本原因

`NSGAII.cpp` に `std::mt19937` と `std::uniform_real_distribution` を使うコードを追加したが、
`#include <random>` を追加し忘れた。

```cpp
// NSGAII.cpp（修正前・バグあり）
// #include <random> がない状態で以下を使用
static thread_local std::mt19937 rng_ms{std::random_device{}()};
std::uniform_real_distribution<> d01_ms(0.0, 1.0);
```

## 修正内容

```cpp
#include <algorithm>
#include <random>    // ← 追加
#include <vector>
```

## 教訓

- **新しいヘッダが必要な型・関数を使う際は、必ずインクルードを確認する**
- `std::mt19937`, `std::uniform_real_distribution`, `std::random_device` は `<random>` が必要
- `std::sort`, `std::min`, `std::max` は `<algorithm>` が必要
- `std::numeric_limits` は `<limits>` が必要
- コードを書いた直後にビルドして確認する習慣をつける
