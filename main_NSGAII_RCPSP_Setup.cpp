// ============================================================
//  main_NSGAII_RCPSP_Setup.cpp
//
//  NSGASetupRunner クラス
//  ─ P2/P3 × セットアップ時間モデル（TW / WD / WR）× NSGA-II の実験
//    ガントチャート出力（セットアップスロットは 'S' 表示）
//
//  【セットアップ時間モデル】
//   TW : t_i^s = floor(alphaS/2 * d_i)         全作業量依存（固定）
//   WD : t_i^s = floor(alphaS * x)               既遂作業量依存
//   WR : t_i^s = floor(alphaS * (d_i+1 - x))    残余作業量依存
//
//  【ガントチャート凡例】
//   '#' = 実行スロット
//   'S' = セットアップスロット（作業進まず資源占有）
//   '.' = スパン内ギャップ（中断・待機）
//   ' ' = 範囲外
// ============================================================

#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <set>
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
#include "problems/RCPSP_Problem_Splitting.h"
#include "problems/RCPSP_Problem_Setup.h"
#include "operators/crossover/PermutationCrossover.h"
#include "operators/mutation/PermutationMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"

using namespace std;

// ============================================================
//  NSGASetupRunner
//
//  P2/P3 × SetupModel(TW/WD/WR) × NSGA-II の実験クラス
// ============================================================
class NSGASetupRunner {
public:
    // ---- 実行設定 ------------------------------------------------
    struct Config {
        string     instanceFile;
        double     rr               = 0.0;
        bool       rv               = false;
        SetupModel setupModel       = SetupModel::WD;
        double     alphaS           = 0.5;
        int        populationSize   = 100;
        int        evalsPerStrategy = 50000;
        int        numStrategies    = 4;
    };

    explicit NSGASetupRunner(Config cfg)
        : cfg_(std::move(cfg))
        , prefix_(toBaseNoExt(cfg_.instanceFile))
    {}

    // ---- NSGA-II 実行 -------------------------------------------

    // P2/P3 を指定して NSGA-II を実行し、最終パレートフロントを返す
    SolutionSet* runMode(ActivitySplittingMode mode) const;

    // 結果ファイル (FUN/VAR/SCHED) を書き出す
    void writeResultFiles(const string        &outPrefix,
                          SolutionSet         *pareto,
                          RCPSP_Problem_Setup *prob,
                          ActivitySplittingMode mode) const;

    // ガントチャート（セットアップスロット 'S' 付き）を書き出す
    void writeGanttFile(const string        &ganttPath,
                        Solution            *sol,
                        RCPSP_Problem_Setup *prob,
                        ActivitySplittingMode mode) const;

    // 全 RR/RV 条件 × P2/P3 × SetupModel を実行
    void runAll() const;

    // 単一条件で実行（main から直接呼ぶ用）
    void runSingle() const;

private:
    Config cfg_;
    string prefix_;

    // 問題インスタンスを生成
    RCPSP_Problem_Setup* makeProblem(ActivitySplittingMode mode, int strategy) const;

    // NSGA-II オペレータをセット
    static void attachOperators(Algorithm *algo, RCPSP_Problem *prob);

    // ---- 文字列ユーティリティ -----------------------------------
    static string toModeTag(ActivitySplittingMode m);
    static string toSetupTag(SetupModel s);
    static string toCondTag(double rr, bool rv);
    static string toBaseNoExt(const string &path);
    static bool   fileExists(const string &p);
    static void   copyFileBinary(const string &src, const string &dst);
};

// ============================================================
//  static ユーティリティ
// ============================================================
string NSGASetupRunner::toModeTag(ActivitySplittingMode m) {
    switch (m) {
        case ActivitySplittingMode::P1: return "P1";
        case ActivitySplittingMode::P2: return "P2";
        case ActivitySplittingMode::P3: return "P3";
    }
    return "P2";
}

string NSGASetupRunner::toSetupTag(SetupModel s) {
    switch (s) {
        case SetupModel::TW: return "TW";
        case SetupModel::WD: return "WD";
        case SetupModel::WR: return "WR";
    }
    return "WD";
}

string NSGASetupRunner::toCondTag(double rr, bool rv) {
    int rrInt = static_cast<int>(std::round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

string NSGASetupRunner::toBaseNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

bool NSGASetupRunner::fileExists(const string &p) {
    ifstream f(p.c_str(), ios::binary);
    return (bool)f;
}

void NSGASetupRunner::copyFileBinary(const string &src, const string &dst) {
    ifstream in(src.c_str(), ios::binary);
    if (!in) throw runtime_error("Cannot open: " + src);
    ofstream out(dst.c_str(), ios::binary);
    if (!out) throw runtime_error("Cannot open: " + dst);
    out << in.rdbuf();
}

// ============================================================
//  makeProblem
// ============================================================
RCPSP_Problem_Setup* NSGASetupRunner::makeProblem(
        ActivitySplittingMode mode, int strategy) const
{
    return new RCPSP_Problem_Setup(
        cfg_.instanceFile,
        mode,
        cfg_.setupModel,
        cfg_.alphaS,
        strategy,
        cfg_.rr,
        cfg_.rv
    );
}

// ============================================================
//  attachOperators
// ============================================================
void NSGASetupRunner::attachOperators(Algorithm *algo, RCPSP_Problem *prob) {
    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob->getNumberOfVariables();

    Operator *crossover = new PermutationCrossover(crossP);
    Operator *mutation  = new PermutationMutation(mutP, prob);
    map<string, void*> selParams;
    Operator *selection = new BinaryTournament2(selParams);

    algo->addOperator("crossover", crossover);
    algo->addOperator("mutation",  mutation);
    algo->addOperator("selection", selection);
}

// ============================================================
//  runMode: P2/P3 の NSGA-II を numStrategies 回実行し
//           最終パレートフロントを返す
// ============================================================
SolutionSet* NSGASetupRunner::runMode(ActivitySplittingMode mode) const {
    const string mtag  = toModeTag(mode);
    const string stag  = toSetupTag(cfg_.setupModel);
    const string ctag  = toCondTag(cfg_.rr, cfg_.rv);

    cout << "\n============================================================\n";
    cout << "[NSGASetupRunner] mode=" << mtag
         << "  setup=" << stag
         << "  alphaS=" << cfg_.alphaS
         << "  " << ctag
         << "\n";
    cout << "  instance=" << cfg_.instanceFile
         << "  pop=" << cfg_.populationSize
         << "  evalsPerStrategy=" << cfg_.evalsPerStrategy << "\n";
    cout << "============================================================\n";

    RCPSP_Problem_Setup *prob = makeProblem(mode, 1);
    prob->setMaxEvaluations(cfg_.evalsPerStrategy);

    SolutionSet *combined = new SolutionSet(
        cfg_.numStrategies * cfg_.populationSize * 4);

    for (int s = 1; s <= cfg_.numStrategies; ++s) {
        cout << "  [" << mtag << "+" << stag
             << " Strategy " << s << "/" << cfg_.numStrategies
             << "]  maxEvals=" << cfg_.evalsPerStrategy << "\n";

        prob->setStrategy(s);
        prob->resetEvalCounter();
        prob->clearStartTimesCache();

        Algorithm *algo = new NSGAII(prob);
        int popSz  = cfg_.populationSize;
        int maxEv  = cfg_.evalsPerStrategy;
        int lsFlag = 0;
        algo->setInputParameter("populationSize", &popSz);
        algo->setInputParameter("maxEvaluations", &maxEv);
        algo->setInputParameter("useLocalSearch", &lsFlag);

        attachOperators(algo, prob);

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
    delete prob;

    cout << "[DONE] mode=" << mtag << "+" << stag
         << "  Final Pareto size=" << finalPareto->size() << "\n";
    return finalPareto;
}

// ============================================================
//  writeResultFiles: FUN / VAR / SCHED ファイル出力
// ============================================================
void NSGASetupRunner::writeResultFiles(
        const string        &outPrefix,
        SolutionSet         *pareto,
        RCPSP_Problem_Setup *prob,
        ActivitySplittingMode /*mode*/) const
{
    const string funPath   = "FUN_"   + outPrefix;
    const string varPath   = "VAR_"   + outPrefix;
    const string schedPath = "SCHED_" + outPrefix;

    ofstream funFile(funPath.c_str());
    ofstream varFile(varPath.c_str());
    ofstream schedFile(schedPath.c_str());

    int nJobs = prob->getNumJobs();
    int nRes  = prob->getNumResources();
    int nVar  = prob->getNumberOfVariables();

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
        Solution *sol   = pareto->get(i);
        Variable **vars = sol->getDecisionVariables();

        funFile << sol->getObjective(0) << " " << sol->getObjective(1) << "\n";

        for (int j = 0; j < nVar; ++j) {
            varFile << vars[j]->getValue();
            if (j + 1 < nVar) varFile << " ";
        }
        varFile << "\n";

        schedFile << sol->getObjective(0) << " " << sol->getObjective(1);
        {
            const vector<int> st = prob->computeStartTimes(sol);
            for (int j = 0; j < nJobs; ++j)
                schedFile << " " << (j < (int)st.size() ? st[j] : 0);
        }
        schedFile << "\n";
    }

    cout << "[FILES] " << funPath << " / " << varPath << " / " << schedPath << "\n";
}

// ============================================================
//  writeGanttFile: ガントチャート出力
//
//  凡例:
//   '#' = 実行スロット（execSlots_）
//   'S' = セットアップスロット（setupSlots_ があれば）
//         ※ RCPSP_Problem_Setup の evaluate() が sol->setupSlots_ に
//            セットアップスロットを格納している場合に有効
//   '.' = スパン内ギャップ（実行でもセットアップでもない待機）
//   ' ' = スパン外
// ============================================================
void NSGASetupRunner::writeGanttFile(
        const string        &ganttPath,
        Solution            *sol,
        RCPSP_Problem_Setup *prob,
        ActivitySplittingMode mode) const
{
    ofstream os(ganttPath.c_str());
    if (!os) {
        cerr << "[Gantt] Cannot open: " << ganttPath << "\n";
        return;
    }

    int n        = prob->getNumJobs();
    int makespan = (int)sol->getObjective(0);
    double cost  = sol->getObjective(1);
    const auto &dur      = prob->getDurations();
    const auto &execSlots = sol->execSlots_;
    const string mtag    = toModeTag(mode);
    const string stag    = toSetupTag(cfg_.setupModel);

    os << "======================================================================\n";
    os << " Gantt Chart [" << mtag << "+" << stag << "]"
       << "  alphaS=" << cfg_.alphaS << "\n";
    os << " '#'=実行  'S'=セットアップ  '.'=ギャップ  ' '=範囲外\n";
    os << " Instance  : " << cfg_.instanceFile << "\n";
    os << " Cond      : " << toCondTag(cfg_.rr, cfg_.rv) << "\n";
    os << " Makespan  : " << makespan << "\n";
    os << " Cost      : " << fixed << setprecision(2) << cost << "\n";
    os << "======================================================================\n\n";

    const int MAX_WIDTH = 80;
    int scale = 1;
    while ((makespan + 1) / scale > MAX_WIDTH) ++scale;
    int dispWidth = (makespan / scale) + 1;

    // 時刻ヘッダー
    os << "     ";
    for (int col = 0; col < dispWidth; ++col) {
        int t = col * scale;
        if (t % 10 == 0) {
            char lbl[8]; snprintf(lbl, sizeof(lbl), "%d", t);
            os << lbl[0];
        } else { os << ' '; }
    }
    os << "\n";

    os << "     ";
    for (int col = 0; col < dispWidth; ++col) {
        int t = col * scale;
        os << ('0' + (t % 10));
    }
    os << "\n";
    os << "  ---" << string(dispWidth, '-') << "\n";

    for (int j = 0; j < n; ++j) {
        int dj = (j < (int)dur.size()) ? dur[j] : 0;
        if (dj <= 0) {
            os << setw(4) << j << "| (d=0 dummy)\n";
            continue;
        }

        // execSlots から exec セット構築
        set<int> execSet;
        int firstExec = 0, lastExec = 0;
        if (j < (int)execSlots.size() && !execSlots[j].empty()) {
            for (int t : execSlots[j]) execSet.insert(t);
            firstExec = execSlots[j].front();
            lastExec  = execSlots[j].back();
        } else if (j < (int)sol->startTimes_.size()) {
            firstExec = sol->startTimes_[j];
            lastExec  = firstExec + dj - 1;
            for (int t = firstExec; t <= lastExec; ++t) execSet.insert(t);
        }

        // setupSlots_ が実装されていれば setup セット構築
        // （Solution クラスに setupSlots_ が追加されている場合に対応）
        // 現実装では execSlots_ のみ利用し、セットアップは '.' 表示にフォールバック
        // 将来 sol->setupSlots_[j] を追加したときにここを有効化する
        set<int> setupSet;
        // if (j < (int)sol->setupSlots_.size())
        //     for (int t : sol->setupSlots_[j]) setupSet.insert(t);

        os << setw(4) << j << "| ";

        int lastDisplayed = -1;  // セットアップスロットの最後の時刻（span 計算用）
        if (!setupSet.empty()) lastDisplayed = *setupSet.rbegin();
        lastDisplayed = max(lastDisplayed, lastExec);

        for (int col = 0; col < dispWidth; ++col) {
            int tFrom = col * scale;
            int tTo   = tFrom + scale - 1;

            bool hasExec  = false;
            bool hasSetup = false;
            bool inSpan   = false;

            for (int t2 = tFrom; t2 <= tTo; ++t2) {
                if (execSet.count(t2))  { hasExec  = true; break; }
                if (setupSet.count(t2)) { hasSetup = true; }
                if (t2 >= firstExec && t2 <= lastDisplayed) inSpan = true;
            }

            if (hasExec)        os << '#';
            else if (hasSetup)  os << 'S';
            else if (inSpan)    os << '.';
            else                os << ' ';
        }

        os << "  d=" << dj << " first=" << firstExec << " last=" << lastExec;
        int gaps = (lastExec - firstExec + 1) - dj;
        if (gaps > 0) os << " gaps=" << gaps;
        os << "\n";
    }

    os << "\n[Scale: 1 char = " << scale << " time unit(s)]\n";
    if (scale > 1) {
        os << "  (makespan=" << makespan
           << " > " << MAX_WIDTH << " => scaled for readability)\n";
    }
    os << "======================================================================\n";

    cout << "[Gantt] Written: " << ganttPath << "\n";
}

// ============================================================
//  runAll: 全 RR/RV × P2/P3 × SetupModel(TW/WD/WR) を実行
// ============================================================
void NSGASetupRunner::runAll() const {
    const string costsFile = "costs_" + prefix_ + ".csv";
    if (fileExists(costsFile)) {
        copyFileBinary(costsFile, "costs.csv");
        cout << "[COST] " << costsFile << " -> costs.csv\n";
    }

    struct Cond { double rr; bool rv; };
    const vector<Cond> conditions = {
        {0.00, false}, {0.00, true },
        {0.25, false}, {0.25, true },
        {0.50, false}, {0.50, true },
        {0.75, false}, {0.75, true },
    };
    const vector<ActivitySplittingMode> modes = {
        ActivitySplittingMode::P2,
        ActivitySplittingMode::P3,
    };
    const vector<SetupModel> setups = {
        SetupModel::TW,
        SetupModel::WD,
        SetupModel::WR,
    };

    for (const auto &c : conditions) {
        RCPSP_Problem::resetGlobalCostSeries();
        if (fileExists(costsFile)) copyFileBinary(costsFile, "costs.csv");

        const string ctag = toCondTag(c.rr, c.rv);

        for (auto sm : setups) {
            for (auto m : modes) {
                Config modCfg     = cfg_;
                modCfg.rr         = c.rr;
                modCfg.rv         = c.rv;
                modCfg.setupModel = sm;
                NSGASetupRunner runner(modCfg);

                SolutionSet *pareto = runner.runMode(m);

                RCPSP_Problem_Setup *prob = runner.makeProblem(m, 1);

                const string mtag      = toModeTag(m);
                const string stag      = toSetupTag(sm);
                const string outPrefix = prefix_ + "_" + ctag
                                       + "_" + stag + "_" + mtag;

                runner.writeResultFiles(outPrefix, pareto, prob, m);

                // min-makespan / min-cost のガントチャート出力
                if (pareto->size() > 0) {
                    int minMsIdx   = 0, minCostIdx = 0;
                    double minMs   = pareto->get(0)->getObjective(0);
                    double minCost = pareto->get(0)->getObjective(1);
                    for (int i = 1; i < pareto->size(); ++i) {
                        double ms   = pareto->get(i)->getObjective(0);
                        double cost = pareto->get(i)->getObjective(1);
                        if (ms   < minMs)   { minMs   = ms;   minMsIdx   = i; }
                        if (cost < minCost) { minCost = cost; minCostIdx = i; }
                    }

                    auto writeGantt = [&](int idx, const string &suffix) {
                        Solution *sol       = pareto->get(idx);
                        double funMs        = sol->getObjective(0);
                        double funCost      = sol->getObjective(1);
                        prob->setOutputMaxShift(0);
                        prob->evaluate(sol);
                        prob->setOutputMaxShift(-1);
                        sol->setObjective(0, funMs);
                        sol->setObjective(1, funCost);
                        const string gp = "GANTT_" + outPrefix + "_" + suffix + ".txt";
                        runner.writeGanttFile(gp, sol, prob, m);
                    };

                    writeGantt(minMsIdx, "MinMS");
                    if (minCostIdx != minMsIdx)
                        writeGantt(minCostIdx, "MinCost");
                }

                delete pareto;
                delete prob;
            }
        }
    }
    cout << "[BATCH] All conditions/setups/modes done for " << prefix_ << "\n\n";
}

// ============================================================
//  runSingle: 単一条件の実験（デバッグ・確認用）
// ============================================================
void NSGASetupRunner::runSingle() const {
    const string mtag  = toModeTag(ActivitySplittingMode::P2);
    const string stag  = toSetupTag(cfg_.setupModel);
    const string ctag  = toCondTag(cfg_.rr, cfg_.rv);

    const vector<ActivitySplittingMode> modes = {
        ActivitySplittingMode::P2,
        ActivitySplittingMode::P3,
    };

    for (auto m : modes) {
        SolutionSet *pareto = runMode(m);

        RCPSP_Problem_Setup *prob = makeProblem(m, 1);

        const string outPrefix = prefix_ + "_" + ctag
                               + "_" + stag + "_" + toModeTag(m);

        writeResultFiles(outPrefix, pareto, prob, m);

        if (pareto->size() > 0) {
            int minMsIdx   = 0;
            double minMs   = pareto->get(0)->getObjective(0);
            for (int i = 1; i < pareto->size(); ++i) {
                double ms = pareto->get(i)->getObjective(0);
                if (ms < minMs) { minMs = ms; minMsIdx = i; }
            }

            Solution *sol  = pareto->get(minMsIdx);
            double funMs   = sol->getObjective(0);
            double funCost = sol->getObjective(1);
            prob->setOutputMaxShift(0);
            prob->evaluate(sol);
            prob->setOutputMaxShift(-1);
            sol->setObjective(0, funMs);
            sol->setObjective(1, funCost);

            const string gp = "GANTT_" + outPrefix + "_MinMS.txt";
            writeGanttFile(gp, sol, prob, m);
        }

        delete pareto;
        delete prob;
    }
}

// ============================================================
//  main
//
//  コマンドライン引数:
//   argv[1] : インスタンスファイルパス（省略時 j30.sm/j301_1.sm）
//   argv[2] : セットアップモード TW/WD/WR（省略時 WD）
//   argv[3] : alphaS（省略時 0.5）
//   argv[4] : "all" のとき runAll(), それ以外 runSingle()（省略時 single）
// ============================================================
int main(int argc, char **argv) {
    try {
        const string defaultInstance = "j30.sm/j301_1.sm";
        const string instanceFile    = (argc >= 2) ? string(argv[1]) : defaultInstance;

        // セットアップモード解析
        SetupModel setupModel = SetupModel::WD;
        if (argc >= 3) {
            string sm = string(argv[2]);
            if      (sm == "TW") setupModel = SetupModel::TW;
            else if (sm == "WD") setupModel = SetupModel::WD;
            else if (sm == "WR") setupModel = SetupModel::WR;
            else {
                cerr << "[WARN] Unknown setup model '" << sm
                     << "'. Using WD.\n";
            }
        }

        double alphaS = 0.5;
        if (argc >= 4) {
            try { alphaS = std::stod(argv[3]); }
            catch (...) {
                cerr << "[WARN] Invalid alphaS '" << argv[3]
                     << "'. Using 0.5.\n";
            }
        }

        bool doAll = (argc >= 5 && string(argv[4]) == "all");

        NSGASetupRunner::Config cfg;
        cfg.instanceFile      = instanceFile;
        cfg.rr                = 0.0;
        cfg.rv                = false;
        cfg.setupModel        = setupModel;
        cfg.alphaS            = alphaS;
        cfg.populationSize    = 100;
        cfg.evalsPerStrategy  = 50000;
        cfg.numStrategies     = 4;

        NSGASetupRunner runner(cfg);

        auto t0 = std::chrono::steady_clock::now();
        if (doAll) {
            runner.runAll();
        } else {
            runner.runSingle();
        }
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        cout << "[ALL DONE]\n";
        cout << "[TIME] " << elapsed << " s\n";
        return 0;

    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
