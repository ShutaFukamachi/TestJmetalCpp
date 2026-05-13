#pragma once
#include "Operator.h"
#include "Solution.h"
#include "problems/RCPSP_Problem_MaxShift.h"
#include <vector>
#include <stdexcept>

// ============================================================
//  MaxShiftMutation
//
//  RCPSP_Problem_MaxShift 用の変異オペレータ。
//
//  第1段階: 活動リストの挿入変異（PermutationMutation と同一）
//    確率 probability = 1/n 毎ジョブ:
//      ジョブ j を除いたリスト上で
//        lo = j の直接先行ジョブの最後の位置
//        hi = j の直接後続ジョブの最初の位置
//      [lo+1, hi] のランダムな位置に j を挿入
//      → 先行制約を常に満たした位置にしか移動しない
//
//  第2段階: max_shift の変異（ゼロリセット付き）
//    確率 probability = 1/n 毎ジョブ:
//      さらに確率 zeroResetProb (デフォルト 0.30) で max_shift を 0 にリセット
//      残り (1 - zeroResetProb) は Uniform[0, T/4] から再サンプリング
//    ダミー端点 (j=0, j=n-1) は変異しない
// ============================================================
class MaxShiftMutation : public Operator {
    std::vector<std::vector<int>> succs_;  // ジョブ j の直接後続
    std::vector<std::vector<int>> preds_;  // ジョブ j の直接先行
    int halfT_;   // max_shift の上限 = T/2

    static void build_lists(RCPSP_Problem_MaxShift *prob,
                            std::vector<std::vector<int>> &succs,
                            std::vector<std::vector<int>> &preds) {
        int n = prob->getNumJobs();
        const auto &succ_list = prob->getSuccessors();
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
    double zeroResetProb = 0.70;  // max_shift 変異時にゼロリセットする確率（調整可）

    MaxShiftMutation(double p, RCPSP_Problem_MaxShift *prob)
        : probability(p)
    {
        if (!prob)
            throw std::runtime_error("MaxShiftMutation: null problem");
        build_lists(prob, succs_, preds_);
        int T = prob->getHorizon();
        halfT_ = std::max(1, T / 4);
    }

    void * execute(void * object) override;
};
