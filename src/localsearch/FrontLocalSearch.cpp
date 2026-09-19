#include "FrontLocalSearch.h"
#include "util/Ranking.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================
//  コンストラクタ
// ============================================================
FrontLocalSearch::FrontLocalSearch(RCPSP_Problem *prob, Config cfg)
    : prob_(prob), cfg_(std::move(cfg))
{
    n_    = prob_->getNumJobs();
    nRes_ = prob_->getNumResources();

    // ホライゾン: durations の合計（capacity_t があればその長さも考慮）
    T_ = 0;
    for (int d : prob_->getDurations()) T_ += d;
    const auto &cap_t = prob_->getCapacityT();
    if (!cap_t.empty() && !cap_t[0].empty()) {
        int tvT = static_cast<int>(cap_t[0].size());
        if (tvT > T_) T_ = tvT;
    }
    if (T_ <= 0) T_ = 1;

    // 先行・後続リスト構築
    const auto &successors = prob_->getSuccessors();
    succs_.resize(n_);
    preds_.resize(n_);
    for (int j = 0; j < n_; ++j) {
        for (int s : successors[j]) {
            if (s >= 0 && s < n_) {
                succs_[j].push_back(s);
                preds_[s].push_back(j);
            }
        }
    }
}

// ============================================================
//  逆トポロジカル順序
// ============================================================
std::vector<int> FrontLocalSearch::reverseTopoOrder() const {
    // Kahn's algorithm で正順トポロジカル順序を作り、逆にする
    std::vector<int> indeg(n_, 0);
    for (int j = 0; j < n_; ++j)
        for (int s : succs_[j])
            ++indeg[s];

    std::vector<int> queue;
    queue.reserve(n_);
    for (int j = 0; j < n_; ++j)
        if (indeg[j] == 0) queue.push_back(j);

    std::vector<int> order;
    order.reserve(n_);
    int head = 0;
    while (head < (int)queue.size()) {
        int j = queue[head++];
        order.push_back(j);
        for (int s : succs_[j])
            if (--indeg[s] == 0) queue.push_back(s);
    }

    std::reverse(order.begin(), order.end());
    return order;
}

// ============================================================
//  資源使用テーブル構築
// ============================================================
std::vector<std::vector<int>> FrontLocalSearch::buildUsage(
        const std::vector<int> &startTimes) const
{
    const auto &dur    = prob_->getDurations();
    const auto &demand = prob_->getDemand();

    std::vector<std::vector<int>> usage(nRes_, std::vector<int>(T_, 0));
    for (int j = 0; j < n_; ++j) {
        int d = dur[j];
        if (d <= 0) continue;
        int t0 = startTimes[j];
        for (int tau = t0; tau < t0 + d && tau < T_; ++tau)
            for (int k = 0; k < nRes_; ++k)
                usage[k][tau] += demand[j][k];
    }
    return usage;
}

// ============================================================
//  canPlaceWithout: usage から j を除いた状態で配置可能か
// ============================================================
bool FrontLocalSearch::canPlaceWithout(
        int j, int t,
        const std::vector<std::vector<int>> &usageWithoutJ) const
{
    const auto &dur    = prob_->getDurations();
    const auto &demand = prob_->getDemand();
    const auto &cap    = prob_->getCapacity();
    const auto &cap_t  = prob_->getCapacityT();

    int d = dur[j];
    if (d <= 0) return true;
    if (t < 0 || t + d > T_) return false;

    for (int tau = t; tau < t + d; ++tau) {
        for (int k = 0; k < nRes_; ++k) {
            // capacityAtTime ロジックの再現
            int capAtTau;
            if (!cap_t.empty() && k < (int)cap_t.size()
                && tau < (int)cap_t[k].size()) {
                capAtTau = cap_t[k][tau];
            } else {
                capAtTau = cap[k];
            }
            if (usageWithoutJ[k][tau] + demand[j][k] > capAtTau)
                return false;
        }
    }
    return true;
}

// ============================================================
//  oneOpt: 1 解に対する one-opt 局所探索
// ============================================================
FrontLocalSearch::OneOptResult FrontLocalSearch::oneOpt(
        const std::vector<int> &startTimes, int msLimit)
{
    const auto &dur    = prob_->getDurations();
    const auto &demand = prob_->getDemand();

    OneOptResult result;
    result.startTimes = startTimes;
    result.passes = 0;
    result.moves  = 0;

    // 逆トポロジカル順序で走査
    std::vector<int> order = reverseTopoOrder();

    for (int pass = 0; pass < cfg_.maxPasses; ++pass) {
        ++result.passes;
        bool improved = false;

        // 全活動の資源使用テーブルを構築
        auto usage = buildUsage(result.startTimes);

        for (int j : order) {
            int d = dur[j];
            if (d <= 0) continue;  // ダミー端点スキップ

            int curStart = result.startTimes[j];

            // j を usage から除去
            for (int tau = curStart; tau < curStart + d && tau < T_; ++tau)
                for (int k = 0; k < nRes_; ++k)
                    usage[k][tau] -= demand[j][k];

            // 先行制約による最早
            int eLo = 0;
            for (int p : preds_[j])
                eLo = std::max(eLo, result.startTimes[p] + dur[p]);

            // 後続制約による最遅
            int eHi = msLimit - d;  // makespan 上限
            for (int s : succs_[j])
                eHi = std::min(eHi, result.startTimes[s] - d);
            eHi = std::min(eHi, T_ - d);

            if (eLo > eHi) {
                // 移動不可 → j を元に戻す
                for (int tau = curStart; tau < curStart + d && tau < T_; ++tau)
                    for (int k = 0; k < nRes_; ++k)
                        usage[k][tau] += demand[j][k];
                continue;
            }

            // [eLo, eHi] で argmin(cost) かつ資源制約 OK
            int    bestT = curStart;
            double bestC = prob_->computeJobCostAt(j, curStart, T_);

            for (int t = eLo; t <= eHi; ++t) {
                if (!canPlaceWithout(j, t, usage)) continue;
                double c = prob_->computeJobCostAt(j, t, T_);
                if (c < bestC) {  // strict '<' → 左詰め保証
                    bestC = c;
                    bestT = t;
                }
            }

            // j を bestT に配置
            result.startTimes[j] = bestT;
            for (int tau = bestT; tau < bestT + d && tau < T_; ++tau)
                for (int k = 0; k < nRes_; ++k)
                    usage[k][tau] += demand[j][k];

            if (bestT != curStart) {
                ++result.moves;
                improved = true;
            }
        }

        if (!improved) break;
    }

    // makespan と totalCost を再計算
    result.makespan = 0;
    result.cost     = 0.0;
    for (int j = 0; j < n_; ++j) {
        int d = dur[j];
        int fin = result.startTimes[j] + d;
        if (fin > result.makespan) result.makespan = fin;
        if (d > 0)
            result.cost += prob_->computeJobCostAt(j, result.startTimes[j], T_);
    }

    return result;
}

// ============================================================
//  apply: フロント全体に局所探索を適用
// ============================================================
SolutionSet* FrontLocalSearch::apply(SolutionSet *front,
                                      std::vector<SolStat> *stats)
{
    const int frontSize = front->size();
    const auto &dur = prob_->getDurations();

    // 全候補を集める（元フロント + 改善解）
    SolutionSet *candidates = new SolutionSet(
            frontSize * (1 + (int)cfg_.deltas.size()) * 2);

    // 元フロントをそのまま追加
    for (int i = 0; i < frontSize; ++i)
        candidates->add(new Solution(front->get(i)));

    // 各フロント解 × 各 Δ で局所探索
    for (int i = 0; i < frontSize; ++i) {
        Solution *origSol = front->get(i);
        double origMs   = origSol->getObjective(0);
        double origCost = origSol->getObjective(1);

        if (origSol->startTimes_.empty()) continue;
        if (origMs >= 1e8) continue;  // フォールバック解は除外

        for (int delta : cfg_.deltas) {
            int msLimit = (int)origMs + delta;

            OneOptResult res = oneOpt(origSol->startTimes_, msLimit);

            // 改善チェック: Δ=0 なら makespan 不変 & コスト非増加を保証
            // Δ>0 なら makespan が delta 以内で増えてコストが下がった場合のみ採用
            bool accept = false;
            if (delta == 0) {
                // makespan 不変、コストが下がった場合のみ
                accept = (res.cost < origCost - 1e-9);
            } else {
                // makespan が origMs + delta 以下、コストが下がった場合
                accept = (res.makespan <= origMs + delta + 1e-9
                          && res.cost < origCost - 1e-9);
            }

            if (stats) {
                SolStat st;
                st.origIdx       = i;
                st.origMakespan  = origMs;
                st.origCost      = origCost;
                st.delta         = delta;
                st.newMakespan   = res.makespan;
                st.newCost       = res.cost;
                st.costReduction = origCost - res.cost;
                st.passesUsed    = res.passes;
                st.movesApplied  = res.moves;
                stats->push_back(st);
            }

            if (accept) {
                // 新しい Solution を作成
                Solution *newSol = new Solution(origSol);
                newSol->startTimes_ = res.startTimes;

                // execSlots_ を更新
                newSol->execSlots_.resize(n_);
                for (int j = 0; j < n_; ++j) {
                    int dj = dur[j];
                    newSol->execSlots_[j].clear();
                    for (int t = 0; t < dj; ++t)
                        newSol->execSlots_[j].push_back(res.startTimes[j] + t);
                }

                newSol->setObjective(0, res.makespan);
                newSol->setObjective(1, res.cost);

                candidates->add(newSol);
            }
        }
    }

    // 非支配抽出
    SolutionSet *enhanced = new SolutionSet(candidates->size());
    {
        Ranking ranking(candidates);
        if (ranking.getNumberOfSubfronts() > 0) {
            SolutionSet *f0 = ranking.getSubfront(0);
            for (int i = 0; i < f0->size(); ++i)
                enhanced->add(new Solution(f0->get(i)));
        }
    }
    delete candidates;

    return enhanced;
}

// ============================================================
//  writeLog: 局所探索ログ CSV 出力
// ============================================================
void FrontLocalSearch::writeLog(const std::string &path,
                                const std::vector<SolStat> &stats,
                                bool truncate)
{
    // ディレクトリ作成
    fs::path p(path);
    if (p.has_parent_path())
        fs::create_directories(p.parent_path());

    std::ofstream ofs(path, truncate ? std::ios::trunc : std::ios::app);
    if (!ofs.is_open()) return;

    if (truncate) {
        ofs << "orig_idx,delta,orig_makespan,orig_cost,"
               "new_makespan,new_cost,cost_reduction,"
               "passes_used,moves_applied\n";
    }

    for (const auto &s : stats) {
        ofs << s.origIdx << "," << s.delta << ","
            << s.origMakespan << "," << s.origCost << ","
            << s.newMakespan << "," << s.newCost << ","
            << s.costReduction << ","
            << s.passesUsed << "," << s.movesApplied << "\n";
    }
}
