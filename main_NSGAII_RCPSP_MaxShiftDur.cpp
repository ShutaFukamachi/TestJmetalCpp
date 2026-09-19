// ============================================================
//  main_NSGAII_RCPSP_MaxShiftDur.cpp  (NSGAMaxShiftDurCpp ターゲット)
//
//  MaxShiftDur エンコーディング（所要時間ベース遅延スケーリング）の
//  NSGA-II ランナー。P1/P2/P3 作業分割モードすべてに対応。
//
//  main.cpp（NSGAEncCpp）と同一の
//    - インスタンスリスト（ALL_INSTANCES）
//    - 実行条件（popSize=100, evalsPerStrategy=50000, 4 Strategy）
//    - 6 RR/RV 条件（0.00/0.25/0.50 × false/true。RR=0.75 は限界条件用に
//      RCPSP_Conditions.h の RCPSP_LIMIT_CONDITIONS() へ分離。既定の比較対象外）
//    - 出力ファイル形式（FUN_ENC_* / SCHED_ENC_*）
//  で実行することで、既存の MaxShift / SchedObj と直接比較可能。
//
//  出力ファイル:
//    FUN_ENC_<prefix>_<cond>_MaxShiftDur.txt        (P1)
//    FUN_ENC_<prefix>_<cond>_P2_MaxShiftDur.txt     (P2)
//    FUN_ENC_<prefix>_<cond>_P3_MaxShiftDur.txt     (P3)
//    SCHED_ENC_* も同様
//
//  使い方:
//    NSGAMaxShiftDurCpp                      # 全インスタンス一括
//    NSGAMaxShiftDurCpp j30.sm/j301_1.sm     # 単一インスタンス
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
#include <random>
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
#include "operators/mutation/MaxShiftDurMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "problems/RCPSP_Problem.h"
#include "problems/RCPSP_Problem_MaxShiftDur.h"
#include "problems/RCPSP_Problem_Splitting_MaxShiftDur.h"
#include "problems/RCPSP_Conditions.h"
#include "util/Ranking.h"

using namespace std;

// ============================================================
//  main.cpp と同一のインスタンスリスト
// ============================================================
static const vector<string> ALL_INSTANCES = {
    // j30 (_1 instances)
    "j30.sm/j3011_1.sm", "j30.sm/j3017_1.sm", "j30.sm/j3026_1.sm",
    "j30.sm/j3047_1.sm", "j30.sm/j309_1.sm",
    // j60 (_1 instances)
    // "j60.sm/j6016_1.sm", "j60.sm/j6018_1.sm", "j60.sm/j6023_1.sm",
    // "j60.sm/j6024_1.sm", "j60.sm/j609_1.sm",
    // // j90 (_1 instances)
    // "j90.sm/j9015_1.sm", "j90.sm/j9041_1.sm", "j90.sm/j9044_1.sm",
    // "j90.sm/j905_1.sm",  "j90.sm/j909_1.sm",
    // // j120 (_1 instances)
    // "j120.sm/j12011_1.sm", "j120.sm/j12012_1.sm", "j120.sm/j12015_1.sm",
    // "j120.sm/j12022_1.sm", "j120.sm/j12035_1.sm",
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
//  Phase2: A2（単目的makespan GA）が抽出した順列プールを読み込む
//    results/FUN_A2/<prefix>/PERM_A2_<prefix>_<ctag>.txt
//    各行: ms cost perm[0] perm[1] ... perm[n-1]
//  ms昇順で保存されているので、先頭から最大 count 件を返す。
// ============================================================
static vector<vector<int>> loadA2Perms(const string &prefix, const string &ctag, int count) {
    vector<vector<int>> perms;
    const string path = "results/FUN_A2/" + prefix + "/PERM_A2_" + prefix + "_" + ctag + ".txt";
    ifstream f(path.c_str());
    if (!f) {
        syncPrint("  [A2seed] WARNING: perm pool not found: " + path + "\n");
        return perms;
    }
    string line;
    while ((int)perms.size() < count && getline(f, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        double ms, cost;
        if (!(iss >> ms >> cost)) continue;
        vector<int> perm;
        int v;
        while (iss >> v) perm.push_back(v);
        if (!perm.empty()) perms.push_back(perm);
    }
    return perms;
}

// ============================================================
//  A2順列からmax_shift/rhoキーに多様性を持たせたSolutionSetを構築する
//  （半分は key=0=EST端、半分はUniform[0, getEffectiveHalfT()]=コスト探索側）。
//  RCPSP_Problem_MaxShiftDur* 型の変数経由で呼ぶため getEffectiveHalfT() の
//  シャドーイング問題（.miss_memory/031で発見）は起きない（正しく R が返る）。
// ============================================================
static SolutionSet* buildA2SeedSet(RCPSP_Problem_MaxShiftDur *prob,
                                    const vector<vector<int>> &perms,
                                    mt19937 &rng)
{
    if (perms.empty()) return nullptr;
    const int n = prob->getNumJobs();
    const int halfT = prob->getEffectiveHalfT();
    uniform_int_distribution<int> keyDist(0, max(0, halfT));

    SolutionSet *seedSet = new SolutionSet((int)perms.size());
    for (size_t i = 0; i < perms.size(); ++i) {
        if ((int)perms[i].size() != n) continue;  // ジョブ数不一致は破棄
        Solution *sol = new Solution(prob);
        auto &vars = sol->getVars();
        for (int j = 0; j < n; ++j) vars[j] = perms[i][j];
        for (int j = 0; j < n; ++j) {
            int idx = n + j;
            if (idx >= (int)vars.size()) break;
            vars[idx] = (i % 2 == 0) ? 0 : keyDist(rng);
        }
        if (n > 0) vars[n + 0] = 0;
        if (n > 1) vars[n + n - 1] = 0;
        seedSet->add(sol);
    }
    return seedSet;
}

// ============================================================
//  オペレータ設定
// ============================================================
static void attachMaxShiftDurOps(Algorithm *algo,
                                  RCPSP_Problem_MaxShiftDur *prob) {
    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob->getNumberOfVariables();
    algo->addOperator("crossover", new MaxShiftCrossover(crossP));
    algo->addOperator("mutation",  new MaxShiftDurMutation(mutP, prob));
    map<string, void*> sel;
    algo->addOperator("selection", new BinaryTournament2(sel));
}

// ============================================================
//  beta 値をファイル名タグ用文字列に変換（小数点を除去: 0.75 -> "0_75"）
// ============================================================
static string betaTagStr(double beta) {
    ostringstream oss;
    oss << fixed << setprecision(2) << beta;
    string s = oss.str();
    for (auto &c : s) if (c == '.') c = '_';
    return s;
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
        double beta,
        bool a1PrioritySeed = false,
        int a2SeedCount = 0,
        const string &prefix = "",
        const string &ctag = "")
{
    using StratResult = pair<SolutionSet*, RCPSP_Problem*>;
    vector<future<StratResult>> futures;
    futures.reserve(numStrategies);

    // Phase2: A2順列プールは条件ごとに共通（strategyに依存しない）ので1回だけ読む
    vector<vector<int>> a2Perms;
    if (a2SeedCount > 0) {
        a2Perms = loadA2Perms(prefix, ctag, a2SeedCount);
        syncPrint("  [A2seed] loaded " + to_string(a2Perms.size())
                  + " perms for " + prefix + " " + ctag + "\n");
    }

    for (int s = 1; s <= numStrategies; ++s) {
        futures.push_back(async(launch::async,
            [instanceFile, rr, rv, s, numStrategies,
             populationSize, evalsPerStrategy, beta, a1PrioritySeed, a2Perms]()
            -> StratResult
        {
            auto *prob = new RCPSP_Problem_MaxShiftDur(instanceFile, s, rr, rv);
            prob->setBeta(beta);
            prob->setMaxEvaluations(evalsPerStrategy);
            prob->resetEvalCounter();

            syncPrint("  [MaxShiftDur-P1 S" + to_string(s)
                      + "/" + to_string(numStrategies) + "]\n");

            Algorithm *algo = new NSGAII(prob);
            int popSz  = populationSize;
            int maxEv  = evalsPerStrategy;
            int lsFlag = 0;
            int a1Flag = a1PrioritySeed ? 1 : 0;
            algo->setInputParameter("populationSize", &popSz);
            algo->setInputParameter("maxEvaluations", &maxEv);
            algo->setInputParameter("useLocalSearch",  &lsFlag);
            algo->setInputParameter("a1PrioritySeed",  &a1Flag);

            SolutionSet *a2SeedSet = nullptr;
            if (!a2Perms.empty()) {
                mt19937 seedRng((unsigned)(std::hash<string>{}(instanceFile) + s * 7919u));
                a2SeedSet = buildA2SeedSet(prob, a2Perms, seedRng);
                // setInputParameter は void* をそのまま保持する（int系パラメータと違い
                // ポインタのアドレスではなく SolutionSet* そのものを渡す。NSGAII.cpp側で
                // static_cast<SolutionSet*>(ptr) と直接キャストされるため）。
                if (a2SeedSet) algo->setInputParameter("initialPopulation", a2SeedSet);
            }

            attachMaxShiftDurOps(algo, prob);

            SolutionSet *pop = algo->execute();
            delete a2SeedSet;

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

    syncPrint("  [MaxShiftDur-P1 DONE] PF=" + to_string(finalPareto->size()) + "\n");
    return finalPareto;
}

// ============================================================
//  P2/P3: 4 Strategy 並列実行 → Pareto フロント返却
// ============================================================
static SolutionSet* runStrategiesSplit(
        const string &instanceFile,
        ActivitySplittingMode mode,
        double rr, bool rv,
        int populationSize, int evalsPerStrategy,
        int numStrategies,
        double beta)
{
    const string modeTag = (mode == ActivitySplittingMode::P2) ? "P2" : "P3";

    using StratResult = pair<SolutionSet*, RCPSP_Problem*>;
    vector<future<StratResult>> futures;
    futures.reserve(numStrategies);

    for (int s = 1; s <= numStrategies; ++s) {
        futures.push_back(async(launch::async,
            [instanceFile, mode, rr, rv, s, numStrategies,
             populationSize, evalsPerStrategy, beta, modeTag]()
            -> StratResult
        {
            auto *prob = new RCPSP_Problem_Splitting_MaxShiftDur(
                    instanceFile, mode, s, rr, rv);
            prob->setBeta(beta);
            prob->setMaxEvaluations(evalsPerStrategy);
            prob->resetEvalCounter();

            syncPrint("  [MaxShiftDur-" + modeTag + " S" + to_string(s)
                      + "/" + to_string(numStrategies) + "]\n");

            Algorithm *algo = new NSGAII(prob);
            int popSz  = populationSize;
            int maxEv  = evalsPerStrategy;
            int lsFlag = 0;
            algo->setInputParameter("populationSize", &popSz);
            algo->setInputParameter("maxEvaluations", &maxEv);
            algo->setInputParameter("useLocalSearch",  &lsFlag);

            attachMaxShiftDurOps(algo, prob);

            SolutionSet *pop = algo->execute();

            SolutionSet *result = new SolutionSet(populationSize * 4);
            {
                Ranking ranking(pop);
                if (ranking.getNumberOfSubfronts() > 0) {
                    SolutionSet *f0 = ranking.getSubfront(0);
                    syncPrint("    [" + modeTag + " S" + to_string(s)
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

    syncPrint("  [MaxShiftDur-" + modeTag + " DONE] PF="
              + to_string(finalPareto->size()) + "\n");
    return finalPareto;
}

// ============================================================
//  1インスタンス × 8条件 × P1/P2/P3 を実行
// ============================================================
static void runInstance(const string &instanceFile,
                        int    populationSize    = 100,
                        int    evalsPerStrategy  = 50000,
                        int    numStrategies     = 4,
                        double beta              = 0.5,
                        bool   p1Only            = false,
                        bool   a1PrioritySeed    = false,
                        int    a2SeedCount       = 0,
                        const string &runTag     = "")
{
    const string prefix = toBaseNoExt(instanceFile);

    using Cond = RCPSP_Cond;
    // 比較対象は RR<=0.50（6条件）。RR=0.75 は限界条件用に RCPSP_Conditions.h で別枠管理
    // （再有効化する場合は RCPSP_ALL_CONDITIONS() に差し替える）。
    const vector<Cond> conditions = RCPSP_STANDARD_CONDITIONS();

    // P2/P3 モード一覧
    struct SplitMode { ActivitySplittingMode mode; string tag; };
    const vector<SplitMode> splitModes = {
        {ActivitySplittingMode::P2, "P2"},
        {ActivitySplittingMode::P3, "P3"},
    };

    cout << "\n============================================================\n";
    cout << " MaxShiftDur (P1/P2/P3)  instance=" << instanceFile << "\n";
    cout << " popSize=" << populationSize
         << "  evalsPerStrategy=" << evalsPerStrategy
         << "  numStrategies=" << numStrategies
         << "  beta=" << beta << "\n";
    cout << "============================================================\n";

    // サマリ表ヘッダ
    cout << "\n" << left  << setw(16) << "Condition"
                 << right << setw(8)  << "Mode"
                          << setw(14) << "min_makespan"
                          << setw(14) << "min_cost"
                          << setw(10) << "|PF|" << "\n";
    cout << string(62, '-') << "\n";

    for (const auto &c : conditions) {
        RCPSP_Problem::resetGlobalCostSeries();
        const string ctag = toCondTag(c.rr, c.rv);

        // ---- P1 ----
        {
            SolutionSet *pareto = runStrategiesP1(
                    instanceFile, c.rr, c.rv,
                    populationSize, evalsPerStrategy, numStrategies, beta,
                    a1PrioritySeed, a2SeedCount, prefix, ctag);

            RCPSP_Problem_MaxShiftDur probOut(instanceFile, 1, c.rr, c.rv);
            probOut.setBeta(beta);
            // --tag が明示されていればそれを最優先で使う（ノイズ床計測のための
            // beta=0.5 再実行など、beta値だけではファイル名が公式結果と衝突する
            // ケースを確実に回避するため）。--tag 未指定かつ beta!=0.5 のときは
            // beta値からタグを自動生成。--tag も beta=0.5 も両方無いときだけ
            // 公式ファイル名（無タグ）のまま = 公式 production 実行時のみ。
            string betaTag;
            if (!runTag.empty()) betaTag = "_" + runTag;
            else if (std::abs(beta - 0.5) >= 1e-9) betaTag = "_beta" + betaTagStr(beta);
            const string outPrefix = prefix + "_" + ctag + "_MaxShiftDur" + betaTag;
            writeResultFiles(outPrefix, pareto, &probOut);

            double minMs = 1e9, minCost = 1e9;
            for (int i = 0; i < pareto->size(); ++i) {
                minMs   = min(minMs,   pareto->get(i)->getObjective(0));
                minCost = min(minCost, pareto->get(i)->getObjective(1));
            }
            cout << left  << setw(16) << ctag
                 << right << setw(8)  << "P1"
                          << setw(14) << (minMs < 1e8 ? to_string((int)minMs) : "---")
                          << setw(14) << fixed << setprecision(1) << minCost
                          << setw(10) << pareto->size() << "\n";
            delete pareto;
        }

        // ---- P2 / P3 ----（p1Only=true のときはスキップ。MIP比較では不要）
        if (p1Only) { cout << string(62, '-') << "\n"; continue; }
        for (const auto &sm : splitModes) {
            SolutionSet *pareto = runStrategiesSplit(
                    instanceFile, sm.mode, c.rr, c.rv,
                    populationSize, evalsPerStrategy, numStrategies, beta);

            RCPSP_Problem_Splitting_MaxShiftDur probOut(
                    instanceFile, sm.mode, 1, c.rr, c.rv);
            probOut.setBeta(beta);
            // 出力ファイル名: FUN_ENC_{prefix}_{cond}_{P2|P3}_MaxShiftDur.txt
            string betaTagP23;
            if (!runTag.empty()) betaTagP23 = "_" + runTag;
            else if (std::abs(beta - 0.5) >= 1e-9) betaTagP23 = "_beta" + betaTagStr(beta);
            const string outPrefix = prefix + "_" + ctag
                                   + "_" + sm.tag + "_MaxShiftDur" + betaTagP23;
            writeResultFiles(outPrefix, pareto, &probOut);

            double minMs = 1e9, minCost = 1e9;
            for (int i = 0; i < pareto->size(); ++i) {
                minMs   = min(minMs,   pareto->get(i)->getObjective(0));
                minCost = min(minCost, pareto->get(i)->getObjective(1));
            }
            cout << left  << setw(16) << ""
                 << right << setw(8)  << sm.tag
                          << setw(14) << (minMs < 1e8 ? to_string((int)minMs) : "---")
                          << setw(14) << fixed << setprecision(1) << minCost
                          << setw(10) << pareto->size() << "\n";
            delete pareto;
        }

        cout << string(62, '-') << "\n";
    }
}

// ============================================================
//  退化チェック（最初の実行時のみ）
// ============================================================
static void verifyDegenerate(const string &instanceFile) {
    cout << "\n[Degenerate Check] alpha=0 (S1) == ESS\n";

    RCPSP_Problem probBase(instanceFile);
    RCPSP_Problem_MaxShiftDur probDur(instanceFile, 1);

    Solution *solBase = probBase.createRandomTopoSolution();
    probBase.setOutputMaxShift(0);
    probBase.evaluate(solBase);

    const int n    = probDur.getNumJobs();
    const int nVar = probDur.getNumberOfVariables();
    Solution *solDur = new Solution(&probDur);
    {
        auto &dv       = solDur->getVars();
        const auto &bv = solBase->getVars();
        for (int i = 0; i < n; ++i) dv[i] = bv[i];
        for (int j = 0; j < n && n + j < nVar; ++j) dv[n + j] = 0;
    }
    probDur.evaluate(solDur);

    bool ok = (std::abs(solDur->getObjective(0) - solBase->getObjective(0)) < 1e-6 &&
               std::abs(solDur->getObjective(1) - solBase->getObjective(1)) < 1e-4);
    cout << "  [" << (ok ? "PASS" : "FAIL") << "]"
         << "  ESS: ms=" << (int)solBase->getObjective(0)
         << " cost=" << fixed << setprecision(0) << solBase->getObjective(1)
         << "  Dur(S1,key=0): ms=" << (int)solDur->getObjective(0)
         << " cost=" << solDur->getObjective(1) << "\n";

    delete solBase;
    delete solDur;
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        // ---- 実行パラメータ（main.cpp と同一）----
        const int    populationSize   = 100;
        int          evalsPerStrategy = 50000;
        const int    numStrategies    = 4;
        // beta=0.5: win_j = round(rho * max(alpha*d_j, 0.5*T))
        // → 短活動でも T/2 のフロアを保証し、MaxShift と同等の探索幅を確保
        // これが公式 production の既定値。--beta で上書き可能（実験用、A/B検証タスク参照）。
        double beta                   = 0.5;

        // 引数走査: 最初の非オプション引数をインスタンスファイル、
        // --p1-only を見つけたら P2/P3 をスキップするフラグとして扱う（既定オフ）。
        string instanceFile;
        bool   p1Only = false;
        bool   a1     = false;
        int    a2SeedCount = 0;
        string runTag;
        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "--p1-only") {
                p1Only = true;
            } else if (a == "--a1") {
                a1 = true;
            } else if (a == "--a2seed" && i + 1 < argc) {
                a2SeedCount = stoi(argv[++i]);
            } else if (a == "--evals" && i + 1 < argc) {
                evalsPerStrategy = stoi(argv[++i]);
            } else if (a == "--beta" && i + 1 < argc) {
                beta = stod(argv[++i]);
            } else if (a == "--tag" && i + 1 < argc) {
                runTag = argv[++i];
            } else if (instanceFile.empty()) {
                instanceFile = a;
            }
        }

        if (!instanceFile.empty()) {
            // 単一インスタンス実行
            cout << "\n[SINGLE] " << instanceFile << "\n";
            verifyDegenerate(instanceFile);
            runInstance(instanceFile, populationSize, evalsPerStrategy,
                        numStrategies, beta, p1Only, a1, a2SeedCount, runTag);
        } else {
            // 全インスタンス一括実行
            const int total = static_cast<int>(ALL_INSTANCES.size());
            cout << "\n[BATCH] MaxShiftDur (P1/P2/P3)  " << total << " instances"
                 << "  beta=" << beta << "\n";

            // 最初のインスタンスで退化チェック
            if (!ALL_INSTANCES.empty())
                verifyDegenerate(ALL_INSTANCES[0]);

            auto t_all = chrono::steady_clock::now();
            for (int i = 0; i < total; ++i) {
                const string &inst = ALL_INSTANCES[i];
                cout << "\n[" << (i+1) << "/" << total << "] " << inst << "\n";
                RCPSP_Problem::resetGlobalCostSeries();

                auto t0 = chrono::steady_clock::now();
                runInstance(inst, populationSize, evalsPerStrategy,
                            numStrategies, beta);
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
