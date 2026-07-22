// ============================================================
//  main_A3_AB.cpp  —  A3 ランダム選抜シードの A/B 比較ハーネス
//
//  目的: A3（ランダム2000順列の上位1%を初期集団へ注入）が
//        最終パレートフロントの makespan (ms*) を実際に改善するかを測る。
//
//  方法: MaxShift エンコーディング P1、4戦略並列の combined front を
//        A3 ON / OFF それぞれ R 試行実行し、ms* と cost* の分布を比較。
//
//  使い方: A3AB [instance] [rr] [trials] [evalsPerStrategy]
//     例:  A3AB j30.sm/j309_1.sm 0.50 6 100000
// ============================================================
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "core/Algorithm.h"
#include "core/SolutionSet.h"
#include "Solution.h"
#include "metaheuristics/nsgaII/NSGAII.h"
#include "problems/RCPSP_Problem_MaxShift.h"
#include "operators/crossover/MaxShiftCrossover.h"
#include "operators/mutation/MaxShiftMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"

using namespace std;

struct RunResult {
    double minMs   = 1e18;  // combined front の最小 makespan
    double costAtMinMs = 1e18;  // その ms* を達成した解のコスト
    double minCost = 1e18;  // combined front の最小コスト
    int    frontSize = 0;
    int    precViol  = 0;   // ms* 解の実ジョブ間 先行制約違反数
    int    resViol   = 0;   // ms* 解の 資源制約違反数
};

// ms* 解の実行可能性を検証（実ジョブのみ: ダミー端点 d<=0 は無視）
// prec: 実ジョブ i→s で start[i]+dur[i] > start[s] の数
// res : 時刻ごとの資源使用が capacityAtTime を超える (k,t) の数
static void checkFeasibility(RCPSP_Problem_MaxShift *prob,
                             const vector<int> &start,
                             int &precViol, int &resViol)
{
    const int n    = prob->getNumJobs();
    const int nRes = prob->getNumResources();
    const auto &dur    = prob->getDurations();
    const auto &dem    = prob->getDemand();
    const auto &cap    = prob->getCapacity();
    const auto &capT   = prob->getCapacityT();
    const auto &succ   = prob->getSuccessors();
    const int T = prob->getHorizon();
    auto capAt = [&](int k, int t) -> int {
        if (!capT.empty() && !capT[k].empty() && t < (int)capT[k].size())
            return capT[k][t];
        return cap[k];
    };
    precViol = 0; resViol = 0;
    if ((int)start.size() < n) { precViol = -1; return; }
    // 先行制約（両端が実ジョブのみ）
    for (int i = 0; i < n; ++i) {
        if (dur[i] <= 0) continue;
        for (int s : succ[i]) {
            if (s < 0 || s >= n || dur[s] <= 0) continue;
            if (start[i] + dur[i] > start[s]) ++precViol;
        }
    }
    // 資源制約
    vector<vector<int>> usage(nRes, vector<int>(T, 0));
    for (int j = 0; j < n; ++j) {
        if (dur[j] <= 0) continue;
        for (int t = start[j]; t < start[j] + dur[j] && t < T; ++t)
            for (int k = 0; k < nRes; ++k) usage[k][t] += dem[j][k];
    }
    for (int t = 0; t < T; ++t)
        for (int k = 0; k < nRes; ++k)
            if (usage[k][t] > capAt(k, t)) ++resViol;
}

// 1 回分（4戦略 combined front）を実行して ms*/cost* を返す
RunResult runOnce(const string &instFile, double rr, bool rv,
                  int popSize, int evalsPerStrategy, int numStr,
                  int a1flag)
{
    using StratResult = pair<SolutionSet*, RCPSP_Problem_MaxShift*>;
    vector<future<StratResult>> futures;
    futures.reserve(numStr);

    for (int s = 1; s <= numStr; ++s) {
        futures.push_back(async(launch::async,
            [=]() -> StratResult {
                auto *prob = new RCPSP_Problem_MaxShift(instFile, s, rr, rv);
                prob->resetEvalCounter();

                Algorithm *algo = new NSGAII(prob);
                int popSz  = popSize;
                int maxEv  = evalsPerStrategy;
                int lsFlag = 0;
                int a1     = a1flag;
                algo->setInputParameter("populationSize", &popSz);
                algo->setInputParameter("maxEvaluations", &maxEv);
                algo->setInputParameter("useLocalSearch",  &lsFlag);
                algo->setInputParameter("a1PrioritySeed",   &a1);

                double crossP = 0.9;
                double mutP   = 1.0 / (double)prob->getNumberOfVariables();
                algo->addOperator("crossover", new MaxShiftCrossover(crossP));
                algo->addOperator("mutation",  new MaxShiftMutation(mutP, prob));
                map<string, void*> sel;
                algo->addOperator("selection", new BinaryTournament2(sel));

                SolutionSet *pop = algo->execute();

                SolutionSet *result = new SolutionSet(popSize * 4);
                Ranking ranking(pop);
                if (ranking.getNumberOfSubfronts() > 0) {
                    SolutionSet *f0 = ranking.getSubfront(0);
                    for (int i = 0; i < f0->size(); ++i)
                        result->add(new Solution(f0->get(i)));
                }
                delete pop;
                delete algo;
                return {result, prob};  // prob は combined 生成後に delete
            }));
    }

    SolutionSet *combined = new SolutionSet(numStr * popSize * 4);
    vector<RCPSP_Problem_MaxShift*> probs;
    for (int s = 0; s < numStr; ++s) {
        auto [res, prob] = futures[s].get();
        for (int i = 0; i < res->size(); ++i)
            combined->add(new Solution(res->get(i)));
        delete res;
        probs.push_back(prob);
    }

    RunResult rr_out;
    Ranking finalR(combined);
    Solution *minMsSol = nullptr;
    if (finalR.getNumberOfSubfronts() > 0) {
        SolutionSet *f0 = finalR.getSubfront(0);
        rr_out.frontSize = f0->size();
        for (int i = 0; i < f0->size(); ++i) {
            double ms   = f0->get(i)->getObjective(0);
            double cost = f0->get(i)->getObjective(1);
            if (ms >= 1e8) continue;  // 実行不能解を除外
            if (ms < rr_out.minMs) {
                rr_out.minMs = ms; rr_out.costAtMinMs = cost;
                minMsSol = f0->get(i);
            }
            rr_out.minCost = min(rr_out.minCost, cost);
        }
    }
    // ms* 解の実行可能性を検証（probs[0] は同一インスタンスなので流用可）
    if (minMsSol && !probs.empty())
        checkFeasibility(probs[0], minMsSol->startTimes_, rr_out.precViol, rr_out.resViol);

    delete combined;
    for (auto *p : probs) delete p;
    return rr_out;
}

int main(int argc, char **argv) {
    string instFile        = (argc >= 2) ? argv[1] : "j30.sm/j309_1.sm";
    double rr              = (argc >= 3) ? atof(argv[2]) : 0.50;
    int    trials          = (argc >= 4) ? atoi(argv[3]) : 6;
    int    evalsPerStrategy= (argc >= 5) ? atoi(argv[4]) : 100000;
    const bool rv          = false;
    const int  popSize     = 100;
    const int  numStr      = 4;

    cout << "=== Baseline vs A1 (priority-rule seed) A/B test ===\n";
    cout << "instance=" << instFile << "  rr=" << rr << "  rv=" << rv
         << "  trials=" << trials << "  evals/strategy=" << evalsPerStrategy
         << "  popSize=" << popSize << "  strategies=" << numStr << "\n\n";

    // 条件: {name, a1flag}
    // （旧 A3/FBI 条件は不採用のため削除。残るのは OFF と A1 priority-rule のみ）
    struct Cond { const char *name; int a1; };
    const vector<Cond> conds = {
        {"OFF (baseline)", 0},
        {"A1  (priority-rule)", 1},
    };

    vector<vector<RunResult>> results(conds.size());
    auto t0 = chrono::steady_clock::now();

    for (size_t c = 0; c < conds.size(); ++c) {
        cout << "---- " << conds[c].name << " ----\n";
        for (int t = 0; t < trials; ++t) {
            RunResult r = runOnce(instFile, rr, rv, popSize,
                                  evalsPerStrategy, numStr,
                                  conds[c].a1);
            cout << "  trial " << (t + 1) << ": ms*=" << (int)r.minMs
                 << "  cost@ms*=" << fixed << setprecision(0) << r.costAtMinMs
                 << "  minCost=" << r.minCost
                 << "  frontSize=" << r.frontSize
                 << "  [feas prec=" << r.precViol << " res=" << r.resViol << "]"
                 << (r.precViol == 0 && r.resViol == 0 ? "" : "  <<< INFEASIBLE")
                 << "\n";
            results[c].push_back(r);
        }
        cout << "\n";
    }

    auto summarize = [](const vector<RunResult> &v) {
        double best = 1e18, sum = 0, worst = -1e18;
        for (const auto &r : v) {
            best  = min(best,  r.minMs);
            worst = max(worst, r.minMs);
            sum  += r.minMs;
        }
        double mean = v.empty() ? 0 : sum / (double)v.size();
        return make_tuple(best, mean, worst);
    };

    cout << "================ SUMMARY (ms*) ================\n";
    cout << fixed << setprecision(2);
    cout << "condition                 best     mean    worst\n";
    for (size_t c = 0; c < conds.size(); ++c) {
        auto [b, m, w] = summarize(results[c]);
        cout << left << setw(22) << conds[c].name << right
             << "  " << setw(6) << b << "  " << setw(6) << m
             << "  " << setw(6) << w << "\n";
    }
    cout << "(lower ms* = better; MIP optimum for j309_1 RR050 = 125)\n";

    double elapsed = chrono::duration<double>(
        chrono::steady_clock::now() - t0).count();
    cout << "[TIME] " << elapsed << " s\n";
    return 0;
}
