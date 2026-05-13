#pragma once
#include "Operator.h"
#include "Solution.h"
#include <vector>

// ============================================================
//  MaxShiftCrossover
//
//  RCPSP_Problem_MaxShift 用の Hartmann (1998) 2 点交叉。
//
//  活動リスト:
//    PermutationCrossover と同一の Hartmann 2 点交叉
//    （k1, k2 を乱数で決め 3 ブロックに分割して組み換え）
//
//  max_shift リスト:
//    各ジョブを「どの親から提供されたか」に応じて
//    そのジョブに対応する親の max_shift 値を継承する。
//    （PermutationCrossover の schedObj 継承と完全に同じ仕組み）
// ============================================================
class MaxShiftCrossover : public Operator {
public:
    double probability;

    explicit MaxShiftCrossover(double p) : probability(p) {}

    void * execute(void * object) override;
};
