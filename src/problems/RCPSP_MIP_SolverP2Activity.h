#pragma once
#ifndef RCPSP_MIP_SOLVER_P2_ACTIVITY_H
#define RCPSP_MIP_SOLVER_P2_ACTIVITY_H

#include "RCPSP_MIP_Solver.h"
#include "RCPSP_MIP_Transform.h"   // RealSchedule
#include <vector>

// ============================================================
//  RCPSP_MIP_SolverP2Activity
//
//  P2（休暇時のみ中断）を NSGA 側の定義に完全一致させた MIP 定式化。
//
//  NSGA 側（RCPSP_Problem_Splitting::simulateP2/executeP2）の定義:
//    γ_jkt = 0 ⟺ capacityAtTime(k,t) <  demand[j][k]  → 中断してよい
//    γ_jkt = 1 ⟺ capacityAtTime(k,t) >= demand[j][k]  → 必ず実行しなければならない
//  判定は活動ごと・資源ごと（全資源同時容量ゼロの「休暇日」に限らない）。
//
//  実装方針: 活動 j ごとに実行可能時刻集合
//    A_j = { t : すべての資源 k で capacityAtTime(k,t) >= demand[j][k] }
//  を作り、活動 j は A_j 上で連続した d_j 要素を占める、という変数 x[j][i] を使う
//  （i は A_j の何番目の要素から開始するかを表す添字）。
//  「全活動で共有される単一の圧縮軸」だった旧実装（RCPSP_MIP_Transform.h の
//  TimeCompression 系、main_MIP_RCPSP.cpp の旧 P2 分岐）とは異なり、
//  活動ごとに別々の軸を持つため、この定式化は既存の RCPSP_MIP_Solver::buildModel
//  をそのまま流用できない（新規の制約構築コードが必要）。
//  RCPSP_MIP_Solver 本体（P1）・旧 P2（全体圧縮）実装は一切変更していない。
// ============================================================

// ステップ1検証用: GRBModel を構築せず、変換規模だけを計算する（軽量）。
struct P2ActivityScaleReport {
    std::vector<int> mLen;            // mLen[j] = |A_j|
    std::vector<int> numVars;         // numVars[j] = 活動 j の変数数（iHi[j]+1）
    long long totalBinaryVars = 0;    // Σ numVars[j]（実際のバイナリ変数総数、上限ではなく厳密値）
    bool feasiblePrecheck = true;     // false: いずれかの実ジョブで |A_j| < d_j
    int  infeasibleJob = -1;          // feasiblePrecheck==false のときの最初の該当ジョブID
};
P2ActivityScaleReport computeP2ActivityScale(const RCPSP_MIP_Solver &prob);

// P2（活動ごと圧縮）の Pareto フロントを求める。
// 既存の RCPSP_MIP_Solver::solvePareto と同じアルゴリズム構造
// （まず min-makespan、次に無制約 min-cost で上限確定、ε を breakpoints 点
//   サンプリングしながら Stage1(cost最小化)→Stage2(makespan タイトニング)）を
// この定式化向けに独立実装したもの。
std::vector<RealSchedule> solveParetoP2ActivityWise(
        const RCPSP_MIP_Solver &prob,
        const RCPSP_MIP_Solver::Config &cfg);

#endif // RCPSP_MIP_SOLVER_P2_ACTIVITY_H
