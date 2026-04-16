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

// ============================================================
//  RR/RV 条件タグ文字列
//   例: rr=0.5, rv=true  → "RR050_RV1"
//       rr=0.0, rv=false → "RR000_RV0"
// ============================================================
static string conditionTag(double rr, bool rv) {
    // rr を 3 桁整数で表す (0.25 → "025")
    int rrInt = static_cast<int>(std::round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

// ============================================================
//  出力ヘルパー: SolutionSet を FUN/VAR/SCHED ファイルに書き出す
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

    int nJobs = rcpsp ? rcpsp->getNumJobs()      : 0;
    int nRes  = rcpsp ? rcpsp->getNumResources() : 0;
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
    // min_makespan 解のインデックスを特定
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

        // min_makespan → ESS (shift=0) 再評価
        //   FUN・SCHED の両方に ESS 値を使う → 値が一致し Gantt もコンパクト
        // その他       → startTimes_ をそのまま使用（コスト最適化配置を反映）
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
//  1インスタンス × 1条件 (rr, rv) の NSGA-II 実行
//
//  フェーズ8の仕様通り：
//    max_shift 戦略 1〜4 をそれぞれ独立実行（各 EVALS_PER_STRATEGY 評価）
//    各実行の第1パレートフロントを統合し、最終的に非支配フィルタを適用
//
//  出力ファイル:
//    FUN_<instanceId>_<condTag>   : 最終パレートフロント (makespan cost)
//    VAR_<instanceId>_<condTag>   : 決定変数
//    SCHED_<instanceId>_<condTag> : ガントチャート用スケジュール情報
// ============================================================
static void runNSGA(const string &instanceFile,
                    const string &tagPrefix,
                    double rr,
                    bool   rv)
{
    const string ctag = conditionTag(rr, rv);

    cout << "============================================\n";
    cout << "[RUN] " << tagPrefix << "  " << ctag << "\n";
    cout << "  instance : " << instanceFile << "\n";
    cout << "  RR=" << rr << "  RV=" << (rv ? 1 : 0) << "\n";
    cout << "============================================\n";

    // ---- パラメータ ----
    int populationSize     = 100;
    int evalsPerStrategy   = 200000;  // 論文: 各戦略 500 万評価
    const int numStrategies = 4;       // 論文: max_shift 戦略 1〜4

    // ---- 問題インスタンス生成（容量テーブルは一度だけ作成・全戦略で共有）----
    RCPSP_Problem *problem = new RCPSP_Problem(instanceFile, /*strategy=*/1, rr, rv);
    problem->setMaxEvaluations(evalsPerStrategy);

    // ---- 戦略ごとに独立実行し、第1パレートフロントを収集 ----
    // combined: 4 戦略のパレート解をまとめる
    SolutionSet *combined = new SolutionSet(numStrategies * populationSize * 4);

    for (int strategy = 1; strategy <= numStrategies; ++strategy) {
        cout << "\n  [Strategy " << strategy << "/" << numStrategies
             << "]  maxEvals=" << evalsPerStrategy << "\n";

        // 戦略切り替え・評価カウンタリセット
        problem->setStrategy(strategy);
        problem->resetEvalCounter();
        problem->clearStartTimesCache();

        // NSGA-II インスタンス生成
        Algorithm *algorithm = new NSGAII(problem);
        algorithm->setInputParameter("populationSize", &populationSize);
        algorithm->setInputParameter("maxEvaluations", &evalsPerStrategy);

        int lsFlag = 0;
        algorithm->setInputParameter("useLocalSearch", &lsFlag);

        // 演算子
        double crossoverProbability = 0.9;
        Operator *crossover = new PermutationCrossover(crossoverProbability);

        double mutationProbability = 1.0 / (double)problem->getNumberOfVariables();
        Operator *mutation  = new PermutationMutation(mutationProbability, problem);

        map<string, void*> selParams;
        Operator *selection = new BinaryTournament2(selParams);

        algorithm->addOperator("crossover", crossover);
        algorithm->addOperator("mutation",  mutation);
        algorithm->addOperator("selection", selection);

        // 実行
        SolutionSet *population = algorithm->execute();

        // 第1パレートフロントを抽出して combined に追加
        {
            Ranking ranking(population);
            if (ranking.getNumberOfSubfronts() > 0) {
                SolutionSet *front0 = ranking.getSubfront(0);
                cout << "  [Strategy " << strategy << "] Pareto front size: "
                     << front0->size() << "\n";
                for (int i = 0; i < front0->size(); ++i) {
                    combined->add(new Solution(front0->get(i)));
                }
            }
        }  // ranking がここで破棄される

        delete population;
        delete algorithm;
        // 演算子は Algorithm が所有しないためリークするが既存コードと同様
    }

    // ---- 4 戦略の統合結果に対して最終非支配フィルタを適用（フェーズ9）----
    SolutionSet *finalPareto = new SolutionSet(combined->size());
    {
        Ranking finalRanking(combined);
        if (finalRanking.getNumberOfSubfronts() > 0) {
            SolutionSet *front0 = finalRanking.getSubfront(0);
            for (int i = 0; i < front0->size(); ++i) {
                finalPareto->add(new Solution(front0->get(i)));
            }
        }
    }  // finalRanking がここで破棄される
    delete combined;

    // ---- 結果出力 ----
    const string funPath   = "FUN_"   + tagPrefix + "_" + ctag;
    const string varPath   = "VAR_"   + tagPrefix + "_" + ctag;
    const string schedPath = "SCHED_" + tagPrefix + "_" + ctag;

    writeResults(funPath, varPath, schedPath, finalPareto, problem);

    cout << "[DONE] " << funPath
         << "  (Final Pareto size=" << finalPareto->size() << ")\n\n";

    delete finalPareto;
    delete problem;
}

// ============================================================
//  1インスタンスに対して 8 通りの RR/RV 条件を全部実行
// ============================================================
static void runAllConditions(const string &instanceFile) {
    const string prefix  = baseNameNoExt(instanceFile);
    const string sizeTag = detectSizeTag(instanceFile);

    if (sizeTag == "unknown") {
        throw runtime_error("Cannot detect sizeTag from path: " + instanceFile);
    }

    // --- コストファイルの準備 ---
    // インスタンスごとの costs_<id>.csv を costs.csv にコピーする
    // costs_<id>.csv がなければ、RCPSP_Problem が自動生成して costs.csv に保存する
    const string costsFile = "costs_" + prefix + ".csv";
    if (fileExists(costsFile)) {
        copyFileBinary(costsFile, "costs.csv");
        cout << "[COST] " << costsFile << " -> costs.csv\n";
    } else {
        cout << "[COST] " << costsFile << " not found. "
             << "Will be auto-generated on first run.\n";
    }
    RCPSP_Problem::resetGlobalCostSeries();

    cout << "\n========================================\n";
    cout << " Instance: " << prefix << " (" << sizeTag << ")\n";
    cout << "========================================\n";

    // ============================================================
    //  論文 Table 5 の RR/RV 条件 8 通り
    //    RR = 0.0, 0.25, 0.5, 0.75
    //    RV = false (0), true (1)
    // ============================================================
    struct Cond { double rr; bool rv; };
    const vector<Cond> conditions = {
        {0.00, false},  // RR=0,    RV=0
        {0.00, true },  // RR=0,    RV=1
        {0.25, false},  // RR=0.25, RV=0
        {0.25, true },  // RR=0.25, RV=1
        {0.50, false},  // RR=0.5,  RV=0
        {0.50, true },  // RR=0.5,  RV=1
        {0.75, false},  // RR=0.75, RV=0
        {0.75, true },  // RR=0.75, RV=1
    };

    for (const auto &c : conditions) {
        // コストテーブルは同一インスタンス内で再利用する
        // (RCPSP_Problem のコンストラクタが capacity_t だけ新たに作る)
        runNSGA(instanceFile, prefix, c.rr, c.rv);
    }

    cout << "[BATCH] Instance " << prefix << " all conditions done.\n\n";
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        // コマンドライン引数でインスタンスを1つ指定した場合
        if (argc >= 2) {
            runAllConditions(argv[1]);
            return 0;
        }

        // ---- バッチ実行 ----
        vector<string> instances;
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

        for (const auto &inst : instances) {
            runAllConditions(inst);
        }

        cout << "[BATCH] All done.\n";
        return 0;

    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}


