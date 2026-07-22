# 001: buildTimeVaryingCapacity の非決定論的シードによる資源違反

## 発生日
2026-05-21

## バグの概要
ガントチャートで資源制約違反が発生したように見えた。
実際には **最適化時の `capacity_t`** と **SCHED ファイル書き出し時の `capacity_t`** が別物になっていた。

## 根本原因

`buildTimeVaryingCapacity` 内の乱数生成器の初期化：

```cpp
// 修正前（バグあり）
static thread_local std::mt19937 capRng(std::random_device{}());
```

`random_device` はプロセス起動のたびに異なるシードを生成する。
`RCPSP_Problem` のインスタンスを 2 回生成すると（最適化用・出力用）、
それぞれ異なる `capacity_t` テーブルが作られる。

- 最適化: `prob1` (capacity_t = A) で解を評価 → start_times を取得
- 出力:   `prob2` (capacity_t = B) の制約でガントチャートを描画
- A ≠ B のため、A で合法な start_times が B では違反に見える

## 修正内容

シードをインスタンスパラメータ（filename + rr + rv）から決定論的に導出する。

```cpp
// RCPSP_Problem.cpp コンストラクタ内
size_t h = std::hash<std::string>{}(filename);
h ^= std::hash<double>{}(rr)       + 0x9e3779b9u + (h << 6) + (h >> 2);
h ^= std::hash<int>{}(rv ? 1 : 0)  + 0x9e3779b9u + (h << 6) + (h >> 2);
buildTimeVaryingCapacity(rr, rv, static_cast<uint32_t>(h));

// buildTimeVaryingCapacity のシグネチャ変更
void RCPSP_Problem::buildTimeVaryingCapacity(double rr, bool rv, uint32_t seed) {
    std::mt19937 capRng(seed);  // 決定論的
    ...
}
```

## 教訓

- **同一パラメータの問題インスタンスを複数生成する場合、確率的な内部状態は必ず決定論的にする**
- `random_device` をクラスの初期化に使うのは、シミュレーション結果の再現性を壊す
- 特に「最適化用」と「出力用」で別インスタンスを作るパターンは注意が必要
- `capacity_t` のように「評価関数の制約テーブル」は、どのインスタンスも同一値であることを保証すること

## 確認方法
ガントチャート（`analyze_diagnosis.py` 出力の `gantt_*.png`）で資源使用量が容量上限を超えていないかチェック。
