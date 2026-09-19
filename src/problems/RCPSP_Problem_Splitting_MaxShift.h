#pragma once
#ifndef RCPSP_PROBLEM_SPLITTING_MAXSHIFT_H
#define RCPSP_PROBLEM_SPLITTING_MAXSHIFT_H

#include "RCPSP_Problem_MaxShift.h"
#include "RCPSP_Problem_Splitting.h"  // ActivitySplittingMode のみ使用
#include <string>
#include <vector>

// ============================================================
//  RCPSP_Problem_Splitting_MaxShift
//
//  MaxShift エンコーディング（vars[n..2n-1] = max_shift 整数値）と
//  P2/P3 作業分割スケジューリングを組み合わせたクラス。
//
//  vars[0..n-1]  : 活動リスト（RCPSP_Problem_MaxShift と同じ）
//  vars[n..2n-1] : max_shift 値（整数, [0, getEffectiveHalfT()]）
//
//  スケジューリング:
//    max_shift[j] == 0 → makespan 優先（貪欲配置）
//    max_shift[j]  > 0 → コスト優先（[EST, EST+max_shift[j]] で P2/P3 探索）
//
//  P2 モード: 資源休暇時のみ中断許可（非先取り分割）
//  P3 モード: 任意時刻で中断・再開許可（先取り分割）
// ============================================================
class RCPSP_Problem_Splitting_MaxShift : public RCPSP_Problem_MaxShift {
public:
    // ----------------------------------------------------------------
    //  コンストラクタ
    //   mode     : P2 または P3
    //   strategy : max_shift 上限戦略（1=0, 2=T/8, 3=T/4, 4=T/2）
    //   rr / rv  : 時間依存容量パラメータ
    // ----------------------------------------------------------------
    explicit RCPSP_Problem_Splitting_MaxShift(
            const std::string    &filename,
            ActivitySplittingMode mode,
            int                   strategy = 4,
            double                rr       = 0.0,
            bool                  rv       = false);

    // evaluate() をオーバーライドして P2/P3 + MaxShift スケジュール生成を実装
    void evaluate(Solution *solution) override;

    ActivitySplittingMode getSplittingMode() const { return mode_; }

private:
    ActivitySplittingMode mode_;

    // ----------------------------------------------------------------
    //  P2 ヘルパー（RCPSP_Problem_Splitting と同一ロジック）
    // ----------------------------------------------------------------
    struct P2Result {
        int    firstExec = -1;
        int    lastExec  = -1;
        double cost      = 0.0;
        bool   feasible  = false;
        std::vector<int> slots;
    };

    P2Result simulateP2(int j, int S_j, int T,
                        const std::vector<std::vector<int>> &usage) const;

    P2Result executeP2(int j, int S_j, int T,
                       std::vector<std::vector<int>> &usage) const;
};

#endif // RCPSP_PROBLEM_SPLITTING_MAXSHIFT_H
