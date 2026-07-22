# 教訓: Variable** から std::vector<int> への移行（2026-06-17）

## 問題
旧 jmetal C++ ライブラリの `Variable**` パターンがメモリリークの温床だった。
- `Variable` は抽象クラス、`Int` はその派生クラス
- `Solution::variable_` = `Variable**` → 手動 new/delete が必要
- `SolutionSet::clear()`, `remove()`, `replace()` が delete を忘れていた
- `NSGAII::execute()` の `offs[]` 配列が `delete[]` されていなかった（一度 // でコメントアウトされていた）

## 解決策
`Variable**` を `std::vector<int>` に置き換え、クラス自体を廃止:

### 旧API（廃止）
```cpp
Variable **vars = solution->getDecisionVariables();
int val = (int)vars[i]->getValue();
vars[i]->setValue((double)newVal);
```

### 新API（現在）
```cpp
auto &vars = solution->getVars();  // std::vector<int>&
int val = vars[i];                  // 直接アクセス
vars[i] = newVal;
```

## 廃止されたクラス（スタブとして残存）
- `Variable` / `Int` → `src/core/Variable.h`, `src/encodings/variable/Int.h`
- `SolutionType` / `IntSolutionType` → `src/core/SolutionType.h`, `src/encodings/solutionType/IntSolutionType.h`
- これらは空スタブになっているので `#include` しても問題ないが、中身は何もない

## Problem クラスの変更
- 旧: `double *lowerLimit_` / `double *upperLimit_` (raw pointer)
- 新: `std::vector<double> lowerLimit_` / `std::vector<double> upperLimit_`
- `SolutionType *solutionType_` も廃止
- RCPSP_Problem.cpp の constructor で `lowerLimit_.assign(...)` を使う

## SolutionSet のバグ修正
以下3か所は必ず delete を行うよう修正済み:
```cpp
void SolutionSet::clear()   { for(auto* s : solutionsList_) delete s; solutionsList_.clear(); }
void SolutionSet::remove(i) { delete solutionsList_[i]; solutionsList_.erase(...); }
void SolutionSet::replace() { delete solutionsList_[pos]; solutionsList_[pos] = solution; }
```

## 注意: rng.seed() を削除してはいけない
`RCPSP_Problem::RCPSP_Problem()` 内の:
```cpp
rng.seed(12345u + static_cast<unsigned>(strategy_) * 1000003u);
```
これは並列実行時の再現性のため必須。削除すると各 strategy 間で RNG 状態が共有されて再現性が失われる。
