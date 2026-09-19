#pragma once
#ifndef RCPSP_MIP_SOLVER_H
#define RCPSP_MIP_SOLVER_H

#include "RCPSP_Problem.h"
#include <string>
#include <vector>

// ============================================================
//  RCPSP_MIP_Solver
//
//  Gurobi を使った RCPSP の厳密解法。
//  時刻インデックス型 MIP 定式化 + ε 制約法でバイオブジェクティブ
//  Pareto フロントを生成する。
//
//  定式化:
//    変数: x[j][t] ∈ {0,1}  ジョブ j が時刻 t に開始
//    制約 (1): Σ_t x[j][t] = 1                     各ジョブ 1 回開始
//    制約 (2): Σ_t t·x[i][t] + d_i ≤ Σ_t t·x[j][t]  先行制約 (i→j)
//    制約 (3): Σ_j r_{jk}·Σ_{τ∈active(j,t)} x[j][τ] ≤ R_k(t)  資源制約
//    目的 (A): min makespan = Σ_t t·x[n-1][t]  (ダミー終端ジョブ開始時刻)
//    目的 (B): min cost = Σ_j Σ_t c_{jt}·x[j][t]  (c_{jt}=computeJobCostAt)
//
//  ε 制約法（辞書式2段階、AUGMECON相当）:
//    1. makespan 最小化 → C*_ms を取得
//    2. ε ∈ [C*_ms, ub_ms] を Config::epsBreakpoints 点に等間隔サンプリング
//       （既定30点。両端を必ず含む。<=0 なら 1 刻み全走査）しながら各点で
//       Stage 1: 「makespan ≤ ε」のもとで cost 最小化 → C*
//       Stage 2: 「makespan ≤ ε かつ cost ≤ C*+tol」のもとで makespan 最小化
//       （Stage 1 だけだと同コストで makespan が最小でない弱パレート解を返しうるため、
//         Stage 2 で詰める。Stage 2 が不可解ならフォールバックで Stage 1 の解を採用）
//    3. サンプリング点を使い切ったら停止
//
//  対象インスタンス: j30 程度（変数数 ≈ n×T ≈ 32×160 = 5120 バイナリ変数）
// ============================================================
class RCPSP_MIP_Solver : public RCPSP_Problem {
public:
    // ---- ソルバー設定 ----
    struct Config {
        double timeLimit     = 300.0;  // 1 ソルブあたりの時間制限 [秒]
        int    threads       = 4;      // Gurobi スレッド数
        bool   verbose       = false;  // Gurobi ログ出力
        int    makespanSlack = 40;     // ε 上限 = C*_ms + slack
        double mipGapTol     = 1e-4;   // MIP ギャップ許容値

        // ε サンプリング（ブレイクポイント数）。
        // [lb_ms, ub_ms] を等間隔に epsBreakpoints 点サンプリングして解く（両端を必ず含む）。
        // <= 0 の場合は従来通り 1 刻み全走査（後方互換）。
        int    epsBreakpoints = 30;

        // NSGA 解などによる warm start（任意）。
        // サイズ n（ジョブ数）で各ジョブの開始時刻を与えると GRB_DoubleAttr_Start に設定する。
        // 空なら warm start なし（既定）。
        std::vector<int> warmStart;
    };

    // ---- ソルブ結果 ----
    struct Result {
        bool   feasible     = false;
        bool   optimal      = false;   // 最適性証明済み
        double gap          = 1.0;
        double makespan     = 1e9;
        double cost         = 1e9;
        double solveSeconds = 0.0;     // この解を得るのに要した時間 [秒]
        std::vector<int> startTimes;  // 各ジョブの開始時刻

        // solvePareto の Stage 2（辞書式タイトニング）適用前の makespan。
        // 適用対象外・適用不要（詰まらなかった）の場合は makespan と同値。
        // solvePareto 以外の単体ソルブでは未使用（-1）。
        double makespanBeforeTighten = -1.0;
    };

    explicit RCPSP_MIP_Solver(const std::string &filename,
                               double rr = 0.0,
                               bool   rv = false);

    // ---- ユーティリティ ----
    int  getHorizon()                    const;  // Σdurations または capacity_t 長
    int  getCapacityAtTime(int k, int t) const;  // protected capacityAtTime の公開ラッパー

    // ---- P2/P3 変換用ラッパー（既存モデル構築ロジックは一切変更しない）----
    // instance を丸ごと差し替える（P3: 単位分割済みインスタンス / P2: 時間圧縮済みインスタンス）。
    void replaceInstance(const RCPSP_Instance &newInstance) {
        replaceInstanceForMIPTransform(newInstance);
    }
    // 資源kの時刻tにおける単位コスト c_k(t)（正本コスト表、または一時上書き中の表を参照）。
    double getResourceCostAt(int k, int t, int horizon) const {
        return resourceCostAt(k, t, horizon);
    }
    // ジョブjが時刻tに1単位実行したときのコスト（全資源合計）。P2/P3の実行時刻リストから
    // 実コストを再計算する検証・レポート用。
    double getSlotCost(int j, int t, int horizon) const {
        return computeSlotCost(j, t, horizon);
    }
    // P2: 圧縮軸のコスト表を一時的に差し替える／正本状態へ復元する。
    static void overrideCostTable(const std::vector<std::vector<double>> &table) {
        overrideGlobalCostTable(table);
    }
    static void restoreCostTable() {
        restoreCanonicalCostTable();
    }

    // ---- 単体ソルブ ----

    // makespan 最小化
    Result solveMakespan(const Config &cfg = {}) const;

    // cost 最小化（makespan ≤ bound 制約付き）
    Result solveCost(int makespanBound, const Config &cfg = {}) const;

    // ---- Pareto フロント生成（ε 制約法）----
    std::vector<Result> solvePareto(const Config &cfg = {}) const;
};

#endif // RCPSP_MIP_SOLVER_H
