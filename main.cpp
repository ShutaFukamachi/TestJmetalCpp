// ============================================================
//  main.cpp  (ターゲット)
//
//  エンコーディング × 作業分割スキーム 比較ランナー
//
//  クラス構成:
//    EncodingComparisonRunner_P1  … P1 のみで SchedObj vs MaxShift を比較（旧来動作）
//    EncodingComparisonRunner_All … P1/P2/P3 それぞれで SchedObj vs MaxShift を比較
//
//  出力ファイル（cmake-build-*/ 直下）:
//    [P1 ランナー]
//      FUN_ENC_<prefix>_<cond>_SchedObj.txt
//      FUN_ENC_<prefix>_<cond>_MaxShift.txt
//      SCHED_ENC_<prefix>_<cond>_SchedObj.txt
//      SCHED_ENC_<prefix>_<cond>_MaxShift.txt
//
//    [All ランナー]
//      FUN_ENC_<prefix>_<cond>_P1_SchedObj.txt
//      FUN_ENC_<prefix>_<cond>_P1_MaxShift.txt
//      FUN_ENC_<prefix>_<cond>_P2_SchedObj.txt
//      FUN_ENC_<prefix>_<cond>_P2_MaxShift.txt
//      FUN_ENC_<prefix>_<cond>_P3_SchedObj.txt
//      FUN_ENC_<prefix>_<cond>_P3_MaxShift.txt
//      （SCHED_ENC_* も同様）
//
//  使い方:
//    NSGAEncCpp [インスタンスファイル]
//    例: NSGAEncCpp j30.sm/j301_1.sm
// ============================================================

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <limits>
#include <future>
#include <mutex>
#include <functional>

#include "core/Problem.h"
#include "core/Algorithm.h"
#include "core/SolutionSet.h"
#include "Solution.h"
#include "metaheuristics/nsgaII/NSGAII.h"
#include "problems/RCPSP_Problem.h"
#include "problems/RCPSP_Problem_MaxShift.h"
#include "problems/RCPSP_Problem_Splitting.h"
#include "problems/RCPSP_Problem_Splitting_MaxShift.h"
#include "problems/RCPSP_Conditions.h"
#include "operators/crossover/PermutationCrossover.h"
#include "operators/crossover/MaxShiftCrossover.h"
#include "operators/mutation/PermutationMutation.h"
#include "operators/mutation/MaxShiftMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"
#include "problems/MaxShiftSensitivity.h"
#include "localsearch/FrontLocalSearch.h"

using namespace std;

// ============================================================
//  共通ユーティリティ（両ランナーで使用）
// ============================================================
namespace fs = std::filesystem;

namespace EncUtil {

// スレッドセーフなコンソール出力用ミューテックス
static std::mutex g_printMutex;

inline void ensureDir(const std::string &dir) {
    fs::create_directories(dir);
}

inline void syncPrint(const std::string &msg) {
    std::lock_guard<std::mutex> lk(g_printMutex);
    std::cout << msg;
}

// ストラテジー並列化用の型エイリアス
// factory(s) が呼ばれると strategy=s の独立した prob ペアを返す
// 返されたポインタの所有権は呼び出し元（runStrategies 内のスレッド）が持つ
using ProbPair    = std::pair<RCPSP_Problem*, RCPSP_Problem_MaxShift*>;
using ProbFactory = std::function<ProbPair(int s)>;

string toCondTag(double rr, bool rv) {
    int rrInt = static_cast<int>(std::round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

string toBaseNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

bool fileExists(const string &p) {
    ifstream f(p.c_str(), ios::binary);
    return (bool)f;
}

void copyFileBinary(const string &src, const string &dst) {
    ifstream in(src.c_str(),  ios::binary);
    if (!in)  throw runtime_error("Cannot open: " + src);
    ofstream out(dst.c_str(), ios::binary);
    if (!out) throw runtime_error("Cannot open: " + dst);
    out << in.rdbuf();
}

// SchedObj エンコーディング用オペレータをアタッチ
void attachSchedObjOps(Algorithm *algo, RCPSP_Problem *prob) {
    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob->getNumberOfVariables();
    algo->addOperator("crossover", new PermutationCrossover(crossP));
    algo->addOperator("mutation",  new PermutationMutation(mutP, prob));
    map<string, void*> sel;
    algo->addOperator("selection", new BinaryTournament2(sel));
}

// MaxShift エンコーディング用オペレータをアタッチ
void attachMaxShiftOps(Algorithm *algo, RCPSP_Problem_MaxShift *prob) {
    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob->getNumberOfVariables();
    algo->addOperator("crossover", new MaxShiftCrossover(crossP));
    algo->addOperator("mutation",  new MaxShiftMutation(mutP, prob));
    map<string, void*> sel;
    algo->addOperator("selection", new BinaryTournament2(sel));
}

// FUN / SCHED ファイル出力
void writeResultFiles(const string &outPrefix,
                      SolutionSet  *pareto,
                      RCPSP_Problem *prob)
{
    const string dir = "results/FUN_ENC/" + prob->getInstancePrefix() + "/";
    ensureDir(dir);
    const string funPath   = dir + "FUN_ENC_"   + outPrefix + ".txt";
    const string schedPath = dir + "SCHED_ENC_" + outPrefix + ".txt";

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

    cout << "[FILES] " << funPath << " / " << schedPath << "\n";

    if (pareto->size() > 0) {
        double minMs   = pareto->get(0)->getObjective(0);
        double minCost = pareto->get(0)->getObjective(1);
        for (int i = 1; i < pareto->size(); ++i) {
            minMs   = std::min(minMs,   pareto->get(i)->getObjective(0));
            minCost = std::min(minCost, pareto->get(i)->getObjective(1));
        }
        cout << "         min_makespan=" << (int)minMs
             << "  min_cost=" << fixed << setprecision(1) << minCost
             << "  pareto_size=" << pareto->size() << "\n";
    }
}

// 複数 strategy を並列で走らせて最終パレートフロントを返す
// makeProb(s) : strategy s 用の独立した prob ペアを生成するファクトリ
//   → 返されたポインタはこの関数内で delete される
SolutionSet* runStrategies(ProbFactory makeProb,
                            int numStr,
                            int populationSize,
                            int evalsPerStrategy,
                            const string &encTag,
                            bool noveltyFilter = false)   // ← NF フラグ（デフォルト off）
{
    // ---- 各ストラテジーを std::async で並列実行 ----
    // prob は Solution::type_ が参照するため、finalPareto 生成後まで生かす
    using StratResult = std::pair<SolutionSet*, RCPSP_Problem*>;
    std::vector<std::future<StratResult>> futures;
    futures.reserve(numStr);

    for (int s = 1; s <= numStr; ++s) {
        futures.push_back(std::async(std::launch::async,
            [makeProb, s, numStr, populationSize, evalsPerStrategy, encTag, noveltyFilter]()
            -> StratResult
        {
            auto [prob, prob_ms] = makeProb(s);

            syncPrint("  [" + encTag + " Strategy " + std::to_string(s)
                      + "/" + std::to_string(numStr) + "]\n");

            prob->resetEvalCounter();

            Algorithm *algo = new NSGAII(prob);
            int popSz  = populationSize;
            int maxEv  = evalsPerStrategy;
            int lsFlag = 0;
            int nfFlag = noveltyFilter ? 1 : 0;
            algo->setInputParameter("populationSize", &popSz);
            algo->setInputParameter("maxEvaluations", &maxEv);
            algo->setInputParameter("useLocalSearch",  &lsFlag);
            algo->setInputParameter("noveltyFilter",   &nfFlag);

            if (prob_ms)
                attachMaxShiftOps(algo, prob_ms);
            else
                attachSchedObjOps(algo, prob);

            SolutionSet *pop = algo->execute();

            SolutionSet *result = new SolutionSet(populationSize * 4);
            {
                Ranking ranking(pop);
                if (ranking.getNumberOfSubfronts() > 0) {
                    SolutionSet *f0 = ranking.getSubfront(0);
                    syncPrint("    [" + encTag + " S" + std::to_string(s)
                              + "] Pareto front size: "
                              + std::to_string(f0->size()) + "\n");
                    for (int i = 0; i < f0->size(); ++i)
                        result->add(new Solution(f0->get(i)));

                    // MaxShift: 進化した活動リストに max_shift=0 を適用した解を注入
                    // makespan 最小側のパレートフロントを補強する
                    if (prob_ms) {
                        prob->setOutputMaxShift(0);
                        for (int i = 0; i < f0->size(); ++i) {
                            Solution *copy = new Solution(f0->get(i));
                            prob->evaluate(copy);
                            result->add(copy);
                        }
                        prob->setOutputMaxShift(-1);
                        syncPrint("    [" + encTag + " S" + std::to_string(s)
                                  + "] Injected " + std::to_string(f0->size())
                                  + " max_shift=0 versions\n");
                    }
                }
            }
            delete pop;
            delete algo;
            // prob は呼び出し元で delete する（Solution::type_ のダングリング防止）

            return {result, prob};
        }));
    }

    // ---- 結果を収集（prob はまだ生きている）----
    SolutionSet *combined = new SolutionSet(numStr * populationSize * 4);
    std::vector<RCPSP_Problem*> probsToDelete;
    probsToDelete.reserve(numStr);

    for (int s = 0; s < numStr; ++s) {
        auto [stratResult, prob] = futures[s].get();
        for (int i = 0; i < stratResult->size(); ++i)
            combined->add(new Solution(stratResult->get(i)));  // type_ 有効
        delete stratResult;
        probsToDelete.push_back(prob);
    }

    // ---- 診断ログ ----
    {
        double minMs = 1e9, maxMs = -1e9;
        double minCost = 1e9, maxCost = -1e9;
        int validCount = 0;
        for (int i = 0; i < combined->size(); ++i) {
            Solution *sol = combined->get(i);
            double ms   = sol->getObjective(0);
            double cost = sol->getObjective(1);
            if (ms < 1e8) {
                minMs   = std::min(minMs,   ms);
                maxMs   = std::max(maxMs,   ms);
                minCost = std::min(minCost, cost);
                maxCost = std::max(maxCost, cost);
                ++validCount;
            }
        }
        syncPrint("  [DIAG:" + encTag + "]"
                  + "  combined_valid=" + std::to_string(validCount)
                  + "  makespan=[" + std::to_string((int)minMs)
                  + ", " + std::to_string((int)maxMs) + "]"
                  + "  cost=[" + std::to_string(minCost)
                  + ", " + std::to_string(maxCost) + "]\n");
    }

    SolutionSet *finalPareto = new SolutionSet(combined->size());
    {
        Ranking finalR(combined);
        if (finalR.getNumberOfSubfronts() > 0) {
            SolutionSet *f0 = finalR.getSubfront(0);
            for (int i = 0; i < f0->size(); ++i)
                finalPareto->add(new Solution(f0->get(i)));  // type_ まだ有効
        }
    }
    delete combined;

    // ---- finalPareto 生成後に prob を削除 ----
    for (RCPSP_Problem *p : probsToDelete)
        delete p;

    syncPrint("[DONE] " + encTag
              + "  Pareto size=" + std::to_string(finalPareto->size()) + "\n");
    return finalPareto;
}

} // namespace EncUtil

// ============================================================
//  EncodingComparisonRunner_P1
//
//  P1（作業分割なし）のみで SchedObj vs MaxShift を比較する。
//  旧 EncodingComparisonRunner と同等の動作。
// ============================================================
class EncodingComparisonRunner_P1 {
public:
    struct Config {
        string instanceFile;
        double rr                    = 0.0;
        bool   rv                    = false;
        int    populationSize        = 100;
        int    evalsPerStrategy      = 50000;
        int    numStrategiesSchedObj = 4;
        int    numStrategiesMaxShift = 4;
    };

    explicit EncodingComparisonRunner_P1(Config cfg)
        : cfg_(std::move(cfg))
        , prefix_(EncUtil::toBaseNoExt(cfg_.instanceFile))
    {}

    // enc=0: SchedObj, enc=1: MaxShift
    SolutionSet* runEncoding(int enc) const;

    void runAll() const;

private:
    Config cfg_;
    string prefix_;
};

SolutionSet* EncodingComparisonRunner_P1::runEncoding(int enc) const {
    const string encTag = (enc == 0) ? "SchedObj" : "MaxShift";
    const string ctag   = EncUtil::toCondTag(cfg_.rr, cfg_.rv);
    const int    numStr = (enc == 0) ? cfg_.numStrategiesSchedObj
                                     : cfg_.numStrategiesMaxShift;

    cout << "\n------------------------------------------------------------\n";
    cout << "[P1 Encoding=" << encTag << "] " << ctag
         << "  instance=" << cfg_.instanceFile << "\n";
    cout << "  popSize=" << cfg_.populationSize
         << "  evalsPerStrategy=" << cfg_.evalsPerStrategy
         << "  numStrategies=" << numStr << "\n";
    cout << "------------------------------------------------------------\n";

    const string &instFile = cfg_.instanceFile;
    const double  rr       = cfg_.rr;
    const bool    rv       = cfg_.rv;
    const int     evals    = cfg_.evalsPerStrategy;

    EncUtil::ProbFactory factory;
    if (enc == 0) {
        factory = [instFile, rr, rv, evals](int s) -> EncUtil::ProbPair {
            auto *p = new RCPSP_Problem(instFile, s, rr, rv);
            p->setMaxEvaluations(evals);
            return {p, nullptr};
        };
    } else {
        factory = [instFile, rr, rv, evals](int s) -> EncUtil::ProbPair {
            auto *p = new RCPSP_Problem_MaxShift(instFile, s, rr, rv);
            p->setMaxEvaluations(evals);
            return {p, p};
        };
    }

    SolutionSet *pareto = EncUtil::runStrategies(
            factory, numStr,
            cfg_.populationSize, cfg_.evalsPerStrategy, encTag);
    return pareto;
}

void EncodingComparisonRunner_P1::runAll() const {
    const string costsFile = "costs/" + prefix_ + "/costs_" + prefix_ + ".csv";
    EncUtil::ensureDir("costs/" + prefix_);
    if (EncUtil::fileExists(costsFile)) EncUtil::copyFileBinary(costsFile, "costs/" + prefix_ + "/costs.csv");

    using Cond = RCPSP_Cond;
    // 比較対象は RR<=0.50（6条件）。RR=0.75 は限界条件用に RCPSP_Conditions.h で別枠管理
    // （再有効化する場合は RCPSP_ALL_CONDITIONS() に差し替える）。
    const vector<Cond> conditions = RCPSP_STANDARD_CONDITIONS();
    const vector<pair<int,string>> encodings = {{0,"SchedObj"},{1,"MaxShift"}};

    cout << "\n============================================================\n";
    cout << " P1 Encoding Comparison Run\n";
    cout << " Instance: " << cfg_.instanceFile << "\n";
    cout << " popSize=" << cfg_.populationSize
         << "  evalsPerStrategy=" << cfg_.evalsPerStrategy << "\n";
    cout << "============================================================\n";

    cout << "\n"
         << left  << setw(14) << "Condition"
         << right << setw(12) << "MS_SchedObj"
         << setw(14) << "Cost_SchedObj"
         << setw(12) << "MS_MaxShift"
         << setw(14) << "Cost_MaxShift"
         << setw(10) << "dMS" << setw(12) << "dCost" << "\n";
    cout << string(88, '-') << "\n";

    for (const auto &c : conditions) {
        RCPSP_Problem::resetGlobalCostSeries();
        if (EncUtil::fileExists(costsFile)) EncUtil::copyFileBinary(costsFile, "costs.csv");

        const string ctag = EncUtil::toCondTag(c.rr, c.rv);
        double minMs[2]    = {1e9, 1e9};
        double minCost[2]  = {1e9, 1e9};

        for (const auto &[enc, encTag] : encodings) {
            Config modCfg = cfg_;
            modCfg.rr = c.rr;
            modCfg.rv = c.rv;
            EncodingComparisonRunner_P1 runner(modCfg);

            SolutionSet *pareto = runner.runEncoding(enc);

            RCPSP_Problem *prob = (enc == 0)
                ? new RCPSP_Problem(cfg_.instanceFile, 1, c.rr, c.rv)
                : (RCPSP_Problem*)new RCPSP_Problem_MaxShift(cfg_.instanceFile, 1, c.rr, c.rv);

            EncUtil::writeResultFiles(prefix_ + "_" + ctag + "_" + encTag, pareto, prob);

            for (int i = 0; i < pareto->size(); ++i) {
                minMs[enc]   = std::min(minMs[enc],   pareto->get(i)->getObjective(0));
                minCost[enc] = std::min(minCost[enc], pareto->get(i)->getObjective(1));
            }
            delete pareto;
            delete prob;
        }

        double dMs   = minMs[1]   - minMs[0];
        double dCost = minCost[1] - minCost[0];
        cout << left  << setw(14) << ctag
             << right << setw(12) << (int)minMs[0]
             << setw(14) << fixed << setprecision(1) << minCost[0]
             << setw(12) << (int)minMs[1]
             << setw(14) << minCost[1]
             << setw(10) << (dMs   >= 0 ? "+" : "") << (int)dMs
             << setw(12) << (dCost >= 0 ? "+" : "") << setprecision(1) << dCost
             << "\n";
    }
    cout << string(88, '-') << "\n";
    cout << "[P1 ALL DONE] " << prefix_ << "\n\n";
}

// ============================================================
//  EncodingComparisonRunner_All
//
//  P1 / P2 / P3 それぞれで SchedObj vs MaxShift を比較する。
//
//  問題クラス対応表:
//    P1 + SchedObj : RCPSP_Problem
//    P1 + MaxShift : RCPSP_Problem_MaxShift
//    P2 + SchedObj : RCPSP_Problem_Splitting(P2)
//    P2 + MaxShift : RCPSP_Problem_Splitting_MaxShift(P2)
//    P3 + SchedObj : RCPSP_Problem_Splitting(P3)
//    P3 + MaxShift : RCPSP_Problem_Splitting_MaxShift(P3)
// ============================================================
class EncodingComparisonRunner_All {
public:
    struct Config {
        string instanceFile;
        double rr                    = 0.0;
        bool   rv                    = false;
        int    populationSize        = 100;
        int    evalsPerStrategy      = 50000;
        int    numStrategiesSchedObj = 4;
        int    numStrategiesMaxShift = 4;
        // true にすると通常ランの後に P1 MaxShift のノベルティフィルタ版を追加実行する。
        // false（デフォルト）のときは何も変わらない。
        bool   enableNoveltyFilter   = false;
    };

    explicit EncodingComparisonRunner_All(Config cfg)
        : cfg_(std::move(cfg))
        , prefix_(EncUtil::toBaseNoExt(cfg_.instanceFile))
    {}

    // splitMode: "P1"/"P2"/"P3"、enc=0:SchedObj / enc=1:MaxShift
    // noveltyFilter=true のときは NF 版（ログ/出力に _NF サフィックス付与）
    SolutionSet* runEncoding(const string &splitMode, int enc,
                             bool noveltyFilter = false) const;

    void runAll() const;

private:
    Config cfg_;
    string prefix_;
};

SolutionSet* EncodingComparisonRunner_All::runEncoding(
        const string &splitMode, int enc, bool noveltyFilter) const
{
    const string encTag = ((enc == 0) ? "SchedObj" : "MaxShift")
                          + string(noveltyFilter ? "_NF" : "");
    const string ctag   = EncUtil::toCondTag(cfg_.rr, cfg_.rv);
    const int    numStr = (enc == 0) ? cfg_.numStrategiesSchedObj
                                     : cfg_.numStrategiesMaxShift;

    cout << "\n------------------------------------------------------------\n";
    cout << "[" << splitMode << " Encoding=" << encTag << "] " << ctag
         << "  instance=" << cfg_.instanceFile << "\n";
    cout << "  popSize=" << cfg_.populationSize
         << "  evalsPerStrategy=" << cfg_.evalsPerStrategy
         << "  numStrategies=" << numStr << "\n";
    cout << "------------------------------------------------------------\n";

    const string &instFile = cfg_.instanceFile;
    const double  rr       = cfg_.rr;
    const bool    rv       = cfg_.rv;
    const int     evals    = cfg_.evalsPerStrategy;
    ActivitySplittingMode mode = (splitMode == "P2")
        ? ActivitySplittingMode::P2 : ActivitySplittingMode::P3;
    const bool isP1 = (splitMode == "P1");

    EncUtil::ProbFactory factory;
    if (isP1) {
        if (enc == 0) {
            factory = [instFile, rr, rv, evals](int s) -> EncUtil::ProbPair {
                auto *p = new RCPSP_Problem(instFile, s, rr, rv);
                p->setMaxEvaluations(evals);
                return {p, nullptr};
            };
        } else {
            factory = [instFile, rr, rv, evals](int s) -> EncUtil::ProbPair {
                auto *p = new RCPSP_Problem_MaxShift(instFile, s, rr, rv);
                p->setMaxEvaluations(evals);
                return {p, p};
            };
        }
    } else {
        if (enc == 0) {
            factory = [instFile, mode, rr, rv, evals](int s) -> EncUtil::ProbPair {
                auto *p = new RCPSP_Problem_Splitting(instFile, mode, s, rr, rv);
                p->setMaxEvaluations(evals);
                return {p, nullptr};
            };
        } else {
            factory = [instFile, mode, rr, rv, evals](int s) -> EncUtil::ProbPair {
                auto *p = new RCPSP_Problem_Splitting_MaxShift(instFile, mode, s, rr, rv);
                p->setMaxEvaluations(evals);
                return {(RCPSP_Problem*)p, p};
            };
        }
    }

    SolutionSet *pareto = EncUtil::runStrategies(
            factory, numStr,
            cfg_.populationSize, cfg_.evalsPerStrategy,
            splitMode + "_" + encTag,
            noveltyFilter);
    return pareto;
}

void EncodingComparisonRunner_All::runAll() const {
    const string costsFile = "costs/" + prefix_ + "/costs_" + prefix_ + ".csv";
    EncUtil::ensureDir("costs/" + prefix_);
    if (EncUtil::fileExists(costsFile)) EncUtil::copyFileBinary(costsFile, "costs/" + prefix_ + "/costs.csv");

    using Cond = RCPSP_Cond;
    // 比較対象は RR<=0.50（6条件）。RR=0.75 は限界条件用に RCPSP_Conditions.h で別枠管理
    // （再有効化する場合は RCPSP_ALL_CONDITIONS() に差し替える）。
    const vector<Cond> conditions = RCPSP_STANDARD_CONDITIONS();
    const vector<string>       splitModes = {"P1", "P2", "P3"};
    const vector<pair<int,string>> encodings = {{0,"SchedObj"},{1,"MaxShift"}};  // 計算時間比較のため SchedObj を一時無効化

    cout << "\n============================================================\n";
    cout << " All Encoding Comparison Run (P1/P2/P3 x SchedObj/MaxShift)\n";
    cout << " Instance: " << cfg_.instanceFile << "\n";
    cout << " popSize=" << cfg_.populationSize
         << "  evalsPerStrategy=" << cfg_.evalsPerStrategy << "\n";
    cout << "============================================================\n";

    // ヘッダ: 条件 | P1_SO P1_MS | P2_SO P2_MS | P3_SO P3_MS
    cout << "\n" << left << setw(14) << "Condition";
    for (const auto &m : splitModes)
        cout << right << setw(12) << (m+"_SO_ms")
             << setw(12) << (m+"_MS_ms");
    cout << "\n" << string(14 + 12*6, '-') << "\n";

    for (const auto &c : conditions) {
        RCPSP_Problem::resetGlobalCostSeries();
        if (EncUtil::fileExists(costsFile)) EncUtil::copyFileBinary(costsFile, "costs.csv");

        const string ctag = EncUtil::toCondTag(c.rr, c.rv);

        // 各 (splitMode, enc) の min_makespan を収集
        map<string, double> minMsMap, minCostMap;

        for (const auto &m : splitModes) {
            for (const auto &[enc, encTag] : encodings) {
                Config modCfg = cfg_;
                modCfg.rr = c.rr;
                modCfg.rv = c.rv;
                EncodingComparisonRunner_All runner(modCfg);

                SolutionSet *pareto = runner.runEncoding(m, enc);

                // 出力用問題インスタンス（決定論的 capacity_t のため同じパラメータで再生成）
                RCPSP_Problem *prob = nullptr;
                if (m == "P1") {
                    prob = (enc == 0)
                        ? new RCPSP_Problem(cfg_.instanceFile, 1, c.rr, c.rv)
                        : (RCPSP_Problem*)new RCPSP_Problem_MaxShift(
                                cfg_.instanceFile, 1, c.rr, c.rv);
                } else {
                    ActivitySplittingMode mode = (m == "P2")
                        ? ActivitySplittingMode::P2 : ActivitySplittingMode::P3;
                    prob = (enc == 0)
                        ? (RCPSP_Problem*)new RCPSP_Problem_Splitting(
                                cfg_.instanceFile, mode, 1, c.rr, c.rv)
                        : (RCPSP_Problem*)new RCPSP_Problem_Splitting_MaxShift(
                                cfg_.instanceFile, mode, 1, c.rr, c.rv);
                }

                // ファイル名: FUN_ENC_{prefix}_{cond}_{mode}_{enc}.txt
                const string outPrefix =
                        prefix_ + "_" + ctag + "_" + m + "_" + encTag;
                EncUtil::writeResultFiles(outPrefix, pareto, prob);

                // ---- P1 MaxShift 後処理局所探索 ----
                if (m == "P1" && enc == 1) {
                    FrontLocalSearch::Config lsCfg;
                    lsCfg.maxPasses = 5;
                    lsCfg.deltas    = {0, 1, 2, 4};
                    FrontLocalSearch ls(prob, lsCfg);

                    std::vector<FrontLocalSearch::SolStat> lsStats;
                    SolutionSet *enhanced = ls.apply(pareto, &lsStats);

                    // MaxShift_LS タグで出力
                    const string lsPrefix =
                            prefix_ + "_" + ctag + "_" + m + "_MaxShift_LS";
                    EncUtil::writeResultFiles(lsPrefix, enhanced, prob);

                    // LS サマリ
                    int improvedCount = 0;
                    double totalReduction = 0.0;
                    for (const auto &s : lsStats) {
                        if (s.costReduction > 1e-9) {
                            ++improvedCount;
                            totalReduction += s.costReduction;
                        }
                    }
                    cout << "  [LS] " << improvedCount << "/" << lsStats.size()
                         << " improved, total_reduction="
                         << fixed << setprecision(1) << totalReduction
                         << ", PF_LS=" << enhanced->size() << "\n";

                    // LS ログ
                    const string lsLogDir = "logs/localsearch/";
                    EncUtil::ensureDir(lsLogDir);
                    FrontLocalSearch::writeLog(
                            lsLogDir + "ls_log_" + prefix_ + "_" + ctag + ".csv",
                            lsStats);

                    delete enhanced;
                }

                // サマリ集計
                const string key = m + "_" + encTag;
                minMsMap[key]   = 1e9;
                minCostMap[key] = 1e9;
                for (int i = 0; i < pareto->size(); ++i) {
                    minMsMap[key]   = std::min(minMsMap[key],
                                               pareto->get(i)->getObjective(0));
                    minCostMap[key] = std::min(minCostMap[key],
                                               pareto->get(i)->getObjective(1));
                }

                delete pareto;
                delete prob;
            }
        }

        // サマリ行を出力
        cout << left << setw(14) << ctag;
        for (const auto &m : splitModes) {
            double msS  = minMsMap[m+"_SchedObj"];
            double msM  = minMsMap[m+"_MaxShift"];
            cout << right << setw(12) << (msS  < 1e8 ? to_string((int)msS)  : "---")
                          << setw(12) << (msM  < 1e8 ? to_string((int)msM)  : "---");
        }
        cout << "\n";
    }

    cout << string(14 + 12*6, '-') << "\n";
    cout << "[ALL DONE] " << prefix_ << "\n\n";

    // ================================================================
    // ノベルティフィルタ追加ラン (cfg_.enableNoveltyFilter == true のとき)
    //   P1 MaxShift のみ、全 8 条件を通常ランと同じ設定で再実行する。
    //   出力ファイル名: FUN_ENC_*_P1_MaxShift_NF.txt / SCHED_ENC_*_P1_MaxShift_NF.txt
    //   ログファイル名: manytoone_log_*_MaxShift_NF.csv
    //   元の NSGA-II ランには一切影響しない（このブロック以外は変更なし）。
    // ================================================================
    if (cfg_.enableNoveltyFilter) {
        cout << "\n============================================================\n";
        cout << " [NF] Novelty Filter 試験ラン: P1 MaxShift\n";
        cout << " Instance: " << cfg_.instanceFile << "\n";
        cout << " popSize=" << cfg_.populationSize
             << "  evalsPerStrategy=" << cfg_.evalsPerStrategy << "\n";
        cout << "============================================================\n";

        cout << "\n" << left  << setw(14) << "Condition"
                     << right << setw(16) << "NF_min_makespan"
                              << setw(14) << "NF_min_cost"
                              << setw(10) << "PF_size" << "\n";
        cout << string(54, '-') << "\n";

        for (const auto &c : conditions) {
            RCPSP_Problem::resetGlobalCostSeries();
            if (EncUtil::fileExists(costsFile))
                EncUtil::copyFileBinary(costsFile, "costs.csv");

            const string ctag = EncUtil::toCondTag(c.rr, c.rv);
            Config modCfg = cfg_;
            modCfg.rr = c.rr;
            modCfg.rv = c.rv;
            EncodingComparisonRunner_All runner(modCfg);

            // noveltyFilter = true で実行
            SolutionSet *pareto = runner.runEncoding("P1", /*enc=*/1, /*noveltyFilter=*/true);

            // 出力ファイル: FUN_ENC_*_P1_MaxShift_NF.txt
            RCPSP_Problem *prob = (RCPSP_Problem*)new RCPSP_Problem_MaxShift(
                    cfg_.instanceFile, 1, c.rr, c.rv);
            const string outPrefix = prefix_ + "_" + ctag + "_P1_MaxShift_NF";
            EncUtil::writeResultFiles(outPrefix, pareto, prob);

            double minMs   = 1e9;
            double minCost = 1e9;
            for (int i = 0; i < pareto->size(); ++i) {
                minMs   = std::min(minMs,   pareto->get(i)->getObjective(0));
                minCost = std::min(minCost, pareto->get(i)->getObjective(1));
            }
            cout << left  << setw(14) << ctag
                 << right << setw(16) << (minMs < 1e8 ? to_string((int)minMs) : "---")
                          << setw(14) << fixed << setprecision(1) << minCost
                          << setw(10) << pareto->size() << "\n";

            delete pareto;
            delete prob;
        }
        cout << string(54, '-') << "\n";
        cout << "[NF DONE] " << prefix_ << "\n\n";
    }
}

// ============================================================
//  main
// ============================================================
// ============================================================
//  実行対象インスタンスリスト
//
//  引数なし → 以下の全インスタンスを順番に実行
//  引数あり → 指定されたインスタンスファイルのみ実行
//    例: NSGAEncCpp j30.sm/j3010_1.sm
//
//  計算量の目安（evalsPerStrategy=50000, popSize=100 の場合）:
//    1インスタンス: 8条件 × 3mode × 2enc × 4str = 192実行 × 5万eval = 960万eval
//    20インスタンス: 192 × 20 = 3840実行 ≒ 19.2億eval
//    必ず Release ビルドをターミナルから実行すること。
// ============================================================
// random.seed(42) で各グループの _1 インスタンス（costs が存在する）から5個ずつ無作為抽出
// j30/j60/j90: 48個中5個、j120: 60個中5個
static const vector<string> ALL_INSTANCES = {
    // j30 (_1 instances: 48個)
    "j30.sm/j3011_1.sm", "j30.sm/j3017_1.sm", "j30.sm/j3026_1.sm",
    "j30.sm/j3047_1.sm", "j30.sm/j309_1.sm",
    // j60 (_1 instances: 48個)
    // "j60.sm/j6016_1.sm", "j60.sm/j6018_1.sm", "j60.sm/j6023_1.sm",
    // "j60.sm/j6024_1.sm", "j60.sm/j609_1.sm",
    // // j90 (_1 instances: 48個)
    // "j90.sm/j9015_1.sm", "j90.sm/j9041_1.sm", "j90.sm/j9044_1.sm",
    // "j90.sm/j905_1.sm",  "j90.sm/j909_1.sm",
    // // j120 (_1 instances: 60個)
    // "j120.sm/j12011_1.sm", "j120.sm/j12012_1.sm", "j120.sm/j12015_1.sm",
    // "j120.sm/j12022_1.sm", "j120.sm/j12035_1.sm",
};

// ============================================================
//  runManyToOneTest
//
//  「活動リストが異なるのにスケジュール・評価値が同じ」という
//  many-to-one 特性を実証する検証関数。
//
//  手順:
//    1. 指定インスタンスを読み込む（maxShift=0 固定）
//    2. N 個のランダムトポロジカル順序を生成
//    3. 各順序で evaluate() → startTimes_ と目的値を記録
//    4. 重複スケジュールの数・割合を報告する
//
//  呼び出し: TestJmetalCpp --many-to-one <instanceFile> [N]
// ============================================================
static void runManyToOneTest(const string &instanceFile, int N = 30,
                              double rr = 0.0, bool rv = false)
{
    cout << "\n========================================\n"
         << "[ManyToOne] instance=" << instanceFile
         << "  RR=" << rr << "  RV=" << rv
         << "  N=" << N << "\n"
         << "========================================\n";

    // maxShift=0（EST 配置のみ）で検証
    RCPSP_Problem_MaxShift prob(instanceFile, /*strategy=*/1, rr, rv);
    const int n = prob.getNumJobs();

    cout << "  jobs=" << n << "\n";

    // activity list (vars[0..n-1]) の文字列表現 → (makespan, cost, startTimes) のマップ
    using SchedKey = std::vector<int>;  // startTimes_ ベクタをキーに使う
    struct Record {
        std::vector<int> actList;
        int    makespan;
        double cost;
    };
    std::map<SchedKey, std::vector<Record>> schedMap;

    for (int trial = 0; trial < N; ++trial) {
        Solution *sol = prob.createRandomTopoSolution();
        auto &vars = sol->getVars();

        // max_shift をすべて 0 に強制（EST 配置で比較）
        for (int i = n; i < 2 * n; ++i) vars[i] = 0;

        prob.evaluate(sol);

        Record rec;
        rec.actList.assign(vars.begin(), vars.begin() + n);
        rec.makespan = static_cast<int>(sol->getObjective(0));
        rec.cost     = sol->getObjective(1);

        SchedKey key = sol->startTimes_;

        schedMap[key].push_back(rec);
        delete sol;
    }

    // 結果集計
    int uniqueScheds   = static_cast<int>(schedMap.size());
    int collisionCount = 0;
    for (auto &[key, recs] : schedMap)
        if (recs.size() > 1) collisionCount += static_cast<int>(recs.size()) - 1;

    cout << "\n  試行数          : " << N << "\n"
         << "  ユニーク スケジュール数 : " << uniqueScheds << "\n"
         << "  重複ヒット数      : " << collisionCount << "\n";

    // 重複のあるグループを最大 3 件表示
    int shown = 0;
    for (auto &[key, recs] : schedMap) {
        if (recs.size() < 2) continue;
        cout << "\n  --- 重複グループ (スケジュール同一, " << recs.size() << "件) ---\n";
        cout << "    startTimes: [";
        for (int t : key) cout << t << " ";
        cout << "]\n";
        for (auto &r : recs) {
            cout << "    actList=[";
            for (int j : r.actList) cout << j << " ";
            cout << "]  makespan=" << r.makespan
                 << "  cost=" << r.cost << "\n";
        }
        if (++shown >= 3) break;
    }

    if (collisionCount == 0) {
        cout << "\n  -> すべての活動リストが異なるスケジュールを生成しました。\n"
             << "     (N を増やすか、resource が緩い問題で試してください)\n";
    } else {
        cout << "\n  -> many-to-one 確認: "
             << collisionCount << " 件の重複スケジュールを検出しました。\n";
    }
    cout << "========================================\n\n";
}

// ============================================================
//  runCostCollisionTest
//
//  「startTimes_ が異なるのに total cost（目的1）が完全一致する」
//  現象の機構を特定するための診断モード。
//
//  呼び出し: TestJmetalCpp --cost-collision <instanceFile> [N=2000] [RR=0.0] [RV=0]
// ============================================================
static void runCostCollisionTest(const string &instanceFile, int N = 2000,
                                  double rr = 0.0, bool rv = false)
{
    cout << "\n========================================\n"
         << "[CostCollision] instance=" << instanceFile
         << "  RR=" << rr << "  RV=" << rv
         << "  N=" << N << "\n"
         << "========================================\n";

    RCPSP_Problem_MaxShift prob(instanceFile, /*strategy=*/3, rr, rv);
    const int n = prob.getNumJobs();
    const int nRes = prob.getNumResources();

    // ホライゾン T の計算（evaluate と同じ方式）
    int T = 0;
    {
        const auto &dur = prob.getDurations();
        for (int d : dur) T += d;
    }

    cout << "  jobs=" << n << "  resources=" << nRes << "  horizon=" << T << "\n";

    // 解を生成・評価
    struct SolRecord {
        double makespan;
        double cost;
        vector<int> startTimes;
    };
    vector<SolRecord> records;
    records.reserve(N);

    for (int i = 0; i < N; ++i) {
        Solution *sol = prob.createRandomTopoSolution();
        // max_shift はランダム初期化のまま（多様な窓を試すため 0 に強制しない）
        prob.evaluate(sol);
        SolRecord rec;
        rec.makespan   = sol->getObjective(0);
        rec.cost       = sol->getObjective(1);
        rec.startTimes = sol->startTimes_;
        records.push_back(std::move(rec));
        delete sol;
    }

    // (makespan, cost) でグルーピング（ビット完全一致）
    using ObjKey = pair<double, double>;
    map<ObjKey, vector<int>> objGroups;  // key -> indices
    for (int i = 0; i < (int)records.size(); ++i) {
        objGroups[{records[i].makespan, records[i].cost}].push_back(i);
    }

    // 「同一目的値だが startTimes_ が異なる」グループを検出
    struct CollisionGroup {
        ObjKey key;
        vector<int> indices;  // このグループに属する解のインデックス
        // 仮説判定結果
        bool hypothesis_a = false;  // 対称スワップ
        bool hypothesis_b = false;  // ダミー/需要ゼロ
        bool hypothesis_c = false;  // 丸め一致（double 完全一致なら false）
    };
    vector<CollisionGroup> collisions;

    for (auto &[key, idxs] : objGroups) {
        if (idxs.size() < 2) continue;
        // startTimes が全て同一かチェック
        bool allSame = true;
        for (int k = 1; k < (int)idxs.size(); ++k) {
            if (records[idxs[k]].startTimes != records[idxs[0]].startTimes) {
                allSame = false;
                break;
            }
        }
        if (allSame) continue;  // 目的値も startTimes も同一 → 衝突ではない

        CollisionGroup cg;
        cg.key = key;
        cg.indices = idxs;
        collisions.push_back(std::move(cg));
    }

    const auto &dur    = prob.getDurations();
    const auto &demand = prob.getDemand();

    // 最大5グループについて詳細ダンプ
    int shown = 0;
    int count_a = 0, count_b = 0, count_c = 0;

    for (auto &cg : collisions) {
        // 代表ペア（先頭2つ）で分析
        const auto &st0 = records[cg.indices[0]].startTimes;
        const auto &st1 = records[cg.indices[1]].startTimes;

        // 差分ジョブの特定
        vector<int> diffJobs;
        for (int j = 0; j < n; ++j) {
            if (j < (int)st0.size() && j < (int)st1.size() && st0[j] != st1[j])
                diffJobs.push_back(j);
        }

        // 仮説 (b): 差分ジョブが全て duration=0 または demand 全ゼロ
        bool allDummy = true;
        for (int j : diffJobs) {
            bool isDummy = (dur[j] == 0);
            if (!isDummy && j < (int)demand.size()) {
                bool allZeroDemand = true;
                for (int k = 0; k < nRes && k < (int)demand[j].size(); ++k) {
                    if (demand[j][k] != 0) { allZeroDemand = false; break; }
                }
                isDummy = allZeroDemand;
            }
            if (!isDummy) { allDummy = false; break; }
        }
        cg.hypothesis_b = allDummy && !diffJobs.empty();

        // 仮説 (a): 差分ジョブ集合の中に duration & demand が完全同一のペアがあるか
        bool hasSymmetricPair = false;
        for (int i = 0; i < (int)diffJobs.size() && !hasSymmetricPair; ++i) {
            for (int k = i + 1; k < (int)diffJobs.size(); ++k) {
                int ji = diffJobs[i], jk = diffJobs[k];
                if (dur[ji] == dur[jk] && ji < (int)demand.size() && jk < (int)demand.size()
                    && demand[ji] == demand[jk]) {
                    hasSymmetricPair = true;
                    break;
                }
            }
        }
        cg.hypothesis_a = hasSymmetricPair;

        // 仮説 (c): double 完全一致 → この時点では false（ビット完全一致でグルーピング済み）
        cg.hypothesis_c = false;

        if (cg.hypothesis_a) ++count_a;
        if (cg.hypothesis_b) ++count_b;
        if (cg.hypothesis_c) ++count_c;

        // 詳細出力（最大5グループ）
        if (shown < 5) {
            ++shown;
            cout << "\n  --- Collision Group #" << shown
                 << " (" << cg.indices.size() << " solutions, same objectives) ---\n";
            printf("    makespan = %.17g\n", cg.key.first);
            printf("    cost     = %.17g\n", cg.key.second);

            // per-job コスト
            double sumJobCost0 = 0.0, sumJobCost1 = 0.0;
            cout << "    Differing jobs (" << diffJobs.size() << "):\n";
            cout << "      " << left << setw(6) << "Job"
                 << setw(6) << "Dur"
                 << setw(20) << "Demand"
                 << setw(10) << "Start_A"
                 << setw(10) << "Start_B"
                 << setw(16) << "JobCost_A"
                 << setw(16) << "JobCost_B" << "\n";

            for (int j : diffJobs) {
                double jc0 = prob.computeJobCostAt(j, st0[j], T);
                double jc1 = prob.computeJobCostAt(j, st1[j], T);
                sumJobCost0 += jc0;
                sumJobCost1 += jc1;

                // demand ベクトル文字列
                string demStr = "[";
                if (j < (int)demand.size()) {
                    for (int k = 0; k < nRes && k < (int)demand[j].size(); ++k) {
                        if (k > 0) demStr += ",";
                        demStr += to_string(demand[j][k]);
                    }
                }
                demStr += "]";

                cout << "      " << left << setw(6) << j
                     << setw(6) << dur[j]
                     << setw(20) << demStr
                     << setw(10) << st0[j]
                     << setw(10) << st1[j];
                printf("%-16.6f%-16.6f\n", jc0, jc1);
            }
            printf("    Sum of diff-job costs: A=%.17g  B=%.17g  match=%s\n",
                   sumJobCost0, sumJobCost1,
                   (sumJobCost0 == sumJobCost1) ? "YES" : "NO");
            cout << "    Symmetric pair (hyp a): " << (cg.hypothesis_a ? "YES" : "NO") << "\n";
            cout << "    All dummy/zero (hyp b): " << (cg.hypothesis_b ? "YES" : "NO") << "\n";
        }
    }

    // サマリ
    int uniqueObjCount = static_cast<int>(objGroups.size());
    cout << "\n  ======== Summary ========\n"
         << "  Total solutions:                  " << N << "\n"
         << "  Unique (makespan,cost) pairs:     " << uniqueObjCount << "\n"
         << "  Collision groups (same obj, diff sched): " << collisions.size() << "\n"
         << "  Hypothesis (a) symmetric swap:    " << count_a << "\n"
         << "  Hypothesis (b) dummy/zero demand: " << count_b << "\n"
         << "  Hypothesis (c) rounding:          " << count_c << " (always 0 with exact match)\n";

    if (collisions.empty()) {
        cout << "\n  -> No exact cost collisions found.\n";
        // 近似一致の参考表示
        int approxCollisions = 0;
        map<pair<int,long long>, vector<int>> approxGroups;
        for (int i = 0; i < (int)records.size(); ++i) {
            int ms = static_cast<int>(records[i].makespan);
            long long cRound = static_cast<long long>(std::round(records[i].cost / 1e-6));
            approxGroups[{ms, cRound}].push_back(i);
        }
        for (auto &[k, idxs] : approxGroups) {
            if (idxs.size() >= 2) {
                // startTimes が異なるペアがあるかチェック
                for (int i = 1; i < (int)idxs.size(); ++i) {
                    if (records[idxs[i]].startTimes != records[idxs[0]].startTimes) {
                        ++approxCollisions;
                        break;
                    }
                }
            }
        }
        cout << "     Approx collisions (cost rounded to 1e-6): " << approxCollisions << "\n";
    }

    cout << "========================================\n\n";
}

void runInstance(const string &instanceFile) {
    // 共通設定
    // NOTE: 本番: evalsPerStrategy=50000, popSize=100
    //       デバッグ時は evalsPerStrategy=10000, numStrategies=2 程度で動作確認を推奨。
    EncodingComparisonRunner_All::Config cfg;
    cfg.instanceFile           = instanceFile;
    cfg.rr                     = 0.0;
    cfg.rv                     = false;
    cfg.populationSize         = 100;     // IDE → 100個体/ストラテジー
    cfg.evalsPerStrategy       = 50000;  // 評価回数=50000 (世代数≒500)  デバッグ: 10000
    cfg.numStrategiesSchedObj  = 4;      // 本番: 4  デバッグ: 2
    cfg.numStrategiesMaxShift  = 4;      // 本番: 4  デバッグ: 2
    // ノベルティフィルタ試験: false → 元の NSGA-II のみ実行（デフォルト）
    //                         true  → 通常ランの後に P1 MaxShift NF 版を追加実行
    cfg.enableNoveltyFilter    = true;

    const string prefix = EncUtil::toBaseNoExt(instanceFile);

    // ---- 感度分析（MaxShift 上限 T/4 の妥当性検証） ----
    {
        const string sensDir = "analysis/sensitivity/" + prefix + "/";
        EncUtil::ensureDir(sensDir);
        const string csvPath = sensDir + "maxshift_sensitivity_" + prefix + "_RR000_RV0.csv";
        MaxShiftSensitivityAnalyzer sa(instanceFile, 0.0, false);
        sa.runAndSave(csvPath, 40);
    }

    // ---- P1/P2/P3 × SchedObj/MaxShift 全比較 ----
    EncodingComparisonRunner_All runner(cfg);
    auto t0 = std::chrono::steady_clock::now();
    runner.runAll();
    double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
    cout << "[TIME] " << prefix << "  " << elapsed << " s\n";
}

int main(int argc, char **argv) {
    try {
        // 検証モード: TestJmetalCpp --many-to-one <instance> [N] [RR] [RV]
        //   例: .\TestJmetalCpp.exe --many-to-one j30.sm/j309_1.sm 100 0.50 1
        if (argc >= 2 && string(argv[1]) == "--many-to-one") {
            const string inst = (argc >= 3) ? argv[2] : "j30.sm/j3011_1.sm";
            const int    N    = (argc >= 4) ? std::stoi(argv[3]) : 50;
            const double rr   = (argc >= 5) ? std::stod(argv[4]) : 0.0;
            const bool   rv   = (argc >= 6) && (std::stoi(argv[5]) != 0);
            runManyToOneTest(inst, N, rr, rv);
            return 0;
        }

        // 診断モード: TestJmetalCpp --cost-collision <instance> [N] [RR] [RV]
        //   例: .\TestJmetalCpp.exe --cost-collision j30.sm/j3011_1.sm 2000 0.0 0
        if (argc >= 2 && string(argv[1]) == "--cost-collision") {
            const string inst = (argc >= 3) ? argv[2] : "j30.sm/j3011_1.sm";
            const int    N    = (argc >= 4) ? std::stoi(argv[3]) : 2000;
            const double rr   = (argc >= 5) ? std::stod(argv[4]) : 0.0;
            const bool   rv   = (argc >= 6) && (std::stoi(argv[5]) != 0);
            runCostCollisionTest(inst, N, rr, rv);
            return 0;
        }

        if (argc >= 2) {
            // 単一インスタンス実行
            const string instanceFile = argv[1];
            cout << "\n[SINGLE] " << instanceFile << "\n";
            runInstance(instanceFile);
        } else {
            // 全インスタンス一括実行
            const int total = static_cast<int>(ALL_INSTANCES.size());
            cout << "\n[BATCH] " << total << " instances\n";
            auto t_all = std::chrono::steady_clock::now();

            for (int i = 0; i < total; ++i) {
                const string &inst = ALL_INSTANCES[i];
                cout << "\n============================================================\n";
                cout << "[" << (i + 1) << "/" << total << "] " << inst << "\n";
                cout << "============================================================\n";
                RCPSP_Problem::resetGlobalCostSeries();
                runInstance(inst);
            }

            double total_elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - t_all).count();
            cout << "\n[BATCH DONE] total=" << total_elapsed << " s"
                 << "  (" << (total_elapsed / 3600.0) << " h)\n";
        }
        return 0;
    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
