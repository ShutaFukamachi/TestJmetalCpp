// ============================================================
//  main_split_comparison.cpp
//
//  3モード比較実験: P1 (分割なし) / P2 (非先制的分割) / P3 (自由分割)
//  + 時間依存資源制約 (Resource Range rr, Resource Vacation rv)
//  + 再開ペナルティ restartPenalty
//
//  出力: FUN_<instanceId>_P{1,2,3}_<mode>.txt  (makespan, cost)
//        results_summary.csv  (インスタンス × モード × 指標)
// ============================================================

#include <limits>
#include <queue>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <random>
#include <stdexcept>
#include <iostream>
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

using namespace std;

// ============================================================
//  ユーティリティ
// ============================================================
struct Obj2 { double f1; double f2; };

static inline bool dominatesMin2(const Obj2 &a, const Obj2 &b) {
    return (a.f1 <= b.f1 && a.f2 <= b.f2) && (a.f1 < b.f1 || a.f2 < b.f2);
}

static inline vector<Obj2> readFun2(const string &path) {
    ifstream fin(path.c_str());
    vector<Obj2> pts;
    if (!fin) return pts;
    double x, y;
    while (fin >> x >> y) pts.push_back({x, y});
    return pts;
}

// C-metric: A が B の何割を支配しているか [0,100]
static inline double coveragePercent(const vector<Obj2> &A, const vector<Obj2> &B) {
    if (B.empty()) return 0.0;
    int dominated = 0;
    for (const auto &b : B) {
        for (const auto &a : A) {
            if (dominatesMin2(a, b)) { ++dominated; break; }
        }
    }
    return 100.0 * dominated / (double)B.size();
}

static inline bool fileExists(const string &p) {
    ifstream f(p.c_str(), ios::binary); return (bool)f;
}
static inline void copyFileBinary(const string &src, const string &dst) {
    ifstream in(src.c_str(), ios::binary);
    if (!in) throw runtime_error("Cannot open src: " + src);
    ofstream out(dst.c_str(), ios::binary);
    if (!out) throw runtime_error("Cannot open dst: " + dst);
    out << in.rdbuf();
}
static inline string baseNameNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}
static inline string detectSizeTag(const string &path) {
    if (path.find("j30")  != string::npos) return "j30";
    if (path.find("j60")  != string::npos) return "j60";
    if (path.find("j90")  != string::npos) return "j90";
    if (path.find("j120") != string::npos) return "j120";
    return "unknown";
}

// ============================================================
//  実験パラメータ
// ============================================================
struct ExperimentParams {
    int    populationSize = 100;
    int    maxEvaluations = 20000;
    double crossoverProb  = 0.9;
    bool   useLocalSearch = true;

    // 時間依存資源制約パラメータ
    double rr = 0.5;   // resource range (0 = 定数, 0.25/0.5/0.75)
    bool   rv = true;  // resource vacation

    // 再開ペナルティ (P2/P3 専用)
    int restartPenalty = 2;
};

// ============================================================
//  1インスタンス × 1 SplitMode の NSGA-II 実行
// ============================================================
static SolutionSet* runNSGAII_splitMode(
    const string        &instanceFile,
    SplitMode            mode,
    const ExperimentParams &params,
    const string        &costsFile  // インスタンス専用の costs_XXX.csv
) {
    // コストファイルを costs.csv にコピーして RCPSP が読み込めるようにする
    if (!fileExists(costsFile)) {
        throw runtime_error("Missing cost file: " + costsFile);
    }
    copyFileBinary(costsFile, "costs.csv");
    RCPSP_Problem::resetGlobalCostSeries();

    // 問題インスタンス生成
    RCPSP_Problem *problem = new RCPSP_Problem(
        instanceFile,
        4,              // strategy (既存の maxShift 生成戦略)
        mode,
        params.restartPenalty
    );

    // 時間依存資源制約の設定
    // まず専用ファイルがあれば読み込む
    string capFile = "cap_" + baseNameNoExt(instanceFile) + ".csv";
    if (!problem->loadTimeVaryingCapacityFromCSV(capFile)) {
        // なければ乱数生成し、保存しておく
        problem->generateTimeVaryingCapacity(params.rr, params.rv, 42);
        problem->writeTimeVaryingCapacityToCSV(capFile);
    }

    problem->setMaxEvaluations(params.maxEvaluations);

    // NSGA-II セットアップ
    Algorithm *algorithm = new NSGAII(problem);
    algorithm->setInputParameter("populationSize", const_cast<int*>(&params.populationSize));
    algorithm->setInputParameter("maxEvaluations", const_cast<int*>(&params.maxEvaluations));
    int lsFlag = params.useLocalSearch ? 1 : 0;
    algorithm->setInputParameter("useLocalSearch", &lsFlag);

    double crossP = params.crossoverProb;
    Operator *crossover  = new PermutationCrossover(crossP);
    double mutP = 1.0 / (double)problem->getNumberOfVariables();
    Operator *mutation   = new PermutationMutation(mutP, problem);
    map<string,void*> selParams;
    Operator *selection  = new BinaryTournament2(selParams);

    algorithm->addOperator("crossover", crossover);
    algorithm->addOperator("mutation",  mutation);
    algorithm->addOperator("selection", selection);

    SolutionSet *result = algorithm->execute();

    delete algorithm;
    delete problem;

    return result;  // 呼び出し元が delete する
}

// ============================================================
//  サマリ CSV への追記
// ============================================================
static void appendSummaryCSV(
    const string &csvPath,
    const string &instanceId,
    const string &sizeTag,
    const string &splitModeStr,
    double rr, bool rv, int restartPenalty,
    double avgMakespan, double avgCost,
    double minMakespan, double minCost,
    int    paretoSize
) {
    bool exists = fileExists(csvPath);
    ofstream fout(csvPath.c_str(), ios::app);
    if (!exists) {
        fout << "instanceId,sizeTag,splitMode,rr,rv,restartPenalty,"
             << "avgMakespan,avgCost,minMakespan,minCost,paretoSize\n";
    }
    fout << instanceId << "," << sizeTag << "," << splitModeStr << ","
         << rr << "," << (rv ? 1 : 0) << "," << restartPenalty << ","
         << avgMakespan << "," << avgCost << ","
         << minMakespan << "," << minCost << ","
         << paretoSize << "\n";
}

// ============================================================
//  1インスタンスで全3モードを比較
// ============================================================
static void runThreeModesComparison(
    const string         &instanceFile,
    const ExperimentParams &params
) {
    string instanceId = baseNameNoExt(instanceFile);
    string sizeTag    = detectSizeTag(instanceFile);
    string costsFile  = "costs_" + instanceId + ".csv";

    cout << "\n========================================\n";
    cout << " Instance : " << instanceId << " (" << sizeTag << ")\n";
    cout << " RR=" << params.rr
         << " RV=" << (params.rv ? "true" : "false")
         << " restartPenalty=" << params.restartPenalty << "\n";
    cout << "========================================\n";

    // コストファイルが存在しなければ生成 (初回実行時)
    if (!fileExists(costsFile)) {
        // RCPSP_Problem を一時的に作って cost table を生成・保存
        RCPSP_Problem::resetGlobalCostSeries();
        RCPSP_Problem tmpProb(instanceFile, 4, SplitMode::NO_SPLIT, 0);
        // costs.csv が生成されるので instanceId 用にコピー保存
        if (fileExists("costs.csv")) copyFileBinary("costs.csv", costsFile);
    }

    // 各モード名
    struct ModeInfo {
        SplitMode   mode;
        string      name;
    };
    vector<ModeInfo> modes = {
        { SplitMode::NO_SPLIT,            "P1_NoSplit"           },
        { SplitMode::NONPREEMPTIVE_SPLIT, "P2_NonPreemptSplit"   },
        { SplitMode::FREE_SPLIT,          "P3_FreeSplit"         },
    };

    // FUN ファイルを全モード分収集 (C-metric 計算用)
    map<string, vector<Obj2>> allFronts;

    for (const auto &mi : modes) {
        cout << "\n[RUN] " << mi.name << "\n";

        SolutionSet *result = runNSGAII_splitMode(
            instanceFile, mi.mode, params, costsFile);

        // --- FUN ファイル出力 ---
        string funFile = "FUN_" + instanceId + "_" + mi.name + ".txt";
        ofstream fout(funFile.c_str());

        double sumMakespan = 0.0, sumCost = 0.0;
        double minMakespan = 1e18, minCost = 1e18;
        int    n = result->size();

        vector<Obj2> front;
        for (int i = 0; i < n; ++i) {
            Solution *sol = result->get(i);
            double f1 = sol->getObjective(0);
            double f2 = sol->getObjective(1);
            fout << f1 << " " << f2 << "\n";
            front.push_back({f1, f2});
            sumMakespan += f1;  sumCost += f2;
            if (f1 < minMakespan) minMakespan = f1;
            if (f2 < minCost)     minCost     = f2;
        }
        fout.close();
        allFronts[mi.name] = front;

        double avgMakespan = (n > 0) ? sumMakespan / n : 0.0;
        double avgCost     = (n > 0) ? sumCost     / n : 0.0;

        cout << "  Pareto front size = " << n << "\n";
        cout << "  Avg makespan = " << avgMakespan
             << "  Min makespan = " << minMakespan << "\n";
        cout << "  Avg cost     = " << avgCost
             << "  Min cost     = " << minCost     << "\n";

        appendSummaryCSV(
            "results_summary.csv",
            instanceId, sizeTag, mi.name,
            params.rr, params.rv, params.restartPenalty,
            avgMakespan, avgCost, minMakespan, minCost, n
        );

        delete result;
    }

    // --- C-metric 比較 ---
    // C(P2, P1): P1 の解のうち P2 に支配される割合
    // C(P3, P1): P1 の解のうち P3 に支配される割合
    // C(P3, P2): P2 の解のうち P3 に支配される割合
    struct CMetricEntry { string A; string B; };
    vector<CMetricEntry> cmetrics = {
        {"P2_NonPreemptSplit", "P1_NoSplit"},
        {"P3_FreeSplit",       "P1_NoSplit"},
        {"P3_FreeSplit",       "P2_NonPreemptSplit"},
        {"P2_NonPreemptSplit", "P3_FreeSplit"},
    };

    bool existsCmetric = fileExists("cmetric_comparison.csv");
    ofstream cmOut("cmetric_comparison.csv", ios::app);
    if (!existsCmetric) {
        cmOut << "instanceId,sizeTag,rr,rv,restartPenalty,A,B,C_A_over_B_percent\n";
    }
    for (const auto &cm : cmetrics) {
        if (allFronts.count(cm.A) && allFronts.count(cm.B)) {
            double c = coveragePercent(allFronts[cm.A], allFronts[cm.B]);
            cout << "[C-METRIC] C(" << cm.A << ", " << cm.B << ") = " << c << "%\n";
            cmOut << instanceId << "," << sizeTag << ","
                  << params.rr << "," << (params.rv ? 1 : 0) << ","
                  << params.restartPenalty << ","
                  << cm.A << "," << cm.B << "," << c << "\n";
        }
    }
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        // デフォルト実験パラメータ
        ExperimentParams params;
        params.populationSize = 100;
        params.maxEvaluations = 20000;
        params.useLocalSearch = true;
        params.rr             = 0.5;   // resource range
        params.rv             = true;  // resource vacation
        params.restartPenalty = 2;     // 再開ペナルティ (タイムステップ数)

        // コマンドライン引数でインスタンスを1つ指定した場合
        if (argc >= 2) {
            string instanceFile = argv[1];
            // 追加オプション: ./a.out instance.sm rr rv restartPenalty
            if (argc >= 3) params.rr             = atof(argv[2]);
            if (argc >= 4) params.rv             = (atoi(argv[3]) != 0);
            if (argc >= 5) params.restartPenalty = atoi(argv[4]);

            runThreeModesComparison(instanceFile, params);
            return 0;
        }

        // バッチ実行: インスタンスリストを直接記述
        vector<string> instances = {
            "j30.sm/j301_1.sm",
            "j30.sm/j302_1.sm",
            "j30.sm/j303_1.sm",
            // 必要に応じてコメントアウトを解除
            // "j60.sm/j601_1.sm",
            // "j90.sm/j901_1.sm",
        };

        // 複数の実験条件を試す
        vector<ExperimentParams> conditions;
        {
            // 条件1: RR=0.5, RV=true,  restartPenalty=2 (デフォルト)
            ExperimentParams p1 = params;
            p1.rr = 0.5; p1.rv = true;  p1.restartPenalty = 2;
            conditions.push_back(p1);

            // 条件2: RR=0.5, RV=true,  restartPenalty=0 (ペナルティなし比較用)
            ExperimentParams p2 = params;
            p2.rr = 0.5; p2.rv = true;  p2.restartPenalty = 0;
            conditions.push_back(p2);

            // 条件3: RR=0.75, RV=true, restartPenalty=2 (高 variability)
            ExperimentParams p3 = params;
            p3.rr = 0.75; p3.rv = true;  p3.restartPenalty = 2;
            conditions.push_back(p3);

            // 条件4: RR=0.0, RV=false (定数制約: P2=P1 のはず)
            ExperimentParams p4 = params;
            p4.rr = 0.0; p4.rv = false; p4.restartPenalty = 2;
            conditions.push_back(p4);
        }

        for (const auto &cond : conditions) {
            for (const auto &inst : instances) {
                runThreeModesComparison(inst, cond);
            }
        }

        cout << "\n[BATCH] All done.\n";
        return 0;

    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}













