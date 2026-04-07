#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <stdexcept>

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
//  1インスタンス × 1条件 (rr, rv) の NSGA-II 実行
//
//  出力ファイル:
//    FUN_<instanceId>_<condTag>  : パレートフロント (makespan cost)
//    VAR_<instanceId>_<condTag>  : 決定変数
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

    // --- 問題インスタンス生成 (rr, rv を渡す) ---
    Problem *problem = new RCPSP_Problem(instanceFile, /*strategy=*/4, rr, rv);
    Algorithm *algorithm = new NSGAII(problem);

    int populationSize = 100;
    int maxEvaluations = 20000;

    if (auto *rcpsp = dynamic_cast<RCPSP_Problem*>(problem)) {
        rcpsp->setMaxEvaluations(maxEvaluations);
    }

    algorithm->setInputParameter("populationSize", &populationSize);
    algorithm->setInputParameter("maxEvaluations", &maxEvaluations);

    // 局所探索: OFF
    int lsFlag = 0;
    algorithm->setInputParameter("useLocalSearch", &lsFlag);

    // 初期解の seed: 渡さない (NSGA-II がランダム生成)

    // --- 演算子設定 ---
    double crossoverProbability = 0.9;
    Operator *crossover = new PermutationCrossover(crossoverProbability);

    double mutationProbability = 1.0 / (double)problem->getNumberOfVariables();
    Operator *mutation  = new PermutationMutation(mutationProbability, problem);

    map<string, void*> selParams;
    Operator *selection = new BinaryTournament2(selParams);

    algorithm->addOperator("crossover", crossover);
    algorithm->addOperator("mutation",  mutation);
    algorithm->addOperator("selection", selection);

    // --- 実行 ---
    SolutionSet *population = algorithm->execute();

    // --- 結果出力 ---
    const string funPath = "FUN_" + tagPrefix + "_" + ctag;
    const string varPath = "VAR_" + tagPrefix + "_" + ctag;

    ofstream funFile(funPath.c_str());
    ofstream varFile(varPath.c_str());

    for (int i = 0; i < population->size(); ++i) {
        Solution *sol = population->get(i);
        funFile << sol->getObjective(0) << " " << sol->getObjective(1) << "\n";

        int nVar = problem->getNumberOfVariables();
        Variable **vars = sol->getDecisionVariables();
        for (int j = 0; j < nVar; ++j) {
            varFile << vars[j]->getValue();
            if (j + 1 < nVar) varFile << " ";
        }
        varFile << "\n";
    }

    funFile.close();
    varFile.close();

    cout << "[DONE] " << funPath << "  (Pareto size=" << population->size() << ")\n\n";

    delete population;
    delete algorithm;
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

        // ---- バッチ実行: 対象インスタンスをここに列挙 ----
        const vector<string> instances = {
            "j30.sm/j301_1.sm",
            "j30.sm/j302_1.sm",
            "j60.sm/j601_1.sm",
            // 必要に応じてコメントアウトを外す
        };

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














