#pragma once
#include "Operator.h"
#include "Solution.h"
#include "problems/RCPSP_Problem_MaxShiftDur.h"
#include <vector>
#include <stdexcept>

// ============================================================
//  MaxShiftDurMutation
//
//  RCPSP_Problem_MaxShiftDur 用の変異オペレータ。
//
//  MaxShiftMutation と同一の 2 段階ロジックだが、
//  rho キーの再サンプリング上限を halfT（ホライゾン依存）ではなく
//  R=1000（比率全域）に固定する点が異なる。
//
//  第1段階: 活動リストの挿入変異（MaxShiftMutation と同一）
//    確率 probability = 1/n 毎ジョブ:
//      ジョブ j を先行制約合法範囲 [lo+1, hi] のランダム位置に再挿入
//
//  第2段階: rho キーの変異（比率空間 [0, R]）
//    確率 probability = 1/n 毎ジョブ:
//      さらに確率 zeroResetProb (デフォルト 0.40) で key を 0 にリセット
//      残り (1 - zeroResetProb) は Uniform[0, R] から再サンプリング
//    ダミー端点 (j=0, j=n-1) は変異しない
//
//  注: MaxShiftMutation は RCPSP_Problem_MaxShift::getEffectiveHalfT()
//      を呼んで halfT を決定するが、getEffectiveHalfT() は virtual でないため
//      RCPSP_Problem_MaxShiftDur を渡してもホライゾン依存値が返る。
//      このクラスは R を直接参照することでその問題を回避する。
// ============================================================
class MaxShiftDurMutation : public Operator {
    std::vector<std::vector<int>> succs_;
    std::vector<std::vector<int>> preds_;

    static void build_lists(RCPSP_Problem_MaxShiftDur *prob,
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
    double zeroResetProb = 0.40;  // key を 0 にリセットする確率

    MaxShiftDurMutation(double p, RCPSP_Problem_MaxShiftDur *prob)
        : probability(p)
    {
        if (!prob)
            throw std::runtime_error("MaxShiftDurMutation: null problem");
        build_lists(prob, succs_, preds_);
    }

    void * execute(void * object) override;
};
