#pragma once
#ifndef BRANCH_AND_BOUND_H
#define BRANCH_AND_BOUND_H

#include <vector>
#include <limits>
#include "RCPSP_Problem.h"

// ============================================================
//  BnB の1解 (パレートフロントの1点)
// ============================================================
struct BnBSolution {
    int    makespan = 0;
    double cost     = 0.0;
    std::vector<int> startTime;   // startTime[j]: ジョブ j の開始時刻
};

// ============================================================
//  BranchAndBound
//
//  ε制約法 + 分枝限定法で RCPSP を解く
//  【外側】ε を (C_max-C_min)/numDivisions ずつ減らしながらループ
//  【内側】コスト ≤ ε を守りながらメイクスパン最小化
// ============================================================
class BranchAndBound {
public:
    explicit BranchAndBound(RCPSP_Problem *problem);

    // ε制約法でパレートフロントを求める
    //   numDivisions : [C_min, C_max] の分割数 (デフォルト 12 → 13点)
    //   戻り値       : パレートフロントの点列 (makespan 昇順)
    std::vector<BnBSolution> solveEpsilonConstraint(int numDivisions = 12);

private:
    // ── 前処理 ──────────────────────────────────────────────
    void   computeESTLFT();       // EST[j], LFT[j] を計算
    double computeCmin();         // 資源無視・最安配置のコスト下界

    // ── BnB 本体 ────────────────────────────────────────────
    // ε固定で BnB を1回実行。解なし → nullptr
    BnBSolution* solveForEpsilon(double epsilon);

    // 再帰分岐
    void branch(
        std::vector<int>&              startTime,
        std::vector<bool>&             scheduled,
        std::vector<std::vector<int>>& usage,
        double                         currentCost,
        double                         epsilon,
        BnBSolution*&                  best
    );

    // ── 下界 ────────────────────────────────────────────────
    // (a) 先行制約による makespan 下界
    // (b) EST[j]+dur[j] の最大値
    int    lbMakespan(const std::vector<int>& startTime,
                      const std::vector<bool>& scheduled);

    // currentCost + Σ cheapestCost_[j] (未スケジュール j)
    double lbCost(const std::vector<bool>& scheduled, double currentCost);

    // ── 資源管理 ────────────────────────────────────────────
    bool canPlace (int j, int t, const std::vector<std::vector<int>>& usage);
    void placeJob (int j, int t, std::vector<std::vector<int>>& usage);
    void unplaceJob(int j, int t, std::vector<std::vector<int>>& usage);

    // ── コスト計算 ──────────────────────────────────────────
    double jobCostAt(int j, int t);

    // [EST_[j], LFT_[j]-dur[j]] 範囲内の最安連続スロットのコスト (資源無視)
    double cheapestCostForJob(int j);

    // ── メンバ ──────────────────────────────────────────────
    RCPSP_Problem* problem_;
    int nJobs_, nRes_, horizon_;

    std::vector<int>              EST_, LFT_;
    std::vector<std::vector<int>> preds_;        // preds_[j] = j の先行ジョブ
    std::vector<double>           cheapestCost_; // 各ジョブの最安コスト (事前計算)
};

#endif // BRANCH_AND_BOUND_H
