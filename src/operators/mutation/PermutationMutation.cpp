#include "PermutationMutation.h"
#include "RCPSP_Problem.h"
#include <random>
#include <vector>
#include <algorithm>

// ============================================================
// Boctor (1996) の挿入変異
//
// 第1段階: 活動リストの挿入変異 (確率 probability = 1/n 毎ジョブ)
//   ジョブ j をリストから除いた後、
//     lo = j の直接先行ジョブの最後の位置 (in 除いたリスト)
//     hi = j の直接後続ジョブの最初の位置 (in 除いたリスト)
//   [lo+1, hi] の範囲からランダムに選んで挿入
//   → 先行制約を必ず満たした位置にしか移動しない
//
// 第2段階: schedObj のビット反転 (確率 probability = 1/n 毎ジョブ)
//   0 → 1 または 1 → 0 に反転
// ============================================================

void * PermutationMutation::execute(void * object) {
    Solution * s = (Solution *)object;
    const int nVars = s->getNumberOfVariables();
    const int nJobs = nVars / 2;

    if (nJobs <= 2) return s;

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution<> prob01(0.0, 1.0);

    auto &vars = s->getVars();

    // 現在の活動リストを取得
    std::vector<int> seq(nJobs);
    for (int i = 0; i < nJobs; ++i) seq[i] = vars[i];

    // ========================================================
    // 第1段階: 挿入変異
    //   ダミー開始 (job 0) とダミー終了 (job nJobs-1) は変異しない
    // ========================================================
    for (int j = 1; j < nJobs - 1; ++j) {
        if (prob01(gen) >= probability) continue;

        // seq 内での job j の現在位置
        int cur_pos = -1;
        for (int i = 0; i < nJobs; ++i) {
            if (seq[i] == j) { cur_pos = i; break; }
        }
        if (cur_pos < 0) continue;

        // job j をリストから除いた列を作成
        std::vector<int> tmp;
        tmp.reserve(nJobs - 1);
        for (int i = 0; i < nJobs; ++i) {
            if (i != cur_pos) tmp.push_back(seq[i]);
        }

        // tmp 上での各ジョブの位置を計算
        std::vector<int> pos_in_tmp(nJobs, -1);
        for (int i = 0; i < (int)tmp.size(); ++i) {
            pos_in_tmp[tmp[i]] = i;
        }

        // lo: j の直接先行ジョブの最後の位置
        // hi: j の直接後続ジョブの最初の位置
        int lo = 0;  // ダミー開始は必ず先行
        for (int p : preds_[j]) {
            int pp = pos_in_tmp[p];
            if (pp >= 0 && pp > lo) lo = pp;
        }

        int hi = (int)tmp.size();  // 挿入位置の上限 = リスト末尾の直後
        for (int succ : succs_[j]) {
            int sp = pos_in_tmp[succ];
            if (sp >= 0 && sp < hi) hi = sp;
        }

        // 有効な挿入範囲: [lo+1, hi]
        //   挿入位置 p に対して:
        //     p >= lo+1 → j は先行ジョブの後に来る
        //     p <= hi   → j は後続ジョブの前に来る
        if (lo + 1 > hi) continue;  // 有効範囲なし

        std::uniform_int_distribution<> ins_dis(lo + 1, hi);
        int insert_pos = ins_dis(gen);

        // tmp の insert_pos に job j を挿入
        tmp.insert(tmp.begin() + insert_pos, j);

        // seq を更新
        seq = tmp;
    }

    // 変異後の活動リストを変数に書き戻す
    for (int i = 0; i < nJobs; ++i) vars[i] = seq[i];

    // ========================================================
    // 第2段階: schedObj のビット反転
    //   ダミー端点は変異しない
    // ========================================================
    for (int j = 1; j < nJobs - 1; ++j) {
        if (prob01(gen) < probability) {
            int idx = nJobs + j;
            int v = vars[idx];
            vars[idx] = v ? 0 : 1;
        }
    }

    return s;
}
