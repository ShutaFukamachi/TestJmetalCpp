#include "MaxShiftSegMutation.h"
#include <random>
#include <vector>
#include <algorithm>

void * MaxShiftSegMutation::execute(void * object) {
    Solution *s = (Solution *)object;
    const int nVars = s->getNumberOfVariables();
    const int nJobs = nVars / 2;

    if (nJobs <= 2) return s;

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution<> prob01(0.0, 1.0);

    auto &vars = s->getVars();

    // ========================================================
    // 第1段階: 活動リストの挿入変異
    // ========================================================
    std::vector<int> seq(nJobs);
    for (int i = 0; i < nJobs; ++i) seq[i] = vars[i];

    for (int j = 1; j < nJobs - 1; ++j) {
        if (prob01(gen) >= probability) continue;

        int cur_pos = -1;
        for (int i = 0; i < nJobs; ++i)
            if (seq[i] == j) { cur_pos = i; break; }
        if (cur_pos < 0) continue;

        std::vector<int> tmp;
        tmp.reserve(nJobs - 1);
        for (int i = 0; i < nJobs; ++i)
            if (i != cur_pos) tmp.push_back(seq[i]);

        std::vector<int> pos_in_tmp(nJobs, -1);
        for (int i = 0; i < (int)tmp.size(); ++i)
            pos_in_tmp[tmp[i]] = i;

        int lo = 0;
        for (int p : preds_[j]) {
            int pp = pos_in_tmp[p];
            if (pp >= 0 && pp > lo) lo = pp;
        }

        int hi = (int)tmp.size();
        for (int succ : succs_[j]) {
            int sp = pos_in_tmp[succ];
            if (sp >= 0 && sp < hi) hi = sp;
        }

        if (lo + 1 > hi) continue;

        std::uniform_int_distribution<> ins_dis(lo + 1, hi);
        int insert_pos = ins_dis(gen);
        tmp.insert(tmp.begin() + insert_pos, j);
        seq = tmp;
    }

    for (int i = 0; i < nJobs; ++i) vars[i] = seq[i];

    // ========================================================
    // 第2段階: rho キーの変異 [0, R]
    // ========================================================
    static constexpr int R = RCPSP_Problem_MaxShiftSeg::R;
    std::uniform_int_distribution<int> rho_dist(0, R);

    for (int j = 1; j < nJobs - 1; ++j) {
        if (prob01(gen) < probability) {
            if (prob01(gen) < zeroResetProb) {
                vars[nJobs + j] = 0;
            } else {
                vars[nJobs + j] = rho_dist(gen);
            }
        }
    }

    return s;
}
