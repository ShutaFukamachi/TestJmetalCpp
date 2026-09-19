// ============================================================
//  main_NSGAII_RCPSP_MaxShiftSeg.cpp  (NSGAMaxShiftSegCpp ターゲット)
//
//  MaxShiftSeg エンコーディング（セグメント選択型デコード）の
//  NSGA-II ランナー。P1 のみ対応。
//
//  main_NSGAII_RCPSP_MaxShiftDur.cpp と同一の
//    - インスタンスリスト
//    - 実行条件（popSize=100, evalsPerStrategy=50000, 4 Strategy）
//    - 8 RR/RV 条件
//    - 出力ファイル形式（FUN_ENC_* / SCHED_ENC_*）
//  で実行することで、既存エンコーディングと直接比較可能。
//
//  出力ファイル:
//    FUN_ENC_<prefix>_<cond>_MaxShiftSeg.txt
//    SCHED_ENC_<prefix>_<cond>_MaxShiftSeg.txt
//
//  使い方:
//    NSGAMaxShiftSegCpp                      # 全インスタンス一括
//    NSGAMaxShiftSegCpp j30.sm/j301_1.sm     # 単一インスタンス
// ============================================================

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#include "core/Algorithm.h"
#include "core/Problem.h"
#include "core/SolutionSet.h"
#include "Solution.h"
#include "metaheuristics/nsgaII/NSGAII.h"
#include "operators/crossover/MaxShiftCrossover.h"
#include "operators/mutation/MaxShiftSegMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "problems/RCPSP_Problem.h"
#include "problems/RCPSP_Problem_MaxShiftSeg.h"
#include "problems/RCPSP_Conditions.h"
#include "util/Ranking.h"

using namespace std;

// ============================================================
//  インスタンスリスト（main.cpp と同一）
// ============================================================
static const vector<string> ALL_INSTANCES = {
    "j30.sm/j3011_1.sm", "j30.sm/j3017_1.sm", "j30.sm/j3026_1.sm",
    "j30.sm/j3047_1.sm", "j30.sm/j309_1.sm",
};

// ============================================================
//  ユーティリティ
// ============================================================
static std::mutex g_printMutex;

static void syncPrint(const string &msg) {
    lock_guard<mutex> lk(g_printMutex);
    cout << msg;
}

static string toCondTag(double rr, bool rv) {
    int rrInt = static_cast<int>(round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

static string toBaseNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

// ============================================================
//  オペレータ設定
// ============================================================
static void attachMaxShiftSegOps(Algorithm *algo,
                                  RCPSP_Problem_MaxShiftSeg *prob) {
    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob->getNumberOfVariables();
    algo->addOperator("crossover", new MaxShiftCrossover(crossP));
    algo->addOperator("mutation",  new MaxShiftSegMutation(mutP, prob));
    map<string, void*> sel;
    algo->addOperator("selection", new BinaryTournament2(sel));
}

// ============================================================
//  FUN / SCHED ファイル出力
// ============================================================
static void writeResultFiles(const string &outPrefix,
                              SolutionSet  *pareto,
                              RCPSP_Problem *prob)
{
    const string encDir = "results/FUN_ENC/" + prob->getInstancePrefix() + "/";
    fs::create_directories(encDir);
    const string funPath   = encDir + "FUN_ENC_"   + outPrefix + ".txt";
    const string schedPath = encDir + "SCHED_ENC_" + outPrefix + ".txt";

    ofstream funFile(funPath.c_str());
    ofstream schedFile(schedPath.c_str());

    int nJobs = prob->getNumJobs();
    int nRes  = prob->getNumResources();

    schedFile << nJobs << " " << nRes << "\n";
    {
        const auto &dur = prob->getDurations();
        for (int j = 0; j < nJobs; ++j) {
            schedFile << dur[j];
            if (j + 1 < nJobs) schedFile << " ";
        }
        schedFile << "\n";

        const auto &demand = prob->getDemand();
        for (int j = 0; j < nJobs; ++j) {
            for (int k = 0; k < nRes; ++k) {
                schedFile << demand[j][k];
                if (k + 1 < nRes) schedFile << " ";
            }
            schedFile << "\n";
        }

        const auto &cap = prob->getCapacity();
        for (int k = 0; k < nRes; ++k) {
            schedFile << cap[k];
            if (k + 1 < nRes) schedFile << " ";
        }
        schedFile << "\n";

        const auto &cap_t = prob->getCapacityT();
        if (cap_t.empty() || cap_t[0].empty()) {
            schedFile << "0\n";
        } else {
            int T_cap = (int)cap_t[0].size();
            schedFile << T_cap << "\n";
            for (int k = 0; k < nRes; ++k) {
                for (int t = 0; t < T_cap; ++t) {
                    schedFile << cap_t[k][t];
                    if (t + 1 < T_cap) schedFile << " ";
                }
                schedFile << "\n";
            }
        }
    }

    schedFile << pareto->size() << "\n";
    for (int i = 0; i < pareto->size(); ++i) {
        Solution *sol = pareto->get(i);
        funFile << sol->getObjective(0) << " " << sol->getObjective(1) << "\n";
        schedFile << sol->getObjective(0) << " " << sol->getObjective(1);
        const vector<int> st = prob->computeStartTimes(sol);
        for (int j = 0; j < nJobs; ++j)
            schedFile << " " << (j < (int)st.size() ? st[j] : 0);
        schedFile << "\n";
    }
}

// ============================================================
//  P1: 4 Strategy 並列実行 → Pareto フロント返却
// ============================================================
static SolutionSet* runStrategiesP1(
        const string &instanceFile,
        double rr, bool rv,
        int populationSize, int evalsPerStrategy,
        int numStrategies,
        double beta, int numSegments)
{
    using StratResult = pair<SolutionSet*, RCPSP_Problem*>;
    vector<future<StratResult>> futures;
    futures.reserve(numStrategies);

    for (int s = 1; s <= numStrategies; ++s) {
        futures.push_back(async(launch::async,
            [instanceFile, rr, rv, s, numStrategies,
             populationSize, evalsPerStrategy, beta, numSegments]()
            -> StratResult
        {
            auto *prob = new RCPSP_Problem_MaxShiftSeg(instanceFile, s, rr, rv);
            prob->setBeta(beta);
            prob->setNumSegments(numSegments);
            prob->setMaxEvaluations(evalsPerStrategy);
            prob->resetEvalCounter();

            syncPrint("  [MaxShiftSeg-P1 S" + to_string(s)
                      + "/" + to_string(numStrategies) + "]\n");

            Algorithm *algo = new NSGAII(prob);
            int popSz  = populationSize;
            int maxEv  = evalsPerStrategy;
            int lsFlag = 0;
            algo->setInputParameter("populationSize", &popSz);
            algo->setInputParameter("maxEvaluations", &maxEv);
            algo->setInputParameter("useLocalSearch",  &lsFlag);

            attachMaxShiftSegOps(algo, prob);

            SolutionSet *pop = algo->execute();

            SolutionSet *result = new SolutionSet(populationSize * 4);
            {
                Ranking ranking(pop);
                if (ranking.getNumberOfSubfronts() > 0) {
                    SolutionSet *f0 = ranking.getSubfront(0);
                    syncPrint("    [P1 S" + to_string(s)
                              + "] PF=" + to_string(f0->size()) + "\n");
                    for (int i = 0; i < f0->size(); ++i)
                        result->add(new Solution(f0->get(i)));
                }
            }
            delete pop;
            delete algo;
            return {result, prob};
        }));
    }

    SolutionSet *combined = new SolutionSet(numStrategies * populationSize * 4);
    vector<RCPSP_Problem*> probsToDelete;
    probsToDelete.reserve(numStrategies);

    for (int s = 0; s < numStrategies; ++s) {
        auto [stratResult, prob] = futures[s].get();
        for (int i = 0; i < stratResult->size(); ++i)
            combined->add(new Solution(stratResult->get(i)));
        delete stratResult;
        probsToDelete.push_back(prob);
    }

    SolutionSet *finalPareto = new SolutionSet(combined->size());
    {
        Ranking finalR(combined);
        if (finalR.getNumberOfSubfronts() > 0) {
            SolutionSet *f0 = finalR.getSubfront(0);
            for (int i = 0; i < f0->size(); ++i)
                finalPareto->add(new Solution(f0->get(i)));
        }
    }
    delete combined;
    for (auto *p : probsToDelete) delete p;

    syncPrint("  [MaxShiftSeg-P1 DONE] PF=" + to_string(finalPareto->size()) + "\n");
    return finalPareto;
}

// ============================================================
//  1インスタンス × 8条件 を実行（P1 のみ）
// ============================================================
static void runInstance(const string &instanceFile,
                        int    populationSize    = 100,
                        int    evalsPerStrategy  = 50000,
                        int    numStrategies     = 4,
                        double beta              = 0.5,
                        int    numSegments       = 8)
{
    const string prefix = toBaseNoExt(instanceFile);

    using Cond = RCPSP_Cond;
    // 比較対象は RR<=0.50（6条件）。RR=0.75 は限界条件用に RCPSP_Conditions.h で別枠管理
    // （再有効化する場合は RCPSP_ALL_CONDITIONS() に差し替える）。
    const vector<Cond> conditions = RCPSP_STANDARD_CONDITIONS();

    cout << "\n============================================================\n";
    cout << " MaxShiftSeg (P1)  instance=" << instanceFile << "\n";
    cout << " popSize=" << populationSize
         << "  evalsPerStrategy=" << evalsPerStrategy
         << "  numStrategies=" << numStrategies
         << "  beta=" << beta
         << "  K=" << numSegments << "\n";
    cout << "============================================================\n";

    cout << "\n" << left  << setw(16) << "Condition"
                 << right << setw(14) << "min_makespan"
                          << setw(14) << "min_cost"
                          << setw(10) << "|PF|" << "\n";
    cout << string(54, '-') << "\n";

    for (const auto &c : conditions) {
        RCPSP_Problem::resetGlobalCostSeries();
        const string ctag = toCondTag(c.rr, c.rv);

        SolutionSet *pareto = runStrategiesP1(
                instanceFile, c.rr, c.rv,
                populationSize, evalsPerStrategy, numStrategies,
                beta, numSegments);

        RCPSP_Problem_MaxShiftSeg probOut(instanceFile, 1, c.rr, c.rv);
        probOut.setBeta(beta);
        probOut.setNumSegments(numSegments);
        const string outPrefix = prefix + "_" + ctag + "_MaxShiftSeg";
        writeResultFiles(outPrefix, pareto, &probOut);

        double minMs = 1e9, minCost = 1e9;
        for (int i = 0; i < pareto->size(); ++i) {
            minMs   = min(minMs,   pareto->get(i)->getObjective(0));
            minCost = min(minCost, pareto->get(i)->getObjective(1));
        }
        cout << left  << setw(16) << ctag
             << right << setw(14) << (minMs < 1e8 ? to_string((int)minMs) : "---")
                      << setw(14) << fixed << setprecision(1) << minCost
                      << setw(10) << pareto->size() << "\n";
        delete pareto;

        cout << string(54, '-') << "\n";
    }
}

// ============================================================
//  退化チェック（α=0 で全キー=0 → ESS と一致するか）
// ============================================================
static void verifyDegenerate(const string &instanceFile) {
    cout << "\n[Degenerate Check] alpha=0 (S1), all keys=0 == ESS\n";

    RCPSP_Problem probBase(instanceFile);
    RCPSP_Problem_MaxShiftSeg probSeg(instanceFile, 1);  // S1: alpha=0

    Solution *solBase = probBase.createRandomTopoSolution();
    probBase.setOutputMaxShift(0);
    probBase.evaluate(solBase);

    const int n    = probSeg.getNumJobs();
    const int nVar = probSeg.getNumberOfVariables();
    Solution *solSeg = new Solution(&probSeg);
    {
        auto &dv       = solSeg->getVars();
        const auto &bv = solBase->getVars();
        for (int i = 0; i < n; ++i) dv[i] = bv[i];
        for (int j = 0; j < n && n + j < nVar; ++j) dv[n + j] = 0;
    }
    probSeg.evaluate(solSeg);

    bool ok = (std::abs(solSeg->getObjective(0) - solBase->getObjective(0)) < 1e-6 &&
               std::abs(solSeg->getObjective(1) - solBase->getObjective(1)) < 1e-4);
    cout << "  [" << (ok ? "PASS" : "FAIL") << "]"
         << "  ESS: ms=" << (int)solBase->getObjective(0)
         << " cost=" << fixed << setprecision(0) << solBase->getObjective(1)
         << "  Seg(S1,key=0): ms=" << (int)solSeg->getObjective(0)
         << " cost=" << solSeg->getObjective(1) << "\n";

    // 制約検証
    bool schedOk = probSeg.verifySchedule(solSeg);
    cout << "  [Schedule verify: " << (schedOk ? "PASS" : "FAIL") << "]\n";

    delete solBase;
    delete solSeg;
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        const int    populationSize   = 100;
        const int    evalsPerStrategy = 50000;
        const int    numStrategies    = 4;
        const double beta             = 0.5;
        const int    numSegments      = 8;

        if (argc >= 2) {
            const string instanceFile = argv[1];
            cout << "\n[SINGLE] " << instanceFile << "\n";
            verifyDegenerate(instanceFile);
            runInstance(instanceFile, populationSize, evalsPerStrategy,
                        numStrategies, beta, numSegments);
        } else {
            const int total = static_cast<int>(ALL_INSTANCES.size());
            cout << "\n[BATCH] MaxShiftSeg (P1)  " << total << " instances"
                 << "  beta=" << beta << "  K=" << numSegments << "\n";

            if (!ALL_INSTANCES.empty())
                verifyDegenerate(ALL_INSTANCES[0]);

            auto t_all = chrono::steady_clock::now();
            for (int i = 0; i < total; ++i) {
                const string &inst = ALL_INSTANCES[i];
                cout << "\n[" << (i+1) << "/" << total << "] " << inst << "\n";
                RCPSP_Problem::resetGlobalCostSeries();

                auto t0 = chrono::steady_clock::now();
                runInstance(inst, populationSize, evalsPerStrategy,
                            numStrategies, beta, numSegments);
                double elapsed = chrono::duration<double>(
                        chrono::steady_clock::now() - t0).count();
                cout << "[TIME] " << toBaseNoExt(inst)
                     << "  " << fixed << setprecision(1) << elapsed << " s\n";
            }

            double total_elapsed = chrono::duration<double>(
                    chrono::steady_clock::now() - t_all).count();
            cout << "\n[BATCH DONE]  total=" << total_elapsed << " s"
                 << "  (" << fixed << setprecision(2)
                 << total_elapsed / 3600.0 << " h)\n";
        }
        return 0;

    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
