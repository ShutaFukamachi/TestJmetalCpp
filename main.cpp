#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <stdexcept>
#include <cmath>

#include "core/Problem.h"
#include "core/Algorithm.h"
#include "core/SolutionSet.h"
#include "Solution.h"
#include "Variable.h"
#include "metaheuristics/nsgaII/NSGAII.h"
#include "problems/RCPSP_Problem.h"
#include "problems/RCPSP_Problem_Splitting.h"
#include "operators/crossover/PermutationCrossover.h"
#include "operators/mutation/PermutationMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"

using namespace std;

// ============================================================
//  ユーティリティ
// ============================================================

static bool fileExists(const string &p) {
    ifstream f(p.c_str(), ios::binary);
    return (bool)f;
}

static void copyFileBinary(const string &src, const string &dst) {
    ifstream in(src.c_str(), ios::binary);
    if (!in) throw runtime_error("Cannot open: " + src);
    ofstream out(dst.c_str(), ios::binary);
    if (!out) throw runtime_error("Cannot open: " + dst);
    out << in.rdbuf();
}

static string baseNameNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

static string detectSizeTag(const string &path) {
    if (path.find("j30")  != string::npos) return "j30";
    if (path.find("j60")  != string::npos) return "j60";
    if (path.find("j90")  != string::npos) return "j90";
    if (path.find("j120") != string::npos) return "j120";
    return "unknown";
}

static string conditionTag(double rr, bool rv) {
    int rrInt = static_cast<int>(std::round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

static string modeTag(ActivitySplittingMode mode) {
    switch (mode) {
        case ActivitySplittingMode::P1: return "P1";
        case ActivitySplittingMode::P2: return "P2";
        case ActivitySplittingMode::P3: return "P3";
    }
    return "P1";
}

// ============================================================
//  結果出力ヘルパー
// ============================================================
static void writeResults(const string &funPath,
                         const string &varPath,
                         const string &schedPath,
                         SolutionSet  *pareto,
                         RCPSP_Problem *rcpsp)
{
    ofstream funFile(funPath.c_str());
    ofstream varFile(varPath.c_str());
    ofstream schedFile(schedPath.c_str());

    int nJobs = rcpsp ? rcpsp->getNumJobs()          : 0;
    int nRes  = rcpsp ? rcpsp->getNumResources()      : 0;
    int nVar  = rcpsp ? rcpsp->getNumberOfVariables() : 0;

    // SCHEDヘッダー
    schedFile << nJobs << " " << nRes << "\n";
    if (rcpsp) {
        const auto &dur = rcpsp->getDurations();
        for (int j = 0; j < nJobs; ++j) {
            schedFile << dur[j];
            if (j + 1 < nJobs) schedFile << " ";
        }
        schedFile << "\n";

        const auto &demand = rcpsp->getDemand();
        for (int j = 0; j < nJobs; ++j) {
            for (int k = 0; k < nRes; ++k) {
                schedFile << demand[j][k];
                if (k + 1 < nRes) schedFile << " ";
            }
            schedFile << "\n";
        }

        const auto &cap = rcpsp->getCapacity();
        for (int k = 0; k < nRes; ++k) {
            schedFile << cap[k];
            if (k + 1 < nRes) schedFile << " ";
        }
        schedFile << "\n";

        const auto &cap_t = rcpsp->getCapacityT();
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

    // min_makespan 解のインデックス
    int minMakespanIdx = 0;
    double minMs = pareto->get(0)->getObjective(0);
    for (int i = 1; i < pareto->size(); ++i) {
        double ms = pareto->get(i)->getObjective(0);
        if (ms < minMs) { minMs = ms; minMakespanIdx = i; }
    }

    schedFile << pareto->size() << "\n";

    for (int i = 0; i < pareto->size(); ++i) {
        Solution *sol = pareto->get(i);
        Variable **vars = sol->getDecisionVariables();

        // min_makespan 解は ESS 再評価（開始時刻をコンパクトにする）
        if (i == minMakespanIdx && rcpsp) {
            rcpsp->setOutputMaxShift(0);
            rcpsp->evaluate(sol);
            rcpsp->setOutputMaxShift(-1);
        }

        funFile << sol->getObjective(0) << " " << sol->getObjective(1) << "\n";

        for (int j = 0; j < nVar; ++j) {
            varFile << vars[j]->getValue();
            if (j + 1 < nVar) varFile << " ";
        }
        varFile << "\n";

        schedFile << sol->getObjective(0) << " " << sol->getObjective(1);
        if (rcpsp) {
            vector<int> st = rcpsp->computeStartTimes(sol);
            for (int j = 0; j < nJobs; ++j) schedFile << " " << st[j];
        }
        schedFile << "\n";
    }
}

// ============================================================
//  1インスタンス × 1条件 × 1モード (P1/P2/P3) の NSGA-II 実行
//
//  戦略 1〜4 を独立実行し、第1パレートフロントを統合、
//  最終的に非支配フィルタを適用した結果を出力する。
// ============================================================
static void runNSGA(const string &instanceFile,
                    const string &tagPrefix,
                    double rr,
                    bool   rv,
                    ActivitySplittingMode mode)
{
    const string ctag = conditionTag(rr, rv);
    const string mtag = modeTag(mode);

    cout << "============================================\n";
    cout << "[RUN] " << tagPrefix << "  " << ctag << "  mode=" << mtag << "\n";
    cout << "  instance : " << instanceFile << "\n";
    cout << "  RR=" << rr << "  RV=" << (rv ? 1 : 0) << "\n";
    cout << "============================================\n";

    // ---- パラメータ ----
    int populationSize   = 100;
    int evalsPerStrategy = 200000;
    const int numStrategies = 4;

    // ---- 問題インスタンス生成 ----
    // P1: 基底クラス RCPSP_Problem をそのまま使う（分割なし）
    // P2/P3: RCPSP_Problem_Splitting を使う
    RCPSP_Problem *problem = nullptr;
    if (mode == ActivitySplittingMode::P1) {
        problem = new RCPSP_Problem(instanceFile, /*strategy=*/1, rr, rv);
    } else {
        problem = new RCPSP_Problem_Splitting(instanceFile, mode, /*strategy=*/1, rr, rv);
    }
    problem->setMaxEvaluations(evalsPerStrategy);

    // ---- 各戦略を独立実行 ----
    SolutionSet *combined = new SolutionSet(numStrategies * populationSize * 4);

    for (int strategy = 1; strategy <= numStrategies; ++strategy) {
        cout << "\n  [" << mtag << " Strategy " << strategy << "/" << numStrategies
             << "]  maxEvals=" << evalsPerStrategy << "\n";

        problem->setStrategy(strategy);
        problem->resetEvalCounter();
        problem->clearStartTimesCache();

        Algorithm *algorithm = new NSGAII(problem);
        algorithm->setInputParameter("populationSize", &populationSize);
        algorithm->setInputParameter("maxEvaluations", &evalsPerStrategy);

        int lsFlag = 0;
        algorithm->setInputParameter("useLocalSearch", &lsFlag);

        double crossoverProbability = 0.9;
        Operator *crossover = new PermutationCrossover(crossoverProbability);

        double mutationProbability = 1.0 / (double)problem->getNumberOfVariables();
        Operator *mutation  = new PermutationMutation(mutationProbability, problem);

        map<string, void*> selParams;
        Operator *selection = new BinaryTournament2(selParams);

        algorithm->addOperator("crossover", crossover);
        algorithm->addOperator("mutation",  mutation);
        algorithm->addOperator("selection", selection);

        SolutionSet *population = algorithm->execute();

        {
            Ranking ranking(population);
            if (ranking.getNumberOfSubfronts() > 0) {
                SolutionSet *front0 = ranking.getSubfront(0);
                cout << "  [" << mtag << " Strategy " << strategy
                     << "] Pareto front size: " << front0->size() << "\n";
                for (int i = 0; i < front0->size(); ++i) {
                    combined->add(new Solution(front0->get(i)));
                }
            }
        }

        delete population;
        delete algorithm;
    }

    // ---- 統合結果に非支配フィルタを適用 ----
    SolutionSet *finalPareto = new SolutionSet(combined->size());
    {
        Ranking finalRanking(combined);
        if (finalRanking.getNumberOfSubfronts() > 0) {
            SolutionSet *front0 = finalRanking.getSubfront(0);
            for (int i = 0; i < front0->size(); ++i) {
                finalPareto->add(new Solution(front0->get(i)));
            }
        }
    }
    delete combined;

    // ---- 結果出力 ----
    //  ファイル名形式: FUN_<instanceId>_<condTag>_<modeTag>
    const string funPath   = "FUN_"   + tagPrefix + "_" + ctag + "_" + mtag;
    const string varPath   = "VAR_"   + tagPrefix + "_" + ctag + "_" + mtag;
    const string schedPath = "SCHED_" + tagPrefix + "_" + ctag + "_" + mtag;

    writeResults(funPath, varPath, schedPath, finalPareto, problem);

    cout << "[DONE] " << funPath
         << "  (Final Pareto size=" << finalPareto->size() << ")\n\n";

    delete finalPareto;
    delete problem;
}

// ============================================================
//  1インスタンスに対して全 RR/RV 条件 × P1/P2/P3 を実行
// ============================================================
static void runAllConditionsAndModes(const string &instanceFile) {
    const string prefix  = baseNameNoExt(instanceFile);
    const string sizeTag = detectSizeTag(instanceFile);

    if (sizeTag == "unknown") {
        throw runtime_error("Cannot detect sizeTag from path: " + instanceFile);
    }

    // コストファイルの準備
    const string costsFile = "costs_" + prefix + ".csv";
    if (fileExists(costsFile)) {
        copyFileBinary(costsFile, "costs.csv");
        cout << "[COST] " << costsFile << " -> costs.csv\n";
    } else {
        cout << "[COST] " << costsFile << " not found. Will be auto-generated.\n";
    }
    RCPSP_Problem::resetGlobalCostSeries();

    cout << "\n========================================\n";
    cout << " Instance: " << prefix << " (" << sizeTag << ")\n";
    cout << "========================================\n";

    struct Cond { double rr; bool rv; };
    const vector<Cond> conditions = {
        {0.00, false},
        {0.00, true },
        {0.25, false},
        {0.25, true },
        {0.50, false},
        {0.50, true },
        {0.75, false},
        {0.75, true },
    };

    // P1 / P2 / P3 の 3 モード
    const vector<ActivitySplittingMode> modes = {
        ActivitySplittingMode::P1,
        ActivitySplittingMode::P2,
        ActivitySplittingMode::P3,
    };

    for (const auto &c : conditions) {
        // 各条件でコストテーブルを共有するためにリセット
        RCPSP_Problem::resetGlobalCostSeries();
        if (fileExists(costsFile)) copyFileBinary(costsFile, "costs.csv");

        for (auto m : modes) {
            runNSGA(instanceFile, prefix, c.rr, c.rv, m);
        }
    }

    cout << "[BATCH] Instance " << prefix << " all conditions/modes done.\n\n";
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        if (argc >= 2) {
            // コマンドライン引数でインスタンス指定
            runAllConditionsAndModes(argv[1]);
            return 0;
        }

        // ---- バッチ実行 ----
        vector<string> instances;

        // 動作確認用: j301_1.sm のみ実行
        instances.push_back(string("j30.sm/j301_1.sm"));

        // j30 全インスタンス
        /*
        for (int i = 1; i <= 48; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "j30.sm/j30%d_1.sm", i);
            instances.push_back(string(buf));
        }
        */

        // j60 〜 j120
        /*
        for (int i = 1; i <= 48; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "j60.sm/j60%d_1.sm", i);
            instances.push_back(string(buf));
        }
        for (int i = 1; i <= 48; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "j90.sm/j90%d_1.sm", i);
            instances.push_back(string(buf));
        }
        for (int i = 1; i <= 60; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "j120.sm/j120%d_1.sm", i);
            instances.push_back(string(buf));
        }
        */

        for (const auto &inst : instances) {
            runAllConditionsAndModes(inst);
        }

        cout << "[BATCH] All done.\n";
        return 0;

    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}