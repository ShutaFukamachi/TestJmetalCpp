#pragma once
#include "Operator.h"
#include "Solution.h"
#include "RCPSP_Problem.h"
#include <vector>
#include <stdexcept>

// ============================================================
// Boctor (1996) の挿入変異 + schedObj ビット反転
//
// 活動リスト突然変異 (確率 1/n 毎ジョブ):
//   ジョブ j を除いたリスト上で
//     lo = j の直接先行ジョブの最後の位置
//     hi = j の直接後続ジョブの最初の位置
//   [lo+1, hi] のランダムな位置に j を挿入
//
// schedObj 突然変異 (確率 1/n 毎ジョブ):
//   0→1 または 1→0 に反転
// ============================================================

class PermutationMutation : public Operator {
    std::vector<std::vector<int>> succs_;  // 直接後続 (job j の immediate successors)
    std::vector<std::vector<int>> preds_;  // 直接先行 (job j の immediate predecessors)

    static void build_lists(RCPSP_Problem *rcpsp,
                            std::vector<std::vector<int>> &succs,
                            std::vector<std::vector<int>> &preds) {
        int n = rcpsp->getNumJobs();
        const auto &succ_list = rcpsp->getSuccessors();
        succs.assign(n, {});
        preds.assign(n, {});
        for (int j = 0; j < n; ++j) {
            for (int s : succ_list[j]) {
                if (s >= 0 && s < n) {
                    succs[j].push_back(s);
                    preds[s].push_back(j);
                }
            }
        }
    }

public:
    double probability;

    PermutationMutation(double p, Problem *problem) : probability(p) {
        RCPSP_Problem *rcpsp = dynamic_cast<RCPSP_Problem *>(problem);
        if (!rcpsp)
            throw std::runtime_error("PermutationMutation: Problem is not RCPSP_Problem");
        build_lists(rcpsp, succs_, preds_);
    }

    virtual void * execute(void * object);
};
