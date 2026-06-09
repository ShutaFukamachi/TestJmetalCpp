# 時間変動コスト・時間変動リソース容量および作業分割を考慮したRCPSPに対するNSGA-IIの改良

## 概要

本研究では，**時間変動コスト (Time-Varying Cost) および時間変動容量 (Time-Varying Capacity) を伴う資源制約付きプロジェクトスケジューリング問題 (RCPSP)** を多目的最適化問題として定式化し，NSGA-II による求解を行う．2 種類のスケジューリングエンコーディング（SchedObj / MaxShift）を提案・比較し，パレートフロントの近似精度と多様性を評価する．

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

## 解法：NSGA-II による多目的最適化

多目的遺伝的アルゴリズム **NSGA-II** により，メイクスパンとコストのトレードオフを表すパレートフロントを近似する．本研究では 2 種類のエンコーディングを提案し，その性能を比較する．

### 染色体表現

変数は `2n` 個の整数配列で表現する：

```
vars[0..n-1]   : 活動リスト（先行制約を満たすジョブの順列）
vars[n..2n-1]  : スケジューリング制御変数（エンコーディングにより意味が異なる）
```

---

### SchedObj エンコーディング

#### 概念

`vars[n..2n-1]` を **0/1 バイナリ変数** として扱う．各ジョブの配置方針を「メイクスパン優先」か「コスト優先」かの二択で表現する．

| 値 | 動作 |
|---|---|
| `0` | そのジョブを **最早開始時刻 (EST)** に配置（メイクスパン短縮を優先） |
| `1` | `[EST, EST + maxShift]` の範囲でコストが最安となる時刻に配置 |

#### スケジューリング手順

活動リストの順序に従い，各ジョブを逐次配置する Serial Scheduling Scheme (SSS) を採用：

1. 活動リスト順にジョブを処理
2. `mode = 0`: 資源制約を満たす最早時刻に配置
3. `mode = 1`: `[EST, EST + maxShift]` の窓内で各時刻のコストを評価し，最安時刻に配置
4. `maxShift` は問題全体で共通の定数（探索中は固定）

#### 特徴

- 制御変数が単純なバイナリのため，解の意味が直感的
- `mode = 0` の個体が集まればメイクスパン寄りのフロント端，`mode = 1` が集まればコスト寄りの端を形成
- バイナリ表現により探索空間が離散的で，コスト方向の細かい調整が困難

#### 遺伝的操作

| 操作 | 内容 |
|---|---|
| 選択 | BinaryTournament2（非支配ランク + 混雑距離） |
| 交叉 | Hartmann (1998) 2点交叉（活動リスト部） + ビット単位一様交叉（制御変数部） |
| 変異 | 挿入変異（活動リスト部） + ビット反転変異（制御変数部） |

---

### MaxShift エンコーディング

#### 概念

`vars[n..2n-1]` を **整数値 $\in [0, T/4]$**（ $T$ =メイクスパン上限）として扱う．各ジョブに対して「コスト探索窓口の幅」を連続的に制御できる．

| 値 | 動作 |
|---|---|
| `0` | そのジョブを最早開始時刻に配置（SchedObj の `mode=0` と同等） |
| `m > 0` | `[EST, EST + m]` の範囲でコスト最安時刻に配置 |

SchedObj がバイナリ（on/off）でコスト探索の有無を切り替えるのに対し，MaxShift は探索窓の**幅**を実数的に制御する点が根本的な違いである．

#### スケジューリング手順

1. 活動リスト順にジョブを処理
2. `max_shift = 0`: EST に即配置
3. `max_shift = m > 0`: `[EST, EST + m]` の各時刻のコストを評価し最安時刻に配置
4. 各ジョブが独立した `max_shift` 値を持つため，ジョブごとに探索圧力を細かく調整できる

#### 特徴

- コスト方向への探索が**連続的かつジョブ単位**で制御可能
- `max_shift = 0` の個体群 → メイクスパン極端解，`max_shift = T/4` 全ジョブ → コスト極端解
- SchedObj より広い表現空間を持ち，パレートフロントの中間領域を密に埋めやすい
- 上限値 $T/4$ の妥当性は `MaxShiftSensitivityAnalyzer` で事前検証

#### 初期集団の工夫

多様性確保のため，ランダム個体に加えて以下を強制挿入する：

| 挿入個体 | 内容 |
|---|---|
| **メイクスパン極端解 × 3** | 全ジョブ `max_shift = 0`（最短工期特化） |
| **コスト極端解 × 3** | 全ジョブ `max_shift = T/4`（最安コスト特化） |

これによりパレートフロント両端の発見を初期世代から保証する．

#### 遺伝的操作

| 操作 | 内容 |
|---|---|
| 選択 | BinaryTournament2（非支配ランク + 混雑距離） |
| 交叉 | Hartmann 2点交叉（活動リスト部） + `max_shift` 値の継承（制御変数部） |
| 変異 | 挿入変異（活動リスト部） + 70% でゼロリセット / 30% で `[0, T/4]` 再サンプリング（制御変数部） |

ゼロリセットを高確率で行うことで，メイクスパン方向への収束圧力を維持しつつ，再サンプリングで多様性を補う．

---

### NSGA-II ループ

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

複数回（`numStrategies` 回）の独立した NSGA-II 実行結果を最後に統合し，最終パレートフロントを得る．

---

### 2エンコーディングの比較まとめ

| 項目 | SchedObj | MaxShift |
|---|---|---|
| 制御変数の型 | 0/1 バイナリ | 整数 $[0, T/4]$ |
| コスト探索の粒度 | ジョブ単位・二択 | ジョブ単位・連続的 |
| メイクスパン方向の表現 | `mode=0` 固定 | `max_shift=0` 固定 |
| パレートフロント中間域 | 離散的・疎になりやすい | 連続的・密に埋まりやすい |
| 初期集団の工夫 | ランダム | ランダム + 両端極端解を強制挿入 |
| 変異戦略 | ビット反転 | ゼロリセット優先 + 再サンプリング |

---

## 実験条件

| 設定 | 値 |
|---|---|
| ベンチマーク | PSPLIB（j30 / j60 / j90 / j120） |
| 集団サイズ | 100 |
| 評価回数/ストラテジー | 50,000（本番: 100,000） |
| ストラテジー数 | 4（結果統合） |
| RR 条件 | 0.00 / 0.25 / 0.50 / 0.75 |
| RV 条件 | off / on |
| 比較エンコーディング | SchedObj vs MaxShift |
| 作業分割モード | P1 / P2 / P3（全モード比較） |

---

## ランナークラス

`main.cpp` には 2 種類のランナークラスが定義されている。

| クラス | 説明 |
|---|---|
| `EncodingComparisonRunner_P1` | P1（分割なし）のみで SchedObj vs MaxShift を比較（旧来動作） |
| `EncodingComparisonRunner_All` | P1/P2/P3 それぞれで SchedObj vs MaxShift を比較 |

現在 `main()` は **`EncodingComparisonRunner_All`** を使用し，実行前に `MaxShiftSensitivityAnalyzer` による感度分析も行う。

### 問題クラス対応表

| 作業分割モード | SchedObj | MaxShift |
|---|---|---|
| P1（分割なし） | `RCPSP_Problem` | `RCPSP_Problem_MaxShift` |
| P2（資源不足時のみ中断） | `RCPSP_Problem_Splitting(P2)` | `RCPSP_Problem_Splitting_MaxShift(P2)` |
| P3（任意中断・再開） | `RCPSP_Problem_Splitting(P3)` | `RCPSP_Problem_Splitting_MaxShift(P3)` |

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

実行時は **`EncodingComparisonRunner_All`**（P1/P2/P3 全比較）が使われるため，ファイル名には分割モード名（`_P1_`, `_P2_`, `_P3_`）が付く。

| ファイル | 内容 |
|---|---|
| `FUN_ENC_<prefix>_<cond>_P1_SchedObj.txt` | P1+SchedObj パレートフロント（makespan, cost） |
| `FUN_ENC_<prefix>_<cond>_P1_MaxShift.txt` | P1+MaxShift パレートフロント |
| `FUN_ENC_<prefix>_<cond>_P2_SchedObj.txt` | P2+SchedObj パレートフロント |
| `FUN_ENC_<prefix>_<cond>_P2_MaxShift.txt` | P2+MaxShift パレートフロント |
| `FUN_ENC_<prefix>_<cond>_P3_SchedObj.txt` | P3+SchedObj パレートフロント |
| `FUN_ENC_<prefix>_<cond>_P3_MaxShift.txt` | P3+MaxShift パレートフロント |
| `SCHED_ENC_<prefix>_<cond>_<mode>_<enc>.txt` | 各解のスケジュール（開始時刻） |
| `maxshift_sensitivity_<prefix>_RR000_RV0.csv` | MaxShift 上限 T/4 妥当性検証の感度分析結果 |
| `figures/encoding_cmp_*.png` | パレートフロント比較グラフ |

`EncodingComparisonRunner_P1` を単独で使用した場合のファイル名は `FUN_ENC_<prefix>_<cond>_SchedObj.txt`（モード名なし）。

---

## 実装

### ディレクトリ構造

| ディレクトリ | 内容 |
|---|---|
| `src/core/` | フレームワーク基盤（Problem, Solution, Algorithm） |
| `src/problems/` | RCPSP 問題クラス群・インスタンス読み込み |
| `src/metaheuristics/` | NSGA-II |
| `src/operators/` | 交叉・変異・選択オペレータ |
| `src/util/` | 非支配ソート，Crowding Distance，比較器 |

### 主要ファイル（`src/problems/`）

| ファイル | 役割 |
|---|---|
| `RCPSP_Problem.h/cpp` | 基底問題クラス，SchedObj 評価，capacity_t 生成 |
| `RCPSP_Problem_MaxShift.h/cpp` | MaxShift エンコーディング評価 |
| `RCPSP_Problem_Splitting.h/cpp` | P2/P3 作業分割 + SchedObj 評価 |
| `RCPSP_Problem_Splitting_MaxShift.h/cpp` | P2/P3 作業分割 + MaxShift 評価 |
| `RCPSP_Problem_Setup.h/cpp` | セットアップ時間モデル（TW/WD/WR） |
| `MaxShiftSensitivity.h/cpp` | MaxShift 上限値の感度分析クラス |
| `RCPSP_Reader.h/cpp` | PSPLIB インスタンス読み込み |

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

`main.cpp` を読み込み，以下の項目が README と一致しているか確認・修正する。

| 確認項目 | 対象箇所 |
|---|---|
| 使用中のランナークラス | `main()` 内のインスタンス生成 |
| Config パラメータ | `populationSize` / `evalsPerStrategy` / `numStrategies*` |
| 有効な解析 | `MaxShiftSensitivityAnalyzer` の on/off |
| 出力ファイル命名 | `FUN_ENC_*` / `SCHED_ENC_*` のプレフィックス規則 |
| 使用中の分割モード | P1 / P2 / P3 の実使用状況 |

既に正確なセクションは変更しない。README は日本語を維持する。
