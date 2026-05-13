#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <stdexcept>
#include <cmath>

#include "problems/RCPSP_Problem.h"
#include "metaheuristics/branchAndBound/BranchAndBound.h"

using namespace std;

// ============================================================
//  ユーティリティ (main.cpp と同じ)
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

// ============================================================
//  1インスタンス × 1条件 (rr, rv) の BnB 実行
//
//  出力ファイル:
//    FUN_tree_<instanceId>_<condTag>   : パレートフロント (makespan cost)
//    VAR_tree_<instanceId>_<condTag>   : 開始時刻列
//    SCHED_tree_<instanceId>_<condTag> : スケジュール詳細
// ============================================================
static void runBnB(const string &instanceFile,
                   const string &tagPrefix,
                   double rr,
                   bool   rv)
{
    const string ctag = conditionTag(rr, rv);

    cout << "============================================\n";
    cout << "[BnB] " << tagPrefix << "  " << ctag << "\n";
    cout << "  instance : " << instanceFile << "\n";
    cout << "  RR=" << rr << "  RV=" << (rv ? 1 : 0) << "\n";
    cout << "============================================\n";

    RCPSP_Problem *problem = new RCPSP_Problem(instanceFile, /*strategy=*/4, rr, rv);

    BranchAndBound bnb(problem);
    vector<BnBSolution> front = bnb.solveEpsilonConstraint(12);

    // ── 出力ファイル ──────────────────────────────────────
    const string funPath   = "FUN_tree_"   + tagPrefix + "_" + ctag;
    const string varPath   = "VAR_tree_"   + tagPrefix + "_" + ctag;
    const string schedPath = "SCHED_tree_" + tagPrefix + "_" + ctag;

    ofstream funFile  (funPath.c_str());
    ofstream varFile  (varPath.c_str());
    ofstream schedFile(schedPath.c_str());

    int nJobs = problem->getNumJobs();
    int nRes  = problem->getNumResources();

    // SCHED ヘッダー (NSGA-II と同形式)
    schedFile << nJobs << " " << nRes << "\n";
    const auto &dur    = problem->getDurations();
    const auto &demand = problem->getDemand();
    const auto &cap    = problem->getCapacity();
    const auto &cap_t  = problem->getCapacityT();

    for (int j = 0; j < nJobs; ++j) {
        schedFile << dur[j];
        if (j + 1 < nJobs) schedFile << " ";
    }
    schedFile << "\n";
    for (int j = 0; j < nJobs; ++j) {
        for (int k = 0; k < nRes; ++k) {
            schedFile << demand[j][k];
            if (k + 1 < nRes) schedFile << " ";
        }
        schedFile << "\n";
    }
    for (int k = 0; k < nRes; ++k) {
        schedFile << cap[k];
        if (k + 1 < nRes) schedFile << " ";
    }
    schedFile << "\n";
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
    schedFile << front.size() << "\n";

    // 各解を出力
    for (const BnBSolution &sol : front) {
        // FUN: makespan cost
        funFile << sol.makespan << " " << sol.cost << "\n";

        // VAR: 開始時刻列 (start[0] start[1] ... start[nJobs-1])
        for (int j = 0; j < nJobs; ++j) {
            varFile << sol.startTime[j];
            if (j + 1 < nJobs) varFile << " ";
        }
        varFile << "\n";

        // SCHED: makespan cost start[0] ... start[nJobs-1]
        schedFile << sol.makespan << " " << sol.cost;
        for (int j = 0; j < nJobs; ++j) schedFile << " " << sol.startTime[j];
        schedFile << "\n";
    }

    funFile.close();
    varFile.close();
    schedFile.close();

    cout << "[DONE] " << funPath
         << "  (Pareto size=" << front.size() << ")\n\n";

    delete problem;
}

// ============================================================
//  1インスタンスに対して 8通りの RR/RV 条件を全部実行
// ============================================================
static void runAllConditions(const string &instanceFile) {
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
        cout << "[COST] " << costsFile << " not found. "
             << "Will be auto-generated on first run.\n";
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

    for (const auto &c : conditions) {
        runBnB(instanceFile, prefix, c.rr, c.rv);
    }

    cout << "[BATCH] Instance " << prefix << " all conditions done.\n\n";
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        auto t0 = std::chrono::steady_clock::now();

        if (argc >= 2) {
            runAllConditions(argv[1]);
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            cout << "[TIME] " << elapsed << " s\n";
            return 0;
        }

        // ---- バッチ実行: j30 インスタンスのみ ----
        const vector<string> instances = {
            "j30.sm/j301_1.sm",
            "j30.sm/j302_1.sm",
        };

        for (const auto &inst : instances) {
            auto t_inst = std::chrono::steady_clock::now();
            runAllConditions(inst);
            double inst_elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_inst).count();
            cout << "[TIME] " << inst << " : " << inst_elapsed << " s\n";
        }

        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        cout << "[BATCH] All done.\n";
        cout << "[TIME] total: " << elapsed << " s\n";
        return 0;

    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}