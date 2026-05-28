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
| `maxshift_sensitivity_RR000_RV0.csv` | MaxShift 上限 T/4 妥当性検証の感度分析結果 |
| `figures/encoding_cmp_*.png` | パレートフロント比較グラフ |

`EncodingComparisonRunner_P1` を単独で使用した場合のファイル名は `FUN_ENC_<prefix>_<cond>_SchedObj.txt`（モード名なし）。

---

## 実装

### ディレクトリ構造

| ディレクトリ | 内容 |
|---|---|
| `src/core/` | フレームワーク基盤（Problem, Solution, Algorithm） |
| `src/problems/` | RCPSP 問題クラス群・インスタンス読み込み |
| `src/metaheuristics/` | NSGA-II，Branch and Bound |
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

