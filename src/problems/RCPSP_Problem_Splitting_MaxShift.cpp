#include "RCPSP_Problem_Splitting_MaxShift.h"
#include "Solution.h"

#include <algorithm>
#include <limits>
#include <iostream>
#include <vector>
#include <queue>

// ============================================================
//  コンストラクタ
// ============================================================
RCPSP_Problem_Splitting_MaxShift::RCPSP_Problem_Splitting_MaxShift(
        const std::string    &filename,
        ActivitySplittingMode mode,
        int                   strategy,
        double                rr,
        bool                  rv)
    : RCPSP_Problem_MaxShift(filename, strategy, rr, rv)
    , mode_(mode)
{
    const char *modeStr = (mode == ActivitySplittingMode::P2) ? "P2" : "P3";
    std::cout << "[RCPSP_Problem_Splitting_MaxShift] mode=" << modeStr << "\n";
}

// ============================================================
//  simulateP2（読み取り専用ドライラン）
//  RCPSP_Problem_Splitting::simulateP2 と同一ロジック
// ============================================================
RCPSP_Problem_Splitting_MaxShift::P2Result
RCPSP_Problem_Splitting_MaxShift::simulateP2(
        int j, int S_j, int T,
        const std::vector<std::vector<int>> &usage) const
{
    int d    = instance.duration[j];
    int nRes = instance.nRes;

    std::vector<int> execSlots;
    execSlots.reserve(d);
    double cost = 0.0;

    for (int t = S_j; t < T && (int)execSlots.size() < d; ++t) {
        // γ チェック: U_kt < demand → RR/RV 由来の容量不足 → P2 スキップ許容
        bool gammaOk = true;
        for (int k = 0; k < nRes; ++k) {
            if (capacityAtTime(k, t) < instance.demand[j][k]) { gammaOk = false; break; }
        }
        if (!gammaOk) continue;

        // 残余容量チェック
        bool residualOk = true;
        for (int k = 0; k < nRes; ++k) {
            if (usage[k][t] + instance.demand[j][k] > capacityAtTime(k, t)) {
                residualOk = false; break;
            }
        }

        if (residualOk) {
            execSlots.push_back(t);
            cost += computeSlotCost(j, t, T);
        } else {
            // γ=1 残余不足 → リスタート
            int restartT = execSlots.empty() ? t : execSlots[0];
            execSlots.clear();
            cost = 0.0;
            t = restartT;
        }
    }

    if ((int)execSlots.size() < d) return {-1, -1, 0.0, false, {}};
    return {execSlots.front(), execSlots.back(), cost, true, {}};
}

// ============================================================
//  executeP2（usage を更新する）
// ============================================================
RCPSP_Problem_Splitting_MaxShift::P2Result
RCPSP_Problem_Splitting_MaxShift::executeP2(
        int j, int S_j, int T,
        std::vector<std::vector<int>> &usage) const
{
    int d    = instance.duration[j];
    int nRes = instance.nRes;

    std::vector<int> execSlots;
    execSlots.reserve(d);
    double cost = 0.0;

    for (int t = S_j; t < T && (int)execSlots.size() < d; ++t) {
        bool gammaOk = true;
        for (int k = 0; k < nRes; ++k) {
            if (capacityAtTime(k, t) < instance.demand[j][k]) { gammaOk = false; break; }
        }
        if (!gammaOk) continue;

        bool residualOk = true;
        for (int k = 0; k < nRes; ++k) {
            if (usage[k][t] + instance.demand[j][k] > capacityAtTime(k, t)) {
                residualOk = false; break;
            }
        }

        if (residualOk) {
            for (int k = 0; k < nRes; ++k) usage[k][t] += instance.demand[j][k];
            execSlots.push_back(t);
            cost += computeSlotCost(j, t, T);
        } else {
            int restartT = execSlots.empty() ? t : execSlots[0];
            for (int s : execSlots)
                for (int k = 0; k < nRes; ++k) usage[k][s] -= instance.demand[j][k];
            execSlots.clear();
            cost = 0.0;
            t = restartT;
        }
    }

    if ((int)execSlots.size() < d) {
        for (int s : execSlots)
            for (int k = 0; k < nRes; ++k) usage[k][s] -= instance.demand[j][k];
        return {-1, -1, 0.0, false, {}};
    }
    return {execSlots.front(), execSlots.back(), cost, true, execSlots};
}

// ============================================================
//  evaluate
//
//  エンコーディング:
//    vars[0..n-1]  : 活動リスト（MaxShift と同じ）
//    vars[n..2n-1] : max_shift 値（整数）
//      0   → makespan 優先（P2/P3 貪欲）
//      > 0 → コスト優先（[EST, EST+max_shift] 窓で P2/P3 探索）
// ============================================================
void RCPSP_Problem_Splitting_MaxShift::evaluate(Solution *solution) {
    ++evalCounter_;

    const int n    = getNumJobs();
    const int nRes = instance.nRes;

    auto &vars = solution->getVars();

    // ---- 1. 活動リスト取得・修復 ----
    std::vector<int> seq(n);
    for (int i = 0; i < n; ++i) seq[i] = vars[i];

    if (!checkTopological(seq)) {
        seq = topoRepair(seq, instance.successors, n);
        for (int i = 0; i < n; ++i) vars[i] = seq[i];
    }

    // ---- 2. ホライゾン T ----
    const int T     = getHorizon();
    const int halfT = getEffectiveHalfT();

    // ---- 3. max_shift 取得 ----
    std::vector<int> maxShift(n, 0);
    if (outputMaxShift_ >= 0) {
        std::fill(maxShift.begin(), maxShift.end(), outputMaxShift_);
    } else {
        for (int j = 0; j < n; ++j) {
            int v = vars[n + j];
            maxShift[j] = std::max(0, std::min(halfT, v));
        }
    }
    if (n > 0) maxShift[0]     = 0;
    if (n > 1) maxShift[n - 1] = 0;

    // ---- 4. 先行リスト構築 ----
    std::vector<std::vector<int>> preds(n);
    for (int j = 0; j < n; ++j)
        for (int s : instance.successors[j])
            if (s >= 0 && s < n) preds[s].push_back(j);

    // ---- 5. 資源使用量テーブル ----
    std::vector<std::vector<int>> usage(nRes, std::vector<int>(T, 0));
    std::vector<int> startArr(n, 0);
    std::vector<int> finish(n, 0);

    solution->execSlots_.assign(n, {});
    double totalCost = 0.0;

    // ---- 6. メインループ ----
    for (int pos = 0; pos < n; ++pos) {
        int j = seq[pos];
        int d = instance.duration[j];

        if (d <= 0) {
            startArr[j] = finish[j] = 0;
            continue;
        }

        // EST: 先行ジョブの最大完了時刻
        int est = 0;
        for (int p : preds[j]) est = std::max(est, finish[p]);

        // ============================================================
        //  P2 配置ロジック
        //    max_shift[j] == 0: makespan 優先（EST から貪欲 P2）
        //    max_shift[j]  > 0: コスト優先（[EST, EST+maxShift] で最安 S_j を探索）
        // ============================================================
        if (mode_ == ActivitySplittingMode::P2) {

            if (maxShift[j] == 0) {
                P2Result res = executeP2(j, est, T, usage);
                if (!res.feasible) {
                    solution->setObjective(0, 1e9);
                    solution->setObjective(1, 1e9);
                    return;
                }
                startArr[j] = res.firstExec;
                finish[j]   = res.lastExec + 1;
                totalCost  += res.cost;
                solution->execSlots_[j] = std::move(res.slots);

            } else {
                int    latest   = std::min(T - d, est + maxShift[j]);
                double bestCost = std::numeric_limits<double>::infinity();
                int    bestSj   = est;

                for (int sj = est; sj <= latest; ++sj) {
                    P2Result sim = simulateP2(j, sj, T, usage);
                    if (sim.feasible && sim.cost < bestCost) {
                        bestCost = sim.cost;
                        bestSj   = sj;
                    }
                }

                P2Result res = executeP2(j, bestSj, T, usage);
                if (!res.feasible) {
                    res = executeP2(j, est, T, usage);
                    if (!res.feasible) {
                        solution->setObjective(0, 1e9);
                        solution->setObjective(1, 1e9);
                        return;
                    }
                }
                startArr[j] = res.firstExec;
                finish[j]   = res.lastExec + 1;
                totalCost  += res.cost;
                solution->execSlots_[j] = std::move(res.slots);
            }

        }
        // ============================================================
        //  P3 配置ロジック
        //    max_shift[j] == 0: makespan 優先（EST から貪欲 P3）
        //    max_shift[j]  > 0: コスト優先（[EST, EST+maxShift+d] で安い d スロット選択）
        // ============================================================
        else { // P3

            if (maxShift[j] == 0) {
                // 貪欲 P3: 資源があれば即実行（任意中断・再開可能）
                int    executed  = 0;
                int    firstExec = -1;
                int    lastExec  = -1;
                double cost      = 0.0;
                std::vector<int> p3Slots;

                for (int t = est; t < T && executed < d; ++t) {
                    bool ok = true;
                    for (int k = 0; k < nRes; ++k) {
                        if (usage[k][t] + instance.demand[j][k] > capacityAtTime(k, t)) {
                            ok = false; break;
                        }
                    }
                    if (ok) {
                        for (int k = 0; k < nRes; ++k) usage[k][t] += instance.demand[j][k];
                        if (firstExec == -1) firstExec = t;
                        lastExec = t;
                        cost += computeSlotCost(j, t, T);
                        p3Slots.push_back(t);
                        ++executed;
                    }
                }

                if (executed < d) {
                    solution->setObjective(0, 1e9);
                    solution->setObjective(1, 1e9);
                    return;
                }
                startArr[j] = firstExec;
                finish[j]   = lastExec + 1;
                totalCost  += cost;
                solution->execSlots_[j] = std::move(p3Slots);

            } else {
                // コスト優先 P3: [EST, EST+maxShift+d] から安い d スロットを選択
                int windowEnd = std::min(T - 1, est + maxShift[j] + d);

                std::vector<std::pair<double, int>> candidates;
                candidates.reserve(windowEnd - est + 1);

                for (int t = est; t <= windowEnd; ++t) {
                    bool canExec = true;
                    for (int k = 0; k < nRes; ++k) {
                        int cap = capacityAtTime(k, t);
                        if (cap == 0 || usage[k][t] + instance.demand[j][k] > cap) {
                            canExec = false; break;
                        }
                    }
                    if (canExec) {
                        candidates.push_back({computeSlotCost(j, t, T), t});
                    }
                }

                if ((int)candidates.size() >= d) {
                    std::partial_sort(candidates.begin(), candidates.begin() + d,
                                      candidates.end());

                    std::vector<int> selected;
                    selected.reserve(d);
                    double slotCostSum = 0.0;
                    for (int i = 0; i < d; ++i) {
                        selected.push_back(candidates[i].second);
                        slotCostSum += candidates[i].first;
                    }
                    std::sort(selected.begin(), selected.end());

                    for (int t : selected)
                        for (int k = 0; k < nRes; ++k)
                            usage[k][t] += instance.demand[j][k];

                    startArr[j] = selected.front();
                    finish[j]   = selected.back() + 1;
                    totalCost  += slotCostSum;
                    solution->execSlots_[j] = std::move(selected);

                } else {
                    // 候補スロット不足 → 貪欲 P3 フォールバック
                    int    executed  = 0;
                    int    firstExec = -1;
                    int    lastExec  = -1;
                    double cost      = 0.0;
                    std::vector<int> p3Slots;

                    for (int t = est; t < T && executed < d; ++t) {
                        bool ok = true;
                        for (int k = 0; k < nRes; ++k) {
                            if (usage[k][t] + instance.demand[j][k] > capacityAtTime(k, t)) {
                                ok = false; break;
                            }
                        }
                        if (ok) {
                            for (int k = 0; k < nRes; ++k) usage[k][t] += instance.demand[j][k];
                            if (firstExec == -1) firstExec = t;
                            lastExec = t;
                            cost += computeSlotCost(j, t, T);
                            p3Slots.push_back(t);
                            ++executed;
                        }
                    }

                    if (executed < d) {
                        solution->setObjective(0, 1e9);
                        solution->setObjective(1, 1e9);
                        return;
                    }
                    startArr[j] = firstExec;
                    finish[j]   = lastExec + 1;
                    totalCost  += cost;
                    solution->execSlots_[j] = std::move(p3Slots);
                }
            }
        }
    }

    // ---- 7. makespan ----
    int makespan = 0;
    for (int j = 0; j < n; ++j) makespan = std::max(makespan, finish[j]);

    // ---- 8. キャッシュ更新 ----
    solution->startTimes_ = startArr;
    solution->setObjective(0, (double)makespan);
    solution->setObjective(1, totalCost);
}
