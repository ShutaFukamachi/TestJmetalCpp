# 時間変動コスト・時間変動リソース容量および作業分割を考慮したRCPSPに対するNSGA-IIの改良

## 概要

本研究では，**時間変動コスト (Time-Varying Cost) および時間変動容量 (Time-Varying Capacity) を伴う資源制約付きプロジェクトスケジューリング問題 (RCPSP)** を多目的最適化問題として定式化し，NSGA-II による求解を行う．

---

## 問題モデル

### 基本設定

- **ジョブ**: $n$ 個のジョブ $j = 0, 1, \ldots, n-1$（ダミー始端・終端含む）
- **資源**: $K$ 種類の再生可能資源
- **制約**:
  - 先行制約: ジョブ $j$ は直接先行ジョブが全て完了してから開始
  - 資源制約: 各時刻 $t$ において使用量が容量を超えない

### 目的関数

| 目的 | 内容 |
|------|------|
| $f_1$: メイクスパン最小化 | $\min \max_j \text{finish}(j)$ |
| $f_2$: 総コスト最小化 | $\min \sum_j \sum_t c_{k}(t) \cdot r_{jk} \cdot x_{jt}$ |

### 時間変動コスト

資源 $k$ の時刻 $t$ におけるコスト $c_k(t)$ は以下で生成される：

$$c_k(t) = \alpha_k + \beta_k t + \gamma_t + \varepsilon_t$$

- $\alpha_k$：基本コスト（資源ごとにランダム）
- $\beta_k t$：トレンド成分
- $\gamma_t$：12ヶ月周期の季節性パターン $\in [-3\Gamma, 3\Gamma]$
- $\varepsilon_t \sim \mathcal{N}(0, \sigma)$：確率的変動

### 時間変動容量

| パラメータ | 内容 |
|---|---|
| **RR (Resource Range)** $\in \{0, 0.25, 0.50, 0.75\}$ | 容量の変動幅．$\text{cap}_k(t) = \text{cap}_k \cdot (1 - \text{RR}) + \text{noise}$ |
| **RV (Resource Vacation)** on/off | 14日ごとに1日の完全休暇（容量 = 0） |

### アクティビティ分割モード

| モード | 内容 |
|---|---|
| **P1**（分割なし） | ジョブを連続して実行（基本RCPSP） |
| **P2**（Non-Preemptive Splitting） | 資源不足の時刻でのみ中断を許可 |
| **P3**（Preemptive Splitting） | 任意時刻での中断・再開を許可 |

P2/P3 にはさらに **セットアップ時間モデル**（TW / WD / WR）を追加可能．

---

## 解法

### NSGA-II（主要アルゴリズム）

多目的遺伝的アルゴリズム NSGA-II により，メイクスパンとコストのトレードオフを表すパレートフロントを近似する．

#### エンコーディング

変数は `2n` 個の整数配列で表現する：

```
vars[0..n-1]   : 活動リスト（先行制約を満たすジョブの順列）
vars[n..2n-1]  : スケジューリング制御変数（下記2種）
```

**SchedObj エンコーディング**（0/1バイナリ）
- `0` → 最早開始時刻に配置（メイクスパン優先）
- `1` → `[t_mak, t_mak + maxShift]` の範囲でコスト最安時刻に配置

**MaxShift エンコーディング**（整数 $\in [0, T/4]$）
- `max_shift = 0` → 最早配置
- `max_shift > 0` → コスト探索窓口の幅を連続的に制御

#### 遺伝的操作

| 操作 | SchedObj | MaxShift |
|---|---|---|
| 選択 | BinaryTournament2 | BinaryTournament2 |
| 交叉 | Hartmann (1998) 2点交叉 | Hartmann 2点交叉 + max_shift継承 |
| 変異 | 挿入変異 + ビット反転 | 挿入変異 + ゼロリセット(70%) / 再サンプリング(30%) |

#### 初期集団

- ランダムトポロジカルソートによる多様な活動リスト
- MaxShift では **メイクスパン極端解 × 3**（全ジョブ `max_shift=0`）と **コスト極端解 × 3**（全ジョブ `max_shift=T/4`）を強制挿入し，パレートフロント両端の発見を促進

#### NSGA-II ループ

```
1. 初期集団生成 (N 個体)
2. 繰り返し (評価回数 < maxEvaluations):
   a. BinaryTournament2 で親を選択
   b. 交叉・変異で子 N 個体を生成
   c. RCPSP スケジューラで評価（makespan, cost 計算）
   d. 親 + 子 = 2N 個体を非支配ソート（Ranking）
   e. フロント0 から順に N 個体を次世代に選択
      └─ 端数フロントは Crowding Distance でトリミング
3. 複数ストラテジーの最終フロントを統合 → 非支配フィルタ
```

### Branch and Bound（正確解法）

小規模インスタンス向けに，**$\varepsilon$-制約法**と分枝限定法を組み合わせた正確解法を実装．コスト上限 $\varepsilon$ を段階的に絞りながらメイクスパン最小解を求め，パレートフロント上の複数点を導出する（NSGA-II 解の検証用）．

---

## 実験条件

| 設定 | 値 |
|---|---|
| ベンチマーク | PSPLIB（j30 / j60 / j90 / j120） |
| 集団サイズ | 100 |
| 評価回数/ストラテジー | 50,000〜100,000 |
| ストラテジー数 | 4（結果統合） |
| RR 条件 | 0.00 / 0.25 / 0.50 / 0.75 |
| RV 条件 | off / on |
| 比較エンコーディング | SchedObj vs MaxShift |

---

## ビルド・実行

```bash
# cmake-build-release でビルド
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --target NSGAEncCpp

# 実行例（j30 インスタンス）
./NSGAEncCpp j30.sm/j301_1.sm

# 結果の可視化
python ../visualize_encoding_comparison.py --dir . --prefix j301_1
```

### 出力ファイル

| ファイル | 内容 |
|---|---|
| `FUN_ENC_<prefix>_<cond>_SchedObj.txt` | SchedObj パレートフロント（makespan, cost） |
| `FUN_ENC_<prefix>_<cond>_MaxShift.txt` | MaxShift パレートフロント（makespan, cost） |
| `SCHED_ENC_*.txt` | 各解のスケジュール（開始時刻） |
| `figures/encoding_cmp_*.png` | パレートフロント比較グラフ |

---

## 実装

| ディレクトリ | 内容 |
|---|---|
| `src/core/` | フレームワーク基盤（Problem, Solution, Algorithm） |
| `src/problems/` | RCPSP 問題クラス群・インスタンス読み込み |
| `src/metaheuristics/` | NSGA-II，Branch and Bound |
| `src/operators/` | 交叉・変異・選択オペレータ |
| `src/util/` | 非支配ソート，Crowding Distance，比較器 |
