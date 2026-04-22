// ============================================================
//  main_NSGAII_RCPSP_ActivitySplitting.cpp
//
//  NSGASplittingRunner クラス
//  ─ P1/P2/P3 × NSGA-II の実験・テスト・ガントチャート出力を
//    一つのクラスにまとめたメインファイル
//
//  【ユニットテスト一覧】
//   Test1: Proposition 2 確認 (RR=0,RV=0 で P1==P2)
//   Test2: 包含関係  P1.makespan >= P2.makespan >= P3.makespan
//   Test3: 資源制約遵守  Σ_j r_jk x_jt ≤ U_kt  ∀t,k
//   Test4: 処理時間保証  Σ_t x_jt = d_j  ∀j
//
//  【ガントチャート】
//   P2/P3: バー開始位置 = 最初の実行時刻 (first_exec_time)
//          '#'=実行、'.'=スパン内ギャップ、' '=範囲外
//   タイトルに first_exec_time である旨を明記
// ============================================================

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
#include <cassert>
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
#include "operators/crossover/PermutationCrossover.h"
#include "operators/mutation/PermutationMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"

using namespace std;

// ============================================================
//  NSGASplittingRunner
//
//  P1/P2/P3 × NSGA-II の実験・テスト・ガントチャート出力
// ============================================================
class NSGASplittingRunner {
public:
    // ---- 実行設定 ------------------------------------------------
    struct Config {
        string instanceFile;
        double rr            = 0.0;
        bool   rv            = false;
        int    populationSize   = 100;
        int    evalsPerStrategy = 50000;
        int    numStrategies    = 4;
    };

    explicit NSGASplittingRunner(Config cfg)
        : cfg_(std::move(cfg))
        , prefix_(toBaseNoExt(cfg_.instanceFile))
    {}

    // ---- NSGA-II 実行 -------------------------------------------

    // P1/P2/P3 を指定して NSGA-II を実行し、最終パレートフロントを返す（呼び出し元が delete する）
    SolutionSet* runMode(ActivitySplittingMode mode) const;

    // 結果ファイル (FUN/VAR/SCHED) を書き出す
    void writeResultFiles(const string &outPrefix,
                          SolutionSet  *pareto,
                          RCPSP_Problem *prob,
                          ActivitySplittingMode mode) const;

    // ガントチャートをファイルに書き出す（min-makespan 解）
    void writeGanttFile(const string &ganttPath,
                        Solution      *sol,
                        RCPSP_Problem *prob,
                        ActivitySplittingMode mode) const;

    // 全 RR/RV 条件 × P1/P2/P3 を実行
    void runAll() const;

    // ---- ユニットテスト (static) --------------------------------

    // Test 1: Proposition 2 (RR=0, RV=0 で P1==P2)
    static bool test1_Proposition2(const string &instanceFile, int nTrials = 20);

    // Test 2: makespan 包含関係  P1 >= P2 >= P3
    static bool test2_InclusionRelation(const string &instanceFile, int nTrials = 20);

    // Test 3: 資源制約遵守  Σ_j r_jk x_jt ≤ U_kt ∀t,k
    static bool test3_ResourceConstraint(const string &instanceFile, int nTrials = 10);

    // Test 4: 処理時間保証  |execSlots_[j]| == d_j ∀j
    static bool test4_ProcessingTime(const string &instanceFile, int nTrials = 10);

    // Test 1〜4 をすべて実行（いずれか失敗したら false）
    static bool runAllTests(const string &instanceFile);

private:
    Config cfg_;
    string prefix_;

    // ---- 内部ヘルパー -------------------------------------------

    // Problem インスタンスを生成（モード・戦略を指定）
    RCPSP_Problem* makeProblem(ActivitySplittingMode mode, int strategy) const;

    // NSGA-II オペレータを生成して algorithm にセットする
    static void attachOperators(Algorithm *algo, RCPSP_Problem *prob);

    // ---- 文字列ユーティリティ -----------------------------------
    static string toModeTag(ActivitySplittingMode m);
    static string toCondTag(double rr, bool rv);
    static string toBaseNoExt(const string &path);
    static bool   fileExists(const string &p);
    static void   copyFileBinary(const string &src, const string &dst);
};

// ============================================================
//  static ユーティリティ
// ============================================================
string NSGASplittingRunner::toModeTag(ActivitySplittingMode m) {
    switch (m) {
        case ActivitySplittingMode::P1: return "P1";
        case ActivitySplittingMode::P2: return "P2";
        case ActivitySplittingMode::P3: return "P3";
    }
    return "P1";
}

string NSGASplittingRunner::toCondTag(double rr, bool rv) {
    int rrInt = static_cast<int>(std::round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

string NSGASplittingRunner::toBaseNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

bool NSGASplittingRunner::fileExists(const string &p) {
    ifstream f(p.c_str(), ios::binary);
    return (bool)f;
}

void NSGASplittingRunner::copyFileBinary(const string &src, const string &dst) {
    ifstream in(src.c_str(), ios::binary);
    if (!in) throw runtime_error("Cannot open: " + src);
    ofstream out(dst.c_str(), ios::binary);
    if (!out) throw runtime_error("Cannot open: " + dst);
    out << in.rdbuf();
}

// ============================================================
//  makeProblem
// ============================================================
RCPSP_Problem* NSGASplittingRunner::makeProblem(
        ActivitySplittingMode mode, int strategy) const
{
    if (mode == ActivitySplittingMode::P1) {
        return new RCPSP_Problem(cfg_.instanceFile, strategy, cfg_.rr, cfg_.rv);
    } else {
        return new RCPSP_Problem_Splitting(cfg_.instanceFile, mode, strategy, cfg_.rr, cfg_.rv);
    }
}

// ============================================================
//  attachOperators
// ============================================================
void NSGASplittingRunner::attachOperators(Algorithm *algo, RCPSP_Problem *prob) {
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
//  runMode: P1/P2/P3 の NSGA-II を numStrategies 回実行し
//           最終パレートフロントを返す
// ============================================================
SolutionSet* NSGASplittingRunner::runMode(ActivitySplittingMode mode) const {
    const string mtag = toModeTag(mode);
    const string ctag = toCondTag(cfg_.rr, cfg_.rv);

    cout << "\n============================================================\n";
    cout << "[NSGASplittingRunner] mode=" << mtag
         << "  " << ctag
         << "  instance=" << cfg_.instanceFile << "\n";
    cout << "  popSize=" << cfg_.populationSize
         << "  evalsPerStrategy=" << cfg_.evalsPerStrategy
         << "  numStrategies=" << cfg_.numStrategies << "\n";
    cout << "============================================================\n";

    RCPSP_Problem *prob = makeProblem(mode, 1);
    prob->setMaxEvaluations(cfg_.evalsPerStrategy);

    SolutionSet *combined = new SolutionSet(
            cfg_.numStrategies * cfg_.populationSize * 4);

    for (int s = 1; s <= cfg_.numStrategies; ++s) {
        cout << "  [" << mtag << " Strategy " << s << "/" << cfg_.numStrategies
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

    cout << "[DONE] mode=" << mtag
         << "  Final Pareto size=" << finalPareto->size() << "\n";
    return finalPareto;
}

// ============================================================
//  writeResultFiles: FUN / VAR / SCHED ファイル出力
// ============================================================
void NSGASplittingRunner::writeResultFiles(
        const string &outPrefix,
        SolutionSet  *pareto,
        RCPSP_Problem *prob,
        ActivitySplittingMode mode) const
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
        Solution *sol = pareto->get(i);
        Variable **vars = sol->getDecisionVariables();

        double origMS   = sol->getObjective(0);
        double origCost = sol->getObjective(1);
        funFile << origMS << " " << origCost << "\n";

        for (int j = 0; j < nVar; ++j) {
            varFile << vars[j]->getValue();
            if (j + 1 < nVar) varFile << " ";
        }
        varFile << "\n";

        // ESS 再評価して開始時刻を確定させる
        prob->setOutputMaxShift(0);
        prob->evaluate(sol);
        prob->setOutputMaxShift(-1);

        schedFile << sol->getObjective(0) << " " << sol->getObjective(1);
        const vector<int> &st = sol->startTimes_;
        for (int j = 0; j < nJobs; ++j) {
            schedFile << " " << (j < (int)st.size() ? st[j] : 0);
        }
        schedFile << "\n";
    }

    cout << "[FILES] " << funPath << " / " << varPath << " / " << schedPath << "\n";
}

// ============================================================
//  writeGanttFile: ガントチャートをテキストファイルに出力
//
//  P1:  各バーは連続ブロック（start_j から d_j スロット）
//  P2/P3: バー開始位置 = 最初の実行時刻 (first_exec_time)
//           '#' = 実行スロット、'.' = スパン内ギャップ、' ' = 範囲外
//
//  タイトルに P2/P3 は「バー開始 = 最初の実行時刻」である旨を明記する。
// ============================================================
void NSGASplittingRunner::writeGanttFile(
        const string &ganttPath,
        Solution      *sol,
        RCPSP_Problem *prob,
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
    const auto &dur = prob->getDurations();
    const auto &execSlots = sol->execSlots_;
    const string mtag = toModeTag(mode);

    // ---- タイトル ----
    os << "======================================================================\n";
    if (mode == ActivitySplittingMode::P1) {
        os << " Gantt Chart [P1]\n";
        os << " 各バーは連続した実行ブロック（開始時刻 = start_j）を示す\n";
    } else {
        os << " Gantt Chart [" << mtag << "]\n";
        os << " バーの開始位置 = 最初の実行時刻 (first_exec_time)\n";
        os << " '#' = 実行スロット  '.' = スパン内ギャップ（中断）  ' ' = 範囲外\n";
    }
    os << " Instance : " << cfg_.instanceFile << "\n";
    os << " Makespan : " << makespan << "\n";
    os << " Cost     : " << fixed << setprecision(2) << cost << "\n";
    os << "======================================================================\n\n";

    // ---- スケールを決める（表示幅 = min(makespan+1, 80)）----
    // makespan が大きい場合は 1 文字 = scale タイムユニットで表示する
    const int MAX_WIDTH = 80;
    int scale = 1;
    while ((makespan + 1) / scale > MAX_WIDTH) ++scale;
    int dispWidth = (makespan / scale) + 1;

    // ---- 時刻ヘッダー ----
    // 10 の位
    os << "     ";
    for (int col = 0; col < dispWidth; ++col) {
        int t = col * scale;
        if (t % 10 == 0) {
            // 3 桁以内のラベル
            char lbl[8];
            snprintf(lbl, sizeof(lbl), "%d", t);
            // ラベルが収まるなら先頭文字だけ出す（重ならないように）
            os << lbl[0];
        } else {
            os << ' ';
        }
    }
    os << "\n";

    // 1 の位
    os << "     ";
    for (int col = 0; col < dispWidth; ++col) {
        int t = col * scale;
        char c = '0' + (t % 10);
        os << c;
    }
    os << "\n";

    os << "  ---" << string(dispWidth, '-') << "\n";

    // ---- 各ジョブのバー ----
    for (int j = 0; j < n; ++j) {
        int dj = (j < (int)dur.size()) ? dur[j] : 0;
        if (dj <= 0) {
            os << setw(4) << j << "| (d=0 dummy)\n";
            continue;
        }

        // execSlots が空の場合は startTimes_ から再構成（フォールバック）
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

        os << setw(4) << j << "| ";

        for (int col = 0; col < dispWidth; ++col) {
            int tFrom = col * scale;
            int tTo   = tFrom + scale - 1;  // このカラムが表す時刻範囲

            // このカラム [tFrom, tTo] に execSlot が 1 つでもあれば '#'
            // execSlot はないが [firstExec, lastExec] 内なら '.'（ギャップ）
            // 範囲外なら ' '
            bool hasExec = false;
            bool inSpan  = false;
            for (int t2 = tFrom; t2 <= tTo; ++t2) {
                if (execSet.count(t2)) { hasExec = true; break; }
                if (t2 >= firstExec && t2 <= lastExec) inSpan = true;
            }

            if (hasExec) {
                os << '#';
            } else if (inSpan && mode != ActivitySplittingMode::P1) {
                os << '.';
            } else {
                os << ' ';
            }
        }

        // 右端に情報
        os << "  d=" << dj << " first=" << firstExec << " last=" << lastExec;
        if (mode != ActivitySplittingMode::P1) {
            int gaps = (lastExec - firstExec + 1) - dj;
            if (gaps > 0) os << " gaps=" << gaps;
        }
        os << "\n";
    }

    os << "\n[Scale: 1 char = " << scale << " time unit(s)]\n";
    if (scale > 1) {
        os << "  (makespan=" << makespan
           << " > " << MAX_WIDTH << " => scaled down for readability)\n";
    }
    os << "======================================================================\n";

    cout << "[Gantt] Written: " << ganttPath << "\n";
}

// ============================================================
//  runAll: 全 RR/RV 条件 × P1/P2/P3 を実行
// ============================================================
void NSGASplittingRunner::runAll() const {
    const string costsFile = "costs_" + prefix_ + ".csv";
    if (fileExists(costsFile)) {
        copyFileBinary(costsFile, "costs.csv");
        cout << "[COST] " << costsFile << " -> costs.csv\n";
    } else {
        cout << "[COST] " << costsFile << " not found. Will be auto-generated.\n";
    }

    struct Cond { double rr; bool rv; };
    const vector<Cond> conditions = {
        {0.00, false}, {0.00, true },
        {0.25, false}, {0.25, true },
        {0.50, false}, {0.50, true },
        {0.75, false}, {0.75, true },
    };
    const vector<ActivitySplittingMode> modes = {
        ActivitySplittingMode::P1,
        ActivitySplittingMode::P2,
        ActivitySplittingMode::P3,
    };

    for (const auto &c : conditions) {
        RCPSP_Problem::resetGlobalCostSeries();
        if (fileExists(costsFile)) copyFileBinary(costsFile, "costs.csv");

        const string ctag = toCondTag(c.rr, c.rv);

        for (auto m : modes) {
            // このモード用のランナー（rr/rv を条件に合わせる）
            Config modCfg  = cfg_;
            modCfg.rr      = c.rr;
            modCfg.rv      = c.rv;
            NSGASplittingRunner runner(modCfg);

            SolutionSet *pareto = runner.runMode(m);

            // 問題インスタンス（出力用）を再作成
            RCPSP_Problem *prob = runner.makeProblem(m, 1);

            // ファイル名プレフィックス: <prefix>_<condTag>_<modeTag>
            const string mtag      = toModeTag(m);
            const string outPrefix = prefix_ + "_" + ctag + "_" + mtag;

            runner.writeResultFiles(outPrefix, pareto, prob, m);

            // min-makespan 解でガントチャートを出力
            if (pareto->size() > 0) {
                int minIdx = 0;
                double minMs = pareto->get(0)->getObjective(0);
                for (int i = 1; i < pareto->size(); ++i) {
                    double ms = pareto->get(i)->getObjective(0);
                    if (ms < minMs) { minMs = ms; minIdx = i; }
                }
                Solution *bestSol = pareto->get(minIdx);

                // ESS 再評価して execSlots_ を確定
                prob->setOutputMaxShift(0);
                prob->evaluate(bestSol);
                prob->setOutputMaxShift(-1);

                const string ganttPath = "GANTT_" + outPrefix + ".txt";
                runner.writeGanttFile(ganttPath, bestSol, prob, m);
            }

            delete pareto;
            delete prob;
        }
    }
    cout << "[BATCH] All conditions/modes done for " << prefix_ << "\n\n";
}

// ============================================================
//  Test 1: Proposition 2
//   RR=0, RV=0 のとき P1 と P2 は完全に同一スケジュールを生成する
// ============================================================
bool NSGASplittingRunner::test1_Proposition2(
        const string &instanceFile, int nTrials)
{
    cout << "\n=== Test 1: Proposition 2 (RR=0, RV=0 => P1 == P2) ===\n";
    cout << "  instance=" << instanceFile << "  trials=" << nTrials << "\n";

    RCPSP_Problem           p1(instanceFile, 1, 0.0, false);
    RCPSP_Problem_Splitting p2(instanceFile, ActivitySplittingMode::P2, 1, 0.0, false);
    p1.setOutputMaxShift(0);
    p2.setOutputMaxShift(0);
    RCPSP_Problem::resetGlobalCostSeries();

    const int nJobs = p1.getNumJobs();
    const int nVars = p1.getNumberOfVariables();
    int pass = 0, fail = 0;

    for (int trial = 0; trial < nTrials; ++trial) {
        Solution *sol1 = p1.createRandomTopoSolution();
        Solution *sol2 = p2.createRandomTopoSolution();

        Variable **v1 = sol1->getDecisionVariables();
        Variable **v2 = sol2->getDecisionVariables();
        // 同じ活動順序・schedObj=0 を設定
        for (int i = 0; i < nVars; ++i) v2[i]->setValue(v1[i]->getValue());
        for (int j = 0; j < nJobs; ++j) {
            v1[nJobs + j]->setValue(0.0);
            v2[nJobs + j]->setValue(0.0);
        }

        p1.evaluate(sol1);
        p2.evaluate(sol2);

        const int ms1 = (int)sol1->getObjective(0);
        const int ms2 = (int)sol2->getObjective(0);
        bool ok = (ms1 == ms2);
        if (ok && (int)sol1->startTimes_.size() == nJobs
                && (int)sol2->startTimes_.size() == nJobs) {
            for (int j = 0; j < nJobs && ok; ++j) {
                ok = (sol1->startTimes_[j] == sol2->startTimes_[j]);
            }
        } else { ok = false; }

        if (ok) {
            ++pass;
            cout << "  Trial " << (trial + 1) << ": PASS (makespan=" << ms1 << ")\n";
        } else {
            ++fail;
            cerr << "  Trial " << (trial + 1) << ": FAIL"
                 << "  P1.ms=" << ms1 << "  P2.ms=" << ms2 << "\n";
            for (int j = 0; j < nJobs
                         && j < (int)sol1->startTimes_.size()
                         && j < (int)sol2->startTimes_.size(); ++j) {
                if (sol1->startTimes_[j] != sol2->startTimes_[j]) {
                    cerr << "    job " << j
                         << " P1.st=" << sol1->startTimes_[j]
                         << " P2.st=" << sol2->startTimes_[j] << "\n";
                }
            }
        }

        assert(ok &&
               "Test1 [Proposition 2]: P1 and P2 must produce identical schedules "
               "when RR=0, RV=0, schedObj=0.");

        delete sol1;
        delete sol2;
    }

    p1.setOutputMaxShift(-1);
    p2.setOutputMaxShift(-1);
    cout << "  Result: " << pass << "/" << nTrials
         << (fail == 0 ? " [PASS]\n" : " [FAIL]\n");
    return fail == 0;
}

// ============================================================
//  Test 2: 包含関係  P1.makespan >= P2.makespan >= P3.makespan
// ============================================================
bool NSGASplittingRunner::test2_InclusionRelation(
        const string &instanceFile, int nTrials)
{
    cout << "\n=== Test 2: Inclusion Relation P1.ms >= P2.ms >= P3.ms ===\n";
    cout << "  instance=" << instanceFile << "  trials=" << nTrials << "\n";

    // RR=0.25, RV=false で検証（資源変動がある条件）
    const double rr = 0.25;
    const bool   rv = false;
    RCPSP_Problem           p1(instanceFile, 1, rr, rv);
    RCPSP_Problem_Splitting p2(instanceFile, ActivitySplittingMode::P2, 1, rr, rv);
    RCPSP_Problem_Splitting p3(instanceFile, ActivitySplittingMode::P3, 1, rr, rv);

    // P1 の capacity_t を P2・P3 に共有する
    // → 3インスタンスが同一の時間依存資源上限プロファイルを持つ
    p2.setCapacityT(p1.getCapacityT());
    p3.setCapacityT(p1.getCapacityT());

    p1.setOutputMaxShift(0);
    p2.setOutputMaxShift(0);
    p3.setOutputMaxShift(0);
    RCPSP_Problem::resetGlobalCostSeries();

    const int nJobs = p1.getNumJobs();
    const int nVars = p1.getNumberOfVariables();
    int pass = 0, fail = 0;

    for (int trial = 0; trial < nTrials; ++trial) {
        Solution *sol1 = p1.createRandomTopoSolution();
        Solution *sol2 = p2.createRandomTopoSolution();
        Solution *sol3 = p3.createRandomTopoSolution();

        Variable **v1 = sol1->getDecisionVariables();
        Variable **v2 = sol2->getDecisionVariables();
        Variable **v3 = sol3->getDecisionVariables();

        // 同じ活動順序・schedObj=0
        for (int i = 0; i < nVars; ++i) {
            v2[i]->setValue(v1[i]->getValue());
            v3[i]->setValue(v1[i]->getValue());
        }
        for (int j = 0; j < nJobs; ++j) {
            v1[nJobs + j]->setValue(0.0);
            v2[nJobs + j]->setValue(0.0);
            v3[nJobs + j]->setValue(0.0);
        }

        p1.evaluate(sol1);
        p2.evaluate(sol2);
        p3.evaluate(sol3);

        int ms1 = (int)sol1->getObjective(0);
        int ms2 = (int)sol2->getObjective(0);
        int ms3 = (int)sol3->getObjective(0);

        // ms1 >= ms2 >= ms3 (等号含む)
        bool ok = (ms1 >= ms2) && (ms2 >= ms3);

        if (ok) {
            ++pass;
            cout << "  Trial " << (trial + 1) << ": PASS"
                 << "  P1=" << ms1 << " P2=" << ms2 << " P3=" << ms3 << "\n";
        } else {
            ++fail;
            cerr << "  Trial " << (trial + 1) << ": FAIL"
                 << "  P1=" << ms1 << " P2=" << ms2 << " P3=" << ms3
                 << (ms1 < ms2 ? " [P1<P2!]" : "")
                 << (ms2 < ms3 ? " [P2<P3!]" : "") << "\n";
        }

        assert(ok &&
               "Test2: Inclusion relation P1.makespan >= P2.makespan >= P3.makespan violated.");

        delete sol1;
        delete sol2;
        delete sol3;
    }

    cout << "  Result: " << pass << "/" << nTrials
         << (fail == 0 ? " [PASS]\n" : " [FAIL]\n");
    return fail == 0;
}

// ============================================================
//  Test 3: 資源制約遵守  Σ_j r_jk * x_jt ≤ U_kt ∀t,k
// ============================================================
bool NSGASplittingRunner::test3_ResourceConstraint(
        const string &instanceFile, int nTrials)
{
    cout << "\n=== Test 3: Resource Constraint Σ r_jk*x_jt <= U_kt ===\n";
    cout << "  instance=" << instanceFile << "  trials=" << nTrials << "\n";

    const double rr = 0.25;
    const bool   rv = true;   // RV あり条件でテスト（より厳しい）

    // 全モード検証
    const vector<ActivitySplittingMode> modes = {
        ActivitySplittingMode::P1,
        ActivitySplittingMode::P2,
        ActivitySplittingMode::P3,
    };

    int totalPass = 0, totalFail = 0;

    for (auto mode : modes) {
        RCPSP_Problem *prob =
            (mode == ActivitySplittingMode::P1)
            ? static_cast<RCPSP_Problem*>(new RCPSP_Problem(instanceFile, 1, rr, rv))
            : static_cast<RCPSP_Problem*>(new RCPSP_Problem_Splitting(instanceFile, mode, 1, rr, rv));

        prob->setOutputMaxShift(0);
        RCPSP_Problem::resetGlobalCostSeries();

        const int nJobs = prob->getNumJobs();
        const int nRes  = prob->getNumResources();
        const auto &demand = prob->getDemand();
        const auto &cap_t  = prob->getCapacityT();
        const auto &cap    = prob->getCapacity();
        const string mtag  = (mode == ActivitySplittingMode::P1) ? "P1" :
                             (mode == ActivitySplittingMode::P2) ? "P2" : "P3";

        cout << "  Mode=" << mtag << "\n";
        int modePass = 0, modeFail = 0;

        for (int trial = 0; trial < nTrials; ++trial) {
            Solution *sol = prob->createRandomTopoSolution();
            prob->evaluate(sol);

            // 使用量テーブルを execSlots_ から再構成してチェック
            // まず最大時刻を求める
            int T = (int)sol->getObjective(0) + 5;
            for (int j = 0; j < nJobs; ++j) {
                if (j < (int)sol->execSlots_.size() && !sol->execSlots_[j].empty()) {
                    T = std::max(T, sol->execSlots_[j].back() + 1);
                }
            }
            if (!cap_t.empty() && !cap_t[0].empty()) {
                T = std::max(T, (int)cap_t[0].size());
            }

            vector<vector<int>> usage(nRes, vector<int>(T, 0));
            for (int j = 0; j < nJobs; ++j) {
                if (j >= (int)sol->execSlots_.size()) continue;
                for (int t : sol->execSlots_[j]) {
                    if (t < T) {
                        for (int k = 0; k < nRes; ++k) {
                            usage[k][t] += demand[j][k];
                        }
                    }
                }
            }

            bool ok = true;
            int violT = -1, violK = -1;
            int violUsage = 0, violCap = 0;
            for (int t = 0; t < T && ok; ++t) {
                for (int k = 0; k < nRes && ok; ++k) {
                    int capKT;
                    if (!cap_t.empty() && k < (int)cap_t.size()
                            && t < (int)cap_t[k].size()) {
                        capKT = cap_t[k][t];
                    } else {
                        capKT = cap[k];
                    }
                    if (usage[k][t] > capKT) {
                        ok = false;
                        violT = t; violK = k;
                        violUsage = usage[k][t]; violCap = capKT;
                    }
                }
            }

            if (ok) {
                ++modePass;
                ++totalPass;
            } else {
                ++modeFail;
                ++totalFail;
                cerr << "    [" << mtag << "] Trial " << (trial + 1) << ": FAIL"
                     << " t=" << violT << " k=" << violK
                     << " usage=" << violUsage << " > cap=" << violCap << "\n";
            }

            assert(ok &&
                   "Test3: Resource constraint Σ r_jk*x_jt <= U_kt violated.");

            delete sol;
        }
        cout << "    " << mtag << ": " << modePass << "/" << nTrials
             << (modeFail == 0 ? " [PASS]\n" : " [FAIL]\n");

        prob->setOutputMaxShift(-1);
        delete prob;
    }

    cout << "  Total: " << totalPass << "/" << (nTrials * 3)
         << (totalFail == 0 ? " [PASS]\n" : " [FAIL]\n");
    return totalFail == 0;
}

// ============================================================
//  Test 4: 処理時間保証  |execSlots_[j]| == d_j ∀j
// ============================================================
bool NSGASplittingRunner::test4_ProcessingTime(
        const string &instanceFile, int nTrials)
{
    cout << "\n=== Test 4: Processing Time |execSlots[j]| == d_j ===\n";
    cout << "  instance=" << instanceFile << "  trials=" << nTrials << "\n";

    const double rr = 0.25;
    const bool   rv = true;

    const vector<ActivitySplittingMode> modes = {
        ActivitySplittingMode::P1,
        ActivitySplittingMode::P2,
        ActivitySplittingMode::P3,
    };

    int totalPass = 0, totalFail = 0;

    for (auto mode : modes) {
        RCPSP_Problem *prob =
            (mode == ActivitySplittingMode::P1)
            ? static_cast<RCPSP_Problem*>(new RCPSP_Problem(instanceFile, 1, rr, rv))
            : static_cast<RCPSP_Problem*>(new RCPSP_Problem_Splitting(instanceFile, mode, 1, rr, rv));

        prob->setOutputMaxShift(0);
        RCPSP_Problem::resetGlobalCostSeries();

        const int nJobs = prob->getNumJobs();
        const auto &dur = prob->getDurations();
        const string mtag = (mode == ActivitySplittingMode::P1) ? "P1" :
                            (mode == ActivitySplittingMode::P2) ? "P2" : "P3";

        cout << "  Mode=" << mtag << "\n";
        int modePass = 0, modeFail = 0;

        for (int trial = 0; trial < nTrials; ++trial) {
            Solution *sol = prob->createRandomTopoSolution();
            prob->evaluate(sol);

            bool ok = true;
            int violJ = -1, violSlots = 0, violDur = 0;

            // execSlots_ が正しく設定されているかチェック
            if ((int)sol->execSlots_.size() < nJobs) {
                ok = false;
                cerr << "    [" << mtag << "] Trial " << (trial + 1)
                     << ": FAIL execSlots_.size()=" << sol->execSlots_.size()
                     << " < nJobs=" << nJobs << "\n";
            } else {
                for (int j = 0; j < nJobs && ok; ++j) {
                    int dj = (j < (int)dur.size()) ? dur[j] : 0;
                    int slots = (int)sol->execSlots_[j].size();
                    if (slots != dj) {
                        ok = false;
                        violJ = j; violSlots = slots; violDur = dj;
                    }
                }
            }

            if (ok) {
                ++modePass;
                ++totalPass;
            } else {
                ++modeFail;
                ++totalFail;
                if (violJ >= 0) {
                    cerr << "    [" << mtag << "] Trial " << (trial + 1) << ": FAIL"
                         << " job=" << violJ
                         << " execSlots=" << violSlots
                         << " != d_j=" << violDur << "\n";
                }
            }

            assert(ok &&
                   "Test4: Processing time |execSlots[j]| == d_j violated.");

            delete sol;
        }
        cout << "    " << mtag << ": " << modePass << "/" << nTrials
             << (modeFail == 0 ? " [PASS]\n" : " [FAIL]\n");

        prob->setOutputMaxShift(-1);
        delete prob;
    }

    cout << "  Total: " << totalPass << "/" << (nTrials * 3)
         << (totalFail == 0 ? " [PASS]\n" : " [FAIL]\n");
    return totalFail == 0;
}

// ============================================================
//  runAllTests: Test 1〜4 をすべて実行
// ============================================================
bool NSGASplittingRunner::runAllTests(const string &instanceFile) {
    cout << "\n"
         << "======================================================\n"
         << " Unit Tests for RCPSP Activity Splitting (P1/P2/P3)\n"
         << " Instance: " << instanceFile << "\n"
         << "======================================================\n";

    bool r1 = test1_Proposition2(instanceFile);
    bool r2 = test2_InclusionRelation(instanceFile);
    bool r3 = test3_ResourceConstraint(instanceFile);
    bool r4 = test4_ProcessingTime(instanceFile);

    cout << "\n"
         << "======================================================\n"
         << " Summary\n"
         << "  Test1 (Proposition 2)       : " << (r1 ? "PASS" : "FAIL") << "\n"
         << "  Test2 (Inclusion Relation)  : " << (r2 ? "PASS" : "FAIL") << "\n"
         << "  Test3 (Resource Constraint) : " << (r3 ? "PASS" : "FAIL") << "\n"
         << "  Test4 (Processing Time)     : " << (r4 ? "PASS" : "FAIL") << "\n"
         << "======================================================\n\n";

    return r1 && r2 && r3 && r4;
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        const string defaultInstance = "j30.sm/j301_1.sm";
        const string instanceFile = (argc >= 2) ? string(argv[1]) : defaultInstance;

        // ---- ユニットテスト（常に実行）----
        bool testOk = NSGASplittingRunner::runAllTests(instanceFile);
        if (!testOk) {
            cerr << "[ABORT] Unit tests failed. Fix issues before running experiment.\n";
            return 1;
        }

        // ---- 実験実行 ----
        NSGASplittingRunner::Config cfg;
        cfg.instanceFile      = instanceFile;
        cfg.rr                = 0.0;   // runAll() 内で条件を変える
        cfg.rv                = false;
        cfg.populationSize    = 100;
        cfg.evalsPerStrategy  = 50000; // 必要に応じて変更
        cfg.numStrategies     = 4;

        NSGASplittingRunner runner(cfg);

        if (argc >= 2) {
            // インスタンス指定: 全条件・全モードを実行
            runner.runAll();
        } else {
            // デフォルト: j301_1.sm で全条件・全モードを実行
            runner.runAll();
        }

        cout << "[ALL DONE]\n";
        return 0;

    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
