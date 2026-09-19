#pragma once
#include "Operator.h"
#include "Solution.h"
#include "problems/RCPSP_Problem_MaxShiftSeg.h"
#include <vector>
#include <stdexcept>

// ============================================================
//  MaxShiftSegMutation
//
//  RCPSP_Problem_MaxShiftSeg 用の変異オペレータ。
//  MaxShiftDurMutation と同一ロジック（rho キー [0, R] の再サンプリング）。
//  型制約のみ異なる（MaxShiftSeg* を受け付ける）。
// ============================================================
class MaxShiftSegMutation : public Operator {
    std::vector<std::vector<int>> succs_;
    std::vector<std::vector<int>> preds_;

    static void build_lists(RCPSP_Problem_MaxShiftSeg *prob,
                            std::vector<std::vector<int>> &succs,
                            std::vector<std::vector<int>> &preds) {
        int n = prob->getNumJobs();
        const auto &succ_list = prob->getSuccessors();
        succs.assign(n, {});
        preds.assign(n, {});
        for (int j = 0; j < n; ++j)
            for (int s : succ_list[j])
                if (s >= 0 && s < n) {
                    succs[j].push_back(s);
                    preds[s].push_back(j);
                }
    }

public:
    double probability;
    double zeroResetProb = 0.40;

    MaxShiftSegMutation(double p, RCPSP_Problem_MaxShiftSeg *prob)
        : probability(p)
    {
        if (!prob)
            throw std::runtime_error("MaxShiftSegMutation: null problem");
        build_lists(prob, succs_, preds_);
    }

    void * execute(void * object) override;
};
