// ============================================================
//  main_NSGAII_RCPSP_EncodingComparison.cpp  (NSGAEncCpp ターゲット)
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

#include "core/Problem.h"
#include "core/Algorithm.h"
#include "core/SolutionSet.h"
#include "Solution.h"
#include "Variable.h"
#include "metaheuristics/nsgaII/NSGAII.h"
#include "problems/RCPSP_Problem.h"
#include "problems/RCPSP_Problem_MaxShift.h"
#include "problems/RCPSP_Problem_Splitting.h"
#include "problems/RCPSP_Problem_Splitting_MaxShift.h"
#include "operators/crossover/PermutationCrossover.h"
#include "operators/crossover/MaxShiftCrossover.h"
#include "operators/mutation/PermutationMutation.h"
#include "operators/mutation/MaxShiftMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"
#include "problems/MaxShiftSensitivity.h"

using namespace std;

// ============================================================
//  共通ユーティリティ（両ランナーで使用）
// ============================================================
namespace EncUtil {

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
    const string funPath   = "FUN_ENC_"   + outPrefix + ".txt";
    const string schedPath = "SCHED_ENC_" + outPrefix + ".txt";

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

// 複数 strategy を走らせて最終パレートフロントを返す（共通コア）
// prob_ms が非 nullptr なら MaxShift オペレータを使用
SolutionSet* runStrategies(RCPSP_Problem *prob,
                            RCPSP_Problem_MaxShift *prob_ms,
                            int numStr,
                            int populationSize,
                            int evalsPerStrategy,
                            const string &encTag)
{
    SolutionSet *combined = new SolutionSet(numStr * populationSize * 4);

    for (int s = 1; s <= numStr; ++s) {
        cout << "  [" << encTag << " Strategy " << s << "/" << numStr << "]\n";

        prob->setStrategy(s);
        prob->resetEvalCounter();
        prob->clearStartTimesCache();

        Algorithm *algo = new NSGAII(prob);
        int popSz  = populationSize;
        int maxEv  = evalsPerStrategy;
        int lsFlag = 0;
        algo->setInputParameter("populationSize", &popSz);
        algo->setInputParameter("maxEvaluations", &maxEv);
        algo->setInputParameter("useLocalSearch",  &lsFlag);

        if (prob_ms)
            attachMaxShiftOps(algo, prob_ms);
        else
            attachSchedObjOps(algo, prob);

        SolutionSet *pop = algo->execute();
        {
            Ranking ranking(pop);
            if (ranking.getNumberOfSubfronts() > 0) {
                SolutionSet *f0 = ranking.getSubfront(0);
                cout << "    Pareto front size: " << f0->size() << "\n";
                for (int i = 0; i < f0->size(); ++i)
                    combined->add(new Solution(f0->get(i)));
            }
        }
        delete pop;
        delete algo;
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
    cout << "[DONE] " << encTag << "  Pareto size=" << finalPareto->size() << "\n";
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

    RCPSP_Problem        *prob   = nullptr;
    RCPSP_Problem_MaxShift *probMS = nullptr;

    if (enc == 0) {
        prob = new RCPSP_Problem(cfg_.instanceFile, 1, cfg_.rr, cfg_.rv);
        prob->setMaxEvaluations(cfg_.evalsPerStrategy);
    } else {
        probMS = new RCPSP_Problem_MaxShift(cfg_.instanceFile, 1, cfg_.rr, cfg_.rv);
        probMS->setMaxEvaluations(cfg_.evalsPerStrategy);
        prob = probMS;
    }

    SolutionSet *pareto = EncUtil::runStrategies(
            prob, probMS, numStr,
            cfg_.populationSize, cfg_.evalsPerStrategy, encTag);

    delete prob;  // probMS は prob と同一ポインタ
    return pareto;
}

void EncodingComparisonRunner_P1::runAll() const {
    const string costsFile = "costs_" + prefix_ + ".csv";
    if (EncUtil::fileExists(costsFile)) EncUtil::copyFileBinary(costsFile, "costs.csv");

    struct Cond { double rr; bool rv; };
    const vector<Cond> conditions = {
        {0.00, false}, {0.00, true},
        {0.25, false}, {0.25, true},
        {0.50, false}, {0.50, true},
        {0.75, false}, {0.75, true},
    };
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
    };

    explicit EncodingComparisonRunner_All(Config cfg)
        : cfg_(std::move(cfg))
        , prefix_(EncUtil::toBaseNoExt(cfg_.instanceFile))
    {}

    // splitMode: "P1"/"P2"/"P3"、enc=0:SchedObj / enc=1:MaxShift
    SolutionSet* runEncoding(const string &splitMode, int enc) const;

    void runAll() const;

private:
    Config cfg_;
    string prefix_;
};

SolutionSet* EncodingComparisonRunner_All::runEncoding(
        const string &splitMode, int enc) const
{
    const string encTag = (enc == 0) ? "SchedObj" : "MaxShift";
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

    RCPSP_Problem          *prob   = nullptr;
    RCPSP_Problem_MaxShift *probMS = nullptr;

    ActivitySplittingMode mode = (splitMode == "P2")
        ? ActivitySplittingMode::P2 : ActivitySplittingMode::P3;

    if (splitMode == "P1") {
        if (enc == 0) {
            prob = new RCPSP_Problem(cfg_.instanceFile, 1, cfg_.rr, cfg_.rv);
        } else {
            probMS = new RCPSP_Problem_MaxShift(cfg_.instanceFile, 1, cfg_.rr, cfg_.rv);
            prob   = probMS;
        }
    } else {
        // P2 or P3
        if (enc == 0) {
            prob = new RCPSP_Problem_Splitting(
                    cfg_.instanceFile, mode, 1, cfg_.rr, cfg_.rv);
        } else {
            auto *p = new RCPSP_Problem_Splitting_MaxShift(
                    cfg_.instanceFile, mode, 1, cfg_.rr, cfg_.rv);
            probMS = p;
            prob   = p;
        }
    }
    prob->setMaxEvaluations(cfg_.evalsPerStrategy);

    SolutionSet *pareto = EncUtil::runStrategies(
            prob, probMS, numStr,
            cfg_.populationSize, cfg_.evalsPerStrategy,
            splitMode + "_" + encTag);

    delete prob;
    return pareto;
}

void EncodingComparisonRunner_All::runAll() const {
    const string costsFile = "costs_" + prefix_ + ".csv";
    if (EncUtil::fileExists(costsFile)) EncUtil::copyFileBinary(costsFile, "costs.csv");

    struct Cond { double rr; bool rv; };
    const vector<Cond> conditions = {
        {0.00, false}, {0.00, true},
        {0.25, false}, {0.25, true},
        {0.50, false}, {0.50, true},
        {0.75, false}, {0.75, true},
    };
    const vector<string>       splitModes = {"P1", "P2", "P3"};
    const vector<pair<int,string>> encodings = {{0,"SchedObj"},{1,"MaxShift"}};

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
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        const string defaultInstance = "j30.sm/j301_1.sm";
        const string instanceFile = (argc >= 2) ? string(argv[1]) : defaultInstance;

        // 共通設定
        EncodingComparisonRunner_All::Config cfg;
        cfg.instanceFile           = instanceFile;
        cfg.rr                     = 0.0;
        cfg.rv                     = false;
        cfg.populationSize         = 100;
        cfg.evalsPerStrategy       = 100000;
        cfg.numStrategiesSchedObj  = 4;  // S1..S4
        cfg.numStrategiesMaxShift  = 4;  // S1=upper=0, S2=T/8, S3=T/4, S4=T/2

        // ---- 感度分析（MaxShift 上限 T/4 の妥当性検証） ----
        {
            MaxShiftSensitivityAnalyzer sa(instanceFile, 0.0, false);
            sa.runAndSave("maxshift_sensitivity_RR000_RV0.csv", 40);
        }

        // ---- P1/P2/P3 × SchedObj/MaxShift 全比較 ----
        EncodingComparisonRunner_All runner(cfg);
        auto t0 = std::chrono::steady_clock::now();
        runner.runAll();
        double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
        cout << "[TIME] " << elapsed << " s\n";

        return 0;
    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
