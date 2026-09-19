# 時間変動コスト・時間変動リソース容量および作業分割を考慮したRCPSPに対するNSGA-IIの改良

## 概要

**時間変動コスト (Time-Varying Cost) および時間変動容量 (Time-Varying Capacity) を伴う資源制約付きプロジェクトスケジューリング問題 (RCPSP)** を多目的最適化問題として定式化し，NSGA-II による求解を行う．3 種類のスケジューリングエンコーディング（SchedObj / MaxShift / MaxShiftDur）を提案・比較し，パレートフロントの近似精度と多様性を評価する．さらに Gurobi MIP ソルバーによる厳密 Pareto フロントを生成し，ヒューリスティック解との比較を行う．

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
| $f_1$: メイクスパン最小化 | $\min \text{start}(j_{n-1})$（ダミー終端ジョブの開始時刻） |
| $f_2$: 総コスト最小化 | $\min \sum_j \sum_t c_{k}(t) \cdot r_{jk} \cdot x_{jt}$ |

### 時間変動コスト

資源 $k$ の時刻 $t$ におけるコスト $c_k(t)$ は以下で生成される：

$$c_k(t) = \alpha_k + \beta_k t + \gamma_t + \varepsilon_t$$

- $\alpha_k$：基本コスト（資源ごとにランダム）
- $\beta_k t$：トレンド成分
- $\gamma_t$：12 ヶ月周期の季節性パターン $\in [-3\Gamma, 3\Gamma]$
- $\varepsilon_t \sim \mathcal{N}(0, \sigma)$：確率的変動

### 時間変動容量

| パラメータ | 内容 |
|---|---|
| **RR (Resource Range)** $\in \{0, 0.25, 0.50, 0.75\}$ | 容量の変動幅 |
| **RV (Resource Vacation)** on/off | 14日ごとに1日の完全休暇（容量 = 0） |

実験条件は RR × RV の組み合わせ 8 条件：`RR000_RV0 / RR000_RV1 / RR025_RV0 / ... / RR075_RV1`

### アクティビティ分割モード

| モード | 内容 |
|---|---|
| **P1**（分割なし） | ジョブを連続して実行（基本 RCPSP） |
| **P2**（Non-Preemptive Splitting） | 資源不足の時刻（容量ゼロ＝休暇）でのみ中断を許可 |
| **P3**（Preemptive Splitting） | 任意時刻での中断・再開を許可 |

---

## 解法

### NSGA-II エンコーディング

変数は `2n` 個の整数配列で表現する：

```
vars[0 .. n-1]   : 活動リスト（先行制約を満たすジョブの順列）
vars[n .. 2n-1]  : スケジューリング制御変数（エンコーディングにより意味が異なる）
```

---

### SchedObj エンコーディング

`vars[n..2n-1]` を **0/1 バイナリ変数** として扱う．

| 値 | 動作 |
|---|---|
| `0` | そのジョブを **最早開始時刻 (EST)** に配置（メイクスパン優先） |
| `1` | `[EST, EST + maxShift]` の範囲でコストが最安となる時刻に配置 |

`maxShift` は問題全体で共通の定数（`T/4`）．

**遺伝的操作**

| 操作 | 内容 |
|---|---|
| 選択 | BinaryTournament2（非支配ランク + 混雑距離） |
| 交叉 | Hartmann 2点交叉（活動リスト部） + 一様交叉（制御変数部） |
| 変異 | 挿入変異（活動リスト部） + ビット反転（制御変数部） |

**Strategy（S1〜S4）**: `maxShift` の上限 $0, T/8, T/4, T/2$ を使い分けた 4 種の独立 NSGA-II 実行

---

### MaxShift エンコーディング

`vars[n..2n-1]` を **整数値 $\in [0, T/4]$** として扱う（ジョブごとに独立した探索窓幅）．

| 値 | 動作 |
|---|---|
| `0` | EST に即配置（makespan 優先） |
| `m > 0` | `[EST, EST + m]` の範囲でコスト最安時刻に配置 |

SchedObj がバイナリ（on/off）でコスト探索の有無を切り替えるのに対し，MaxShift は探索窓の**幅をジョブ単位で連続的**に制御する点が根本的な違いである．

**遺伝的操作**

| 操作 | 内容 |
|---|---|
| 選択 | BinaryTournament2 |
| 交叉 | Hartmann 2点交叉（活動リスト部） + 値継承（制御変数部） |
| 変異 | 挿入変異（活動リスト部） + 70% ゼロリセット / 30% 再サンプリング（制御変数部） |

**初期集団**: ランダム個体に加え，makespan 極端解（全 `max_shift = 0`）× 3 と cost 極端解（全 `max_shift = T/4`）× 3 を強制挿入．

**Strategy（S1〜S4）**: 上限 $0, T/8, T/4, T/2$ を使い分けた 4 種の独立 NSGA-II 実行

---

### MaxShiftDur エンコーディング

`vars[n..2n-1]` を **正規化比率キー $\rho_j \in [0, R=1000]$** として扱う（Gonçalves et al. 2008 の duration-scaled delay に着想）．

**窓幅の計算式:**

$$\text{eff}_j = \max(\alpha \cdot d_j,\ \beta \cdot T), \quad
  w_j = \text{round}(\rho_j \cdot \text{eff}_j)$$

- $d_j$: ジョブ $j$ の所要時間，$T$: メイクスパン上限
- $\alpha$: Strategy に依存する係数（S1=0.0, S2=0.5, S3=1.0, S4=1.5）
- $\beta = 0.5$: $w_j \geq \beta T / 2$ を保証するフロア（短ジョブが極端に窓を狭めないための安全網）

$\rho_j = 0 \Rightarrow w_j = 0$：EST 配置（makespan 優先），$\rho_j = R \Rightarrow$ 最大窓でコスト探索．

MaxShift がグローバルな絶対窓幅 $m \in [0, T/4]$ を使うのに対し，MaxShiftDur は**ジョブの所要時間に比例したスケーリング**を行う点が異なる．

**遺伝的操作**: MaxShiftCrossover（値継承） + MaxShiftDurMutation（比率キー $\rho$ のゼロリセット/再サンプリング）

**Strategy（S1〜S4）**: $\alpha = 0.0, 0.5, 1.0, 1.5$ を使い分けた 4 種の独立 NSGA-II 実行

---

### 3エンコーディング比較まとめ

| 項目 | SchedObj | MaxShift | MaxShiftDur |
|---|---|---|---|
| 制御変数の型 | 0/1 バイナリ | 整数 $[0, T/4]$ | 整数 $[0, R=1000]$（比率キー） |
| 窓幅の粒度 | ジョブ単位・二択 | ジョブ単位・絶対値連続 | ジョブ単位・所要時間スケール |
| Strategy の違い | maxShift 上限 $0, T/8, T/4, T/2$ | 上限 $0, T/8, T/4, T/2$ | $\alpha = 0.0, 0.5, 1.0, 1.5$ |
| 短ジョブへの対応 | maxShift 固定 → 相対的に過大 | 絶対幅 → 短ジョブも同じ幅 | $\beta T$ フロアで最低幅を保証 |
| 変異戦略 | ビット反転 | ゼロリセット優先 | ゼロリセット優先（比率キー版） |

---

### NSGA-II ループ

```
1. 初期集団生成（N 個体；両端極端解を強制挿入）
2. 繰り返し（評価回数 < maxEvaluations）:
   a. BinaryTournament2 で親を選択
   b. 交叉・変異で子 N 個体を生成
   c. RCPSP スケジューラで評価（makespan, cost 計算）
   d. 親 + 子 = 2N 個体を非支配ソート（Ranking）
   e. フロント0 から順に N 個体を次世代に選択
      └─ 端数フロントは Crowding Distance でトリミング
3. 4 Strategy の最終フロントを統合 → 非支配フィルタ
```

---

### Gurobi MIP 厳密解法

時刻インデックス型 MIP 定式化 + **ε 制約法**でバイオブジェクティブ Pareto フロントを生成する．

**変数**: $x_{jt} \in \{0,1\}$：ジョブ $j$ が時刻 $t$ に開始

**制約**:
1. $\sum_t x_{jt} = 1$（各ジョブ 1 回だけ開始）
2. $\sum_t t \cdot x_{it} + d_i \leq \sum_t t \cdot x_{jt}$（先行制約）
3. $\sum_j r_{jk} \sum_{\tau: \tau \leq t < \tau + d_j} x_{j\tau} \leq R_k(t)$（資源制約）

**ε 制約法**:
1. makespan 最小化 → $C^*_{ms}$ を取得
2. $\varepsilon = C^*_{ms}, C^*_{ms}+1, \ldots, C^*_{ms} + \text{slack}$ と増やしながら「makespan ≤ ε」制約下で cost 最小化
3. コストが改善しなくなったら停止

対象: j30 インスタンス（変数数 ≈ 32×160 = 5120 バイナリ変数）

---

## ビルドターゲット一覧

`CMakeLists.txt` が現在定義するターゲットは以下の 11 個。TestJmetalCpp 系列（主要な比較実験用）に加え，個別の検証・実験ハーネス（A3AB / E17 / PSGS / MIPVERIFY / MSVERIFY / NSGAMaxShiftCpp）が残っている。

| ターゲット名 | ソースファイル | 目的 |
|---|---|---|
| `TestJmetalCpp` | `main.cpp` | P1/P2/P3 × SchedObj/MaxShift 全インスタンス一括比較バッチランナー + 感度分析 + P1 MaxShift 局所探索(LS)後処理 + Novelty Filter(NF) 追加検証ラン |
| `NSGAEncCpp` | `main_NSGAII_RCPSP_EncodingComparison.cpp` | **メイン比較ランナー**: SchedObj vs MaxShift, P1/P2/P3（単一インスタンス実行） |
| `NSGAMaxShiftDurCpp` | `main_NSGAII_RCPSP_MaxShiftDur.cpp` | MaxShiftDur エンコーディング, P1/P2/P3（単一 or 全インスタンス一括） |
| `NSGAMaxShiftCpp` | `main_NSGAII_RCPSP_MaxShift.cpp` | MaxShift エンコーディング単独ランナー、P1 のみ（popSize=100, evalsPerStrategy=50000, 4 Strategy で MaxShiftDur/MIP と直接比較可能な結果を出力） |
| `NSGAMaxShiftSegCpp` | `main_NSGAII_RCPSP_MaxShiftSeg.cpp` | MaxShiftSeg エンコーディング（セグメント選択型デコード）, P1 のみ対応（単一 or 全インスタンス一括） |
| `RCPSPMIPCpp` | `main_MIP_RCPSP.cpp` | Gurobi MIP 厳密解法 |
| `A3AB` | `main_A3_AB.cpp` | A3（ランダム2000順列の上位1%を初期集団へ注入）が最終パレートフロントの makespan を改善するかの A/B 比較ハーネス |
| `E17` | `main_E17_PlacementExact.cpp` | E17: 順列固定＋配置厳密化（matheuristic）。NSGA-II(MaxShift) の各 makespan 水準について配置のみを小規模 MIP で厳密にコスト最小化 |
| `PSGS` | `main_ParallelSGS_AB.cpp` | 生成スキーム A/B（Serial SGS ⇄ Parallel SGS）比較。デコーダ切替でパレートフロントが改善するか検証 |
| `MIPVERIFY` | `main_MIP_Verify.cpp` | MIP が時変容量(RR/RV)を正しく解いているか（緩和バグの有無）の feasibility 検証 |
| `MSVERIFY` | `main_MaxShift_Verify.cpp` | 提案手法(MaxShift P1 / MaxShiftDur)の最終前線が制約を満たすかの feasibility 検証 |

---

## 実験条件

### NSGAEncCpp（SchedObj vs MaxShift 比較）

| 設定 | 値 |
|---|---|
| ベンチマーク | 単一インスタンス実行（デフォルト `j30.sm/j301_1.sm`、引数でインスタンス指定可）。20 インスタンス一括のバッチ実行は `TestJmetalCpp` が担当（SchedObj/MaxShift 両方、現状 j30 × 5 のみ有効化） |
| 集団サイズ | 100 |
| 評価回数/ストラテジー | 100,000 |
| ストラテジー数 | 4（結果統合） |
| 比較エンコーディング | SchedObj vs MaxShift |
| 作業分割モード | P1 / P2 / P3 |
| RR 条件 | 0.00 / 0.25 / 0.50 / 0.75 |
| RV 条件 | off / on |
| 感度分析 | MaxShiftSensitivityAnalyzer による上限 $T/4$ の妥当性検証 |

### NSGAMaxShiftDurCpp（MaxShiftDur）

| 設定 | 値 |
|---|---|
| ベンチマーク | 単一 or 全インスタンス一括（引数なしで `ALL_INSTANCES` を一括実行）。設計上は PSPLIB j30/j60/j90/j120 × 5（計 20 インスタンス）だが、現状 j30 × 5 のみ有効化（j60/j90/j120 はコメントアウト中） |
| 集団サイズ | 100 |
| 評価回数/ストラテジー | 50,000 |
| ストラテジー数 | 4（$\alpha = 0.0, 0.5, 1.0, 1.5$） |
| ハイブリッドパラメータ | $\beta = 0.5$（フロア係数） |
| 作業分割モード | P1 / P2 / P3 |
| RR / RV 条件 | 同上 8 条件 |

### RCPSPMIPCpp（Gurobi 厳密解法）

| 設定 | 値 |
|---|---|
| ベンチマーク | j30 × 5 インスタンス |
| 時間制限 | 120 秒/ソルブ |
| スレッド数 | 4 |
| $\varepsilon$ 探索幅（makespan slack） | 40 |
| RR / RV 条件 | 同上 8 条件 |

---

## 問題クラス対応表

| 作業分割モード | SchedObj | MaxShift | MaxShiftDur | MaxShiftSeg |
|---|---|---|---|---|
| P1（分割なし） | `RCPSP_Problem` | `RCPSP_Problem_MaxShift` | `RCPSP_Problem_MaxShiftDur` | `RCPSP_Problem_MaxShiftSeg` |
| P2（資源休暇時のみ中断） | `RCPSP_Problem_Splitting(P2)` | `RCPSP_Problem_Splitting_MaxShift(P2)` | `RCPSP_Problem_Splitting_MaxShiftDur(P2)` | — 未対応 |
| P3（任意中断・再開） | `RCPSP_Problem_Splitting(P3)` | `RCPSP_Problem_Splitting_MaxShift(P3)` | `RCPSP_Problem_Splitting_MaxShiftDur(P3)` | — 未対応 |
| 厳密解 | — | — | `RCPSP_MIP_Solver`（Gurobi） | — |

---

## 出力ファイル形式

### TestJmetalCpp

出力先は `results/FUN_ENC/{prefix}/`（インスタンスごとのサブディレクトリ、自動作成）。

| ファイル | 内容 |
|---|---|
| `results/FUN_ENC/{prefix}/FUN_ENC_{prefix}_{cond}_{P1\|P2\|P3}_{SchedObj\|MaxShift}.txt` | P1/P2/P3 Pareto フロント |
| `results/FUN_ENC/{prefix}/SCHED_ENC_{prefix}_{cond}_{P1\|P2\|P3}_{SchedObj\|MaxShift}.txt` | スケジュール詳細 |
| `results/FUN_ENC/{prefix}/FUN_ENC_{prefix}_{cond}_P1_MaxShift_LS.txt` | P1 MaxShift に `FrontLocalSearch` 局所探索を適用した後の Pareto フロント |
| `results/FUN_ENC/{prefix}/FUN_ENC_{prefix}_{cond}_P1_MaxShift_NF.txt` | Novelty Filter 有効時（`enableNoveltyFilter=true`）の P1 MaxShift 追加検証ラン |
| `analysis/sensitivity/{prefix}/maxshift_sensitivity_{prefix}_RR000_RV0.csv` | MaxShift 上限の感度分析結果 |
| `logs/localsearch/ls_log_{prefix}_{cond}.csv` | LS 後処理のログ（改善件数・コスト削減量） |

### NSGAEncCpp

| ファイル | 内容 |
|---|---|
| `FUN_ENC_{prefix}_{cond}_{P1\|P2\|P3}_{SchedObj\|MaxShift}.txt` | Pareto フロント（makespan cost） |
| `SCHED_ENC_{prefix}_{cond}_{P1\|P2\|P3}_{SchedObj\|MaxShift}.txt` | スケジュール詳細（開始時刻） |
| `maxshift_sensitivity_{prefix}_RR000_RV0.csv` | MaxShift 上限の感度分析結果 |
| `figures/encoding_cmp_{prefix}_{cond}.png` | Pareto フロント比較グラフ |

### NSGAMaxShiftDurCpp

出力先は `results/FUN_ENC/{prefix}/`（インスタンスごとのサブディレクトリ、自動作成）。

| ファイル | 内容 |
|---|---|
| `results/FUN_ENC/{prefix}/FUN_ENC_{prefix}_{cond}_MaxShiftDur.txt` | P1 Pareto フロント |
| `results/FUN_ENC/{prefix}/FUN_ENC_{prefix}_{cond}_P2_MaxShiftDur.txt` | P2 Pareto フロント |
| `results/FUN_ENC/{prefix}/FUN_ENC_{prefix}_{cond}_P3_MaxShiftDur.txt` | P3 Pareto フロント |
| `results/FUN_ENC/{prefix}/SCHED_ENC_*` | 各モードのスケジュール詳細 |
| `figures/MaxShiftDur/encoding_maxshiftdur_{prefix}_{cond}.png` | 1×3 サブプロット（P1/P2/P3）比較グラフ |

### NSGAMaxShiftSegCpp

出力先は `results/FUN_ENC/{prefix}/`（インスタンスごとのサブディレクトリ、自動作成）。P1 のみ対応。

| ファイル | 内容 |
|---|---|
| `results/FUN_ENC/{prefix}/FUN_ENC_{prefix}_{cond}_MaxShiftSeg.txt` | P1 Pareto フロント |
| `results/FUN_ENC/{prefix}/SCHED_ENC_{prefix}_{cond}_MaxShiftSeg.txt` | スケジュール詳細 |

### RCPSPMIPCpp

| ファイル | 内容 |
|---|---|
| `FUN_MIP_{prefix}_{cond}.txt` | 厳密 Pareto フロント（makespan cost） |
| `SCHED_MIP_{prefix}_{cond}.txt` | スケジュール詳細（NSGA-II と同形式） |

---

## ビルド・実行

```bash
# cmake-build-release でビルド
cmake -DCMAKE_BUILD_TYPE=Release ..

# SchedObj vs MaxShift 全比較（P1/P2/P3）
cmake --build . --target NSGAEncCpp
./NSGAEncCpp j30.sm/j301_1.sm

# MaxShiftDur 実行（P1/P2/P3）
cmake --build . --target NSGAMaxShiftDurCpp
./NSGAMaxShiftDurCpp j30.sm/j301_1.sm

# Gurobi MIP 厳密解法（j30 全インスタンス × 8条件）
cmake --build . --target RCPSPMIPCpp
./RCPSPMIPCpp
./RCPSPMIPCpp j30.sm/j301_1.sm   # 単一インスタンス
```

### 結果の可視化

```bash
# NSGAEncCpp の結果可視化
python ../visualize_all_comparison.py --dir .

# MaxShiftDur の結果可視化
python ../visualize_maxshiftdur.py --dir .
```

---

## 実装

### ディレクトリ構造

| ディレクトリ | 内容 |
|---|---|
| `src/core/` | フレームワーク基盤（Problem, Solution, Algorithm） |
| `src/problems/` | RCPSP 問題クラス群・インスタンス読み込み・MIP ソルバー |
| `src/metaheuristics/` | NSGA-II |
| `src/operators/` | 交叉・変異・選択オペレータ |
| `src/util/` | 非支配ソート，Crowding Distance，比較器 |

### 主要ファイル（`src/problems/`）

| ファイル | 役割 |
|---|---|
| `RCPSP_Problem.h/cpp` | 基底クラス，SchedObj 評価，capacity_t 生成 |
| `RCPSP_Problem_MaxShift.h/cpp` | MaxShift エンコーディング評価，`getHorizon()` |
| `RCPSP_Problem_MaxShiftDur.h/cpp` | MaxShiftDur エンコーディング（duration-scaled window） |
| `RCPSP_Problem_MaxShiftSeg.h/cpp` | MaxShiftSeg エンコーディング（窓をセグメント分割し多対一デコードを緩和、P1 のみ） |
| `RCPSP_Problem_Splitting.h/cpp` | P2/P3 作業分割 + SchedObj 評価 |
| `RCPSP_Problem_Splitting_MaxShift.h/cpp` | P2/P3 作業分割 + MaxShift 評価 |
| `RCPSP_Problem_Splitting_MaxShiftDur.h/cpp` | P2/P3 作業分割 + MaxShiftDur 評価 |
| `RCPSP_MIP_Solver.h/cpp` | Gurobi MIP 厳密解法（ε 制約法） |
| `MaxShiftSensitivity.h/cpp` | MaxShift 上限値の感度分析 |
| `RCPSP_Reader.h/cpp` | PSPLIB インスタンス読み込み |

### 局所探索（`src/localsearch/`）

| ファイル | 役割 |
|---|---|
| `FrontLocalSearch.h/cpp` | GA 最終パレートフロントに対する後処理局所探索（TestJmetalCpp の P1 MaxShift_LS 出力で使用） |

### 演算子ファイル（`src/operators/`）

| ファイル | 役割 |
|---|---|
| `crossover/MaxShiftCrossover` | MaxShift / MaxShiftDur 共用交叉（値継承） |
| `mutation/MaxShiftMutation` | MaxShift 変異（ゼロリセット優先） |
| `mutation/MaxShiftDurMutation` | MaxShiftDur 変異（比率キー版） |
| `mutation/MaxShiftSegMutation` | MaxShiftSeg 変異（セグメントセレクタキー版） |

---

## 日次メンテナンス Routines

毎日終業時に Claude Code Routines が自動実行され，以下の 2 タスクを行う。

### タスク 1 — `.miss_memory/` の更新

今日の Claude Code セッション履歴を検索し，以下に該当するバグ・ミスを抽出して教訓ファイルを追記する。

**記録対象**
- コンパイルエラー・クラッシュの診断と修正
- アルゴリズムのロジックバグ（誤出力・誤計算）
- 性能問題（CPU 過負荷・メモリ・過剰 I/O）
- ファイル操作・命名・パス解決のミス
- 思い込みが覆された場面（「X は〇〇だと思っていたが違った」）

**記録しないもの**
- バグを伴わない通常のコード変更
- スタイル・フォーマット修正
- 初回で動いた機能追加

**ファイル命名規則**
```
.miss_memory/<NNN>_<スネークケースタイトル>.md
```
既存ファイルの最大番号の続きから採番。その日新規バグがなければファイルを作成しない。

**ファイルフォーマット**
```markdown
# <NNN>: <タイトル>

## 発生日
YYYY-MM-DD

## バグの概要
（何が起きたか・症状）

## 根本原因
（なぜ起きたか・コード or 設計上の問題点）

## 修正内容
（どう直したか。コードスニペットがあれば記載）

## 教訓
- （箇条書きで再発防止のポイント）

## 確認方法（任意）
（修正が正しいことをどうやって確認するか）
```

---

### タスク 2 — `README.md` の更新

`main_NSGAII_RCPSP_EncodingComparison.cpp` と `main_NSGAII_RCPSP_MaxShiftDur.cpp` を読み込み，以下の項目が README と一致しているか確認・修正する。

| 確認項目 | 対象箇所 |
|---|---|
| Config パラメータ | `populationSize` / `evalsPerStrategy` / `numStrategies` / `beta` |
| 有効な解析 | `MaxShiftSensitivityAnalyzer` の on/off |
| 出力ファイル命名 | `FUN_ENC_*` / `SCHED_ENC_*` / `FUN_MIP_*` のプレフィックス規則 |
| ビルドターゲット | `CMakeLists.txt` に追加された新ターゲット |
| 問題クラス対応表 | 新しいエンコーディング・分割モードの追加 |

既に正確なセクションは変更しない。README は日本語を維持する。
