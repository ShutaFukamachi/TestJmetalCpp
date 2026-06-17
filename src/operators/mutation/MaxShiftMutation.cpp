#include "MaxShiftMutation.h"
#include <random>
#include <vector>
#include <algorithm>
#include <cmath>

// ============================================================
//  MaxShiftMutation::execute
//
//  第1段階: 活動リストの挿入変異（PermutationMutation と同一）
//  第2段階: max_shift へのガウスノイズ加算
//    new_shift_j = clip( round( shift_j + N(0, sigma_) ), 0, halfT_ )
// ============================================================
void * MaxShiftMutation::execute(void * object) {
    Solution *s = (Solution *)object;
    const int nVars = s->getNumberOfVariables();
    const int nJobs = nVars / 2;

    if (nJobs <= 2) return s;

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution<> prob01(0.0, 1.0);

    auto &vars = s->getVars();

    // ========================================================
    // 第1段階: 活動リストの挿入変異
    //   ダミー開始 (job 0) とダミー終了 (job nJobs-1) は変異しない
    // ========================================================
    std::vector<int> seq(nJobs);
    for (int i = 0; i < nJobs; ++i) seq[i] = vars[i];

    for (int j = 1; j < nJobs - 1; ++j) {
        if (prob01(gen) >= probability) continue;

        // seq 内での job j の現在位置1
        int cur_pos = -1;
        for (int i = 0; i < nJobs; ++i) {
            if (seq[i] == j) { cur_pos = i; break; }
        }
        if (cur_pos < 0) continue;

        // job j をリストから除いた列
        std::vector<int> tmp;
        tmp.reserve(nJobs - 1);
        for (int i = 0; i < nJobs; ++i)
            if (i != cur_pos) tmp.push_back(seq[i]);

        // tmp 上での各ジョブの位置
        std::vector<int> pos_in_tmp(nJobs, -1);
        for (int i = 0; i < (int)tmp.size(); ++i)
            pos_in_tmp[tmp[i]] = i;

        // lo: j の直接先行ジョブの最後の位置
        int lo = 0;
        for (int p : preds_[j]) {
            int pp = pos_in_tmp[p];
            if (pp >= 0 && pp > lo) lo = pp;
        }

        // hi: j の直接後続ジョブの最初の位置
        int hi = (int)tmp.size();
        for (int succ : succs_[j]) {
            int sp = pos_in_tmp[succ];
            if (sp >= 0 && sp < hi) hi = sp;
        }

        if (lo + 1 > hi) continue;  // 有効範囲なし

        std::uniform_int_distribution<> ins_dis(lo + 1, hi);
        int insert_pos = ins_dis(gen);
        tmp.insert(tmp.begin() + insert_pos, j);
        seq = tmp;
    }

    // 変異後の活動リストを書き戻す
    for (int i = 0; i < nJobs; ++i) vars[i] = seq[i];

    // ========================================================
    // 第2段階: max_shift の変異（ゼロリセット付き）
    //   ダミー端点は変異しない
    //   確率 probability で変異発生 → さらに:
    //     zeroResetProb (70%) : max_shift を 0 にリセット
    //     残り (30%)          : Uniform[0, T/4] から再サンプリング
    //   ※ zeroResetProb の実際の値は MaxShiftMutation.h の
    //     メンバ変数初期値 (= 0.70) が適用される。
    // ========================================================
    std::uniform_int_distribution<int> ms_dist(0, halfT_);
    for (int j = 1; j < nJobs - 1; ++j) {
        if (prob01(gen) < probability) {
            if (prob01(gen) < zeroResetProb) {
                vars[nJobs + j] = 0;
            } else {
                vars[nJobs + j] = ms_dist(gen);
            }
        }
    }

    return s;
}
