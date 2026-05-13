// ============================================================
//  main_NSGAII_RCPSP_EncodingComparison.cpp
//
//  エンコーディング比較ランナー
//  ─ 「従来の schedObj (0/1) エンコーディング」と
//    「新しい max_shift エンコーディング」を
//    同一インスタンス・同一条件で並べて実行し、
//    FUN/SCHED ファイルを出力する。
//
//  出力ファイル (cmake-build-*/ 直下):
//    FUN_ENC_<prefix>_<cond>_SchedObj.txt   ─ 従来エンコーディング
//    FUN_ENC_<prefix>_<cond>_MaxShift.txt   ─ 新エンコーディング
//    SCHED_ENC_<prefix>_<cond>_SchedObj.txt
//    SCHED_ENC_<prefix>_<cond>_MaxShift.txt
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
#include "operators/crossover/PermutationCrossover.h"
#include "operators/crossover/MaxShiftCrossover.h"
#include "operators/mutation/PermutationMutation.h"
#include "operators/mutation/MaxShiftMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"

using namespace std;

// ============================================================
//  EncodingComparisonRunner
// ============================================================
class EncodingComparisonRunner {
public:
    struct Config {
        string instanceFile;
        double rr              = 0.0;
        bool   rv              = false;
        int    populationSize  = 100;
        int    evalsPerStrategy = 50000;
        int    numStrategies   = 4;
    };

    explicit EncodingComparisonRunner(Config cfg)
        : cfg_(std::move(cfg))
        , prefix_(toBaseNoExt(cfg_.instanceFile))
    {}

    // 両エンコーディングで NSGA-II を実行し最終パレートフロントを返す
    //   enc=0 → schedObj (0/1)  enc=1 → max_shift
    SolutionSet* runEncoding(int enc) const;

    // FUN / SCHED ファイル出力
    void writeResultFiles(const string &outPrefix,
                          SolutionSet  *pareto,
                          RCPSP_Problem *prob) const;

    // 全 RR/RV 条件 × 両エンコーディングを実行
    void runAll() const;

private:
    Config cfg_;
    string prefix_;

    // schedObj エンコーディング (P1) 用オペレータをセット
    static void attachSchedObjOps(Algorithm *algo, RCPSP_Problem *prob);
    // max_shift エンコーディング用オペレータをセット
    static void attachMaxShiftOps(Algorithm *algo, RCPSP_Problem_MaxShift *prob);

    static string toCondTag(double rr, bool rv);
    static string toBaseNoExt(const string &path);
    static bool   fileExists(const string &p);
    static void   copyFileBinary(const string &src, const string &dst);
};

// ---- static ユーティリティ ----

string EncodingComparisonRunner::toCondTag(double rr, bool rv) {
    int rrInt = static_cast<int>(std::round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

string EncodingComparisonRunner::toBaseNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

bool EncodingComparisonRunner::fileExists(const string &p) {
    ifstream f(p.c_str(), ios::binary);
    return (bool)f;
}

void EncodingComparisonRunner::copyFileBinary(const string &src, const string &dst) {
    ifstream in(src.c_str(),  ios::binary);
    if (!in)  throw runtime_error("Cannot open: " + src);
    ofstream out(dst.c_str(), ios::binary);
    if (!out) throw runtime_error("Cannot open: " + dst);
    out << in.rdbuf();
}

// ---- オペレータ設定 ----

void EncodingComparisonRunner::attachSchedObjOps(Algorithm *algo, RCPSP_Problem *prob) {
    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob->getNumberOfVariables();
    algo->addOperator("crossover", new PermutationCrossover(crossP));
    algo->addOperator("mutation",  new PermutationMutation(mutP, prob));
    map<string, void*> sel;
    algo->addOperator("selection", new BinaryTournament2(sel));
}

void EncodingComparisonRunner::attachMaxShiftOps(Algorithm *algo,
                                                  RCPSP_Problem_MaxShift *prob) {
    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob->getNumberOfVariables();
    algo->addOperator("crossover", new MaxShiftCrossover(crossP));
    algo->addOperator("mutation",  new MaxShiftMutation(mutP, prob));
    map<string, void*> sel;
    algo->addOperator("selection", new BinaryTournament2(sel));
}

// ============================================================
//  runEncoding
//   enc == 0 : schedObj (0/1) エンコーディング (P1)
//   enc == 1 : max_shift エンコーディング
// ============================================================
SolutionSet* EncodingComparisonRunner::runEncoding(int enc) const {
    const string encTag = (enc == 0) ? "SchedObj" : "MaxShift";
    const string ctag   = toCondTag(cfg_.rr, cfg_.rv);

    cout << "\n------------------------------------------------------------\n";
    cout << "[Encoding=" << encTag << "] " << ctag
         << "  instance=" << cfg_.instanceFile << "\n";
    cout << "  popSize=" << cfg_.populationSize
         << "  evalsPerStrategy=" << cfg_.evalsPerStrategy
         << "  numStrategies=" << cfg_.numStrategies << "\n";
    cout << "------------------------------------------------------------\n";

    // 問題インスタンスを生成（両エンコーディングとも nVars=2n）
    RCPSP_Problem *prob = nullptr;
    RCPSP_Problem_MaxShift *probMS = nullptr;

    if (enc == 0) {
        prob = new RCPSP_Problem(cfg_.instanceFile, 1, cfg_.rr, cfg_.rv);
        prob->setMaxEvaluations(cfg_.evalsPerStrategy);
    } else {
        probMS = new RCPSP_Problem_MaxShift(cfg_.instanceFile, 1, cfg_.rr, cfg_.rv);
        probMS->setMaxEvaluations(cfg_.evalsPerStrategy);
        prob = probMS;
    }

    SolutionSet *combined = new SolutionSet(
            cfg_.numStrategies * cfg_.populationSize * 4);

    for (int s = 1; s <= cfg_.numStrategies; ++s) {
        cout << "  [" << encTag << " Strategy " << s << "/"
             << cfg_.numStrategies << "]\n";

        prob->setStrategy(s);
        prob->resetEvalCounter();
        prob->clearStartTimesCache();

        Algorithm *algo = new NSGAII(prob);
        int popSz  = cfg_.populationSize;
        int maxEv  = cfg_.evalsPerStrategy;
        int lsFlag = 0;
        algo->setInputParameter("populationSize", &popSz);
        algo->setInputParameter("maxEvaluations", &maxEv);
        algo->setInputParameter("useLocalSearch",  &lsFlag);

        if (enc == 0)
            attachSchedObjOps(algo, prob);
        else
            attachMaxShiftOps(algo, probMS);

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

    // 統合結果に非支配フィルタ
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
    delete prob;   // probMS は prob と同一ポインタなので二重 delete 不要

    cout << "[DONE] " << encTag << "  Pareto size=" << finalPareto->size() << "\n";
    return finalPareto;
}

// ============================================================
//  writeResultFiles: FUN / SCHED 出力
// ============================================================
void EncodingComparisonRunner::writeResultFiles(
        const string &outPrefix,
        SolutionSet  *pareto,
        RCPSP_Problem *prob) const
{
    const string funPath   = "FUN_ENC_"   + outPrefix + ".txt";
    const string schedPath = "SCHED_ENC_" + outPrefix + ".txt";

    ofstream funFile(funPath.c_str());
    ofstream schedFile(schedPath.c_str());

    int nJobs = prob->getNumJobs();
    int nRes  = prob->getNumResources();

    // SCHED ヘッダー
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
        {
            const vector<int> st = prob->computeStartTimes(sol);
            for (int j = 0; j < nJobs; ++j)
                schedFile << " " << (j < (int)st.size() ? st[j] : 0);
        }
        schedFile << "\n";
    }

    cout << "[FILES] " << funPath << " / " << schedPath << "\n";

    // コンソールに min-makespan / min-cost を表示
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

// ============================================================
//  runAll: 全 RR/RV 条件 × 両エンコーディングを実行
// ============================================================
void EncodingComparisonRunner::runAll() const {
    const string costsFile = "costs_" + prefix_ + ".csv";
    if (fileExists(costsFile)) copyFileBinary(costsFile, "costs.csv");

    struct Cond { double rr; bool rv; };
    const vector<Cond> conditions = {
        {0.00, false}, {0.00, true },
        {0.25, false}, {0.25, true },
        {0.50, false}, {0.50, true },
        {0.75, false}, {0.75, true },
    };

    // エンコーディング名とタグ
    const vector<pair<int,string>> encodings = {
        {0, "SchedObj"},   // 従来 0/1 エンコーディング
        {1, "MaxShift"},   // 新 max_shift エンコーディング
    };

    cout << "\n============================================================\n";
    cout << " Encoding Comparison Run\n";
    cout << " Instance : " << cfg_.instanceFile << "\n";
    cout << " popSize=" << cfg_.populationSize
         << "  evalsPerStrategy=" << cfg_.evalsPerStrategy
         << "  numStrategies=" << cfg_.numStrategies << "\n";
    cout << "============================================================\n";

    // 比較サマリ用テーブル（コンソール出力）
    cout << "\n"
         << left  << setw(14) << "Condition"
         << right << setw(12) << "MS_SchedObj"
         << setw(14) << "Cost_SchedObj"
         << setw(12) << "MS_MaxShift"
         << setw(14) << "Cost_MaxShift"
         << setw(10) << "ΔMS"
         << setw(12) << "ΔCost" << "\n";
    cout << string(88, '-') << "\n";

    for (const auto &c : conditions) {
        RCPSP_Problem::resetGlobalCostSeries();
        if (fileExists(costsFile)) copyFileBinary(costsFile, "costs.csv");

        const string ctag = toCondTag(c.rr, c.rv);

        double minMs[2]   = {1e9, 1e9};
        double minCost[2] = {1e9, 1e9};
        int    paretoSz[2]= {0, 0};

        for (const auto &[enc, encTag] : encodings) {
            Config modCfg = cfg_;
            modCfg.rr = c.rr;
            modCfg.rv = c.rv;
            EncodingComparisonRunner runner(modCfg);

            SolutionSet *pareto = runner.runEncoding(enc);

            // 出力用問題インスタンスを作成
            RCPSP_Problem *prob = nullptr;
            if (enc == 0)
                prob = new RCPSP_Problem(cfg_.instanceFile, 1, c.rr, c.rv);
            else
                prob = new RCPSP_Problem_MaxShift(cfg_.instanceFile, 1, c.rr, c.rv);

            const string outPrefix = prefix_ + "_" + ctag + "_" + encTag;
            runner.writeResultFiles(outPrefix, pareto, prob);

            // サマリ集計
            for (int i = 0; i < pareto->size(); ++i) {
                minMs[enc]   = std::min(minMs[enc],   pareto->get(i)->getObjective(0));
                minCost[enc] = std::min(minCost[enc], pareto->get(i)->getObjective(1));
            }
            paretoSz[enc] = pareto->size();

            delete pareto;
            delete prob;
        }

        // コンソールに比較サマリを表示
        double dMs   = minMs[1]   - minMs[0];
        double dCost = minCost[1] - minCost[0];
        cout << left  << setw(14) << ctag
             << right << setw(12) << (int)minMs[0]
             << setw(14) << fixed << setprecision(1) << minCost[0]
             << setw(12) << (int)minMs[1]
             << setw(14) << fixed << setprecision(1) << minCost[1]
             << setw(10) << (dMs >= 0 ? "+" : "") << (int)dMs
             << setw(12) << (dCost >= 0 ? "+" : "") << setprecision(1) << dCost
             << "\n";
    }

    cout << string(88, '-') << "\n";
    cout << "  ΔMS / ΔCost: MaxShift - SchedObj  (負 = MaxShift が優秀)\n";
    cout << "[ALL DONE] " << prefix_ << "\n\n";
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        const string defaultInstance = "j30.sm/j301_1.sm";
        const string instanceFile = (argc >= 2) ? string(argv[1]) : defaultInstance;

        EncodingComparisonRunner::Config cfg;
        cfg.instanceFile      = instanceFile;
        cfg.rr                = 0.0;
        cfg.rv                = false;
        cfg.populationSize    = 100;
        cfg.evalsPerStrategy  = 50000;
        cfg.numStrategies     = 4;

        EncodingComparisonRunner runner(cfg);
        auto t0 = std::chrono::steady_clock::now();
        runner.runAll();
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        cout << "[TIME] " << elapsed << " s\n";
        return 0;
    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
