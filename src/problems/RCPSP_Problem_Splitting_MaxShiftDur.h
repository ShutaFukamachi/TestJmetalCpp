#pragma once
#ifndef RCPSP_PROBLEM_SPLITTING_MAXSHIFTDUR_H
#define RCPSP_PROBLEM_SPLITTING_MAXSHIFTDUR_H

#include "RCPSP_Problem_MaxShiftDur.h"
#include "RCPSP_Problem_Splitting.h"  // ActivitySplittingMode のみ使用
#include <string>
#include <vector>

// ============================================================
//  RCPSP_Problem_Splitting_MaxShiftDur
//
//  MaxShiftDur エンコーディング（所要時間ベース遅延スケーリング）と
//  P2/P3 作業分割スケジューリングを組み合わせたクラス。
//
//  変数配置:
//    vars[0 .. n-1]   : 活動リスト（先行制約を満たす順列）
//    vars[n .. 2n-1]  : 正規化遅延比率キー ρ_j ∈ [0, R]（R=1000）
//
//  スケジューリング（P2/P3 + duration-scaled window）:
//    rho_j   = vars[n+j] / (double)R
//    eff_j   = max(alpha * d_j, beta * T)
//    win_j   = round(rho_j * eff_j)           --- MaxShiftDur と同じ窓幅計算
//    win_j == 0 → makespan 優先（P2/P3 貪欲）
//    win_j  > 0 → コスト優先（[EST, EST+win_j] 窓で P2/P3 探索）
//
//  P2: 資源休暇時のみ中断許可（non-preemptive splitting）
//  P3: 任意時刻で中断・再開許可（preemptive splitting）
// ============================================================
class RCPSP_Problem_Splitting_MaxShiftDur : public RCPSP_Problem_MaxShiftDur {
public:
    explicit RCPSP_Problem_Splitting_MaxShiftDur(
            const std::string    &filename,
            ActivitySplittingMode mode,
            int                   strategy = 3,
            double                rr       = 0.0,
            bool                  rv       = false);

    void evaluate(Solution *solution) override;

    ActivitySplittingMode getSplittingMode() const { return mode_; }

private:
    ActivitySplittingMode mode_;

    // ---- P2 ヘルパー（RCPSP_Problem_Splitting_MaxShift と同一ロジック）----
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

#endif // RCPSP_PROBLEM_SPLITTING_MAXSHIFTDUR_H
