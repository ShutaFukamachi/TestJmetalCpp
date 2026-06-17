// ============================================================
//  main_NSGAII_RCPSP_Comparison.cpp
//
//  NSGAComparisonRunner クラス
//  ─ P1 をベースラインとして P2/P3 × セットアップモデル（TW/WD/WR）
//    × RR/RV 条件を横断比較し、以下を出力する。
//
//  【出力ファイル一覧】
//   CMP_<prefix>_alphaS<N>.csv
//     ─ 統計サマリー（条件×モード×Setup ごとの msMin/costMin/delta など）
//
//   PF_<prefix>_alphaS<N>.csv
//     ─ 全パレートフロント点の生データ
//       (condition, mode, setup, makespan, cost) の全行
//       → Excel / Python / R でそのまま散布図を描ける
//
//   FUN_CMP_<prefix>_<cond>_<mode>_<setup>.txt
//     ─ 条件×モード×Setup ごとの FUN ファイル（makespan cost の 2 列）
//
//   PFPLOT_CMP_<prefix>_<cond>.txt
//     ─ 1 条件に対して全モードを重ねた ASCII パレートフロント図
//       X 軸: makespan（左=小）  Y 軸: cost（上=大）
//       凡例: 1=P1, A=P2+TW, B=P2+WD, C=P2+WR, D=P3+TW, E=P3+WD, F=P3+WR
//             +=複数モードが同セル
//
//  【コマンドライン引数】
//   argv[1] : インスタンスファイル（省略時 j30.sm/j301_1.sm）
//   argv[2] : alphaS（省略時 0.5）
// ============================================================

#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <limits>

#include "core/Problem.h"
#include "core/Algorithm.h"
#include "core/SolutionSet.h"
#include "Solution.h"
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
//  NSGAComparisonRunner
// ============================================================
class NSGAComparisonRunner {
public:
    // ---- 実行設定 -----------------------------------------------
    struct Config {
        string instanceFile;
        double alphaS           = 0.5;
        int    populationSize   = 100;
        int    evalsPerStrategy = 50000;
        int    numStrategies    = 4;
    };

    explicit NSGAComparisonRunner(Config cfg)
        : cfg_(std::move(cfg))
        , prefix_(toBaseNoExt(cfg_.instanceFile))
    {}

    // 全条件 × 全モード × 全 SetupModel を実行し全出力ファイルを書く
    void runComparison() const;

private:
    Config cfg_;
    string prefix_;

    // ---- パレートフロント 1 点 ------------------------------------
    struct ParetoPoint {
        double makespan;
        double cost;
    };

    // ---- 1 実験ラン分のデータ -------------------------------------
    struct RunData {
        // 識別子
        string condTag;    // RR000_RV0 など
        string modeTag;    // P1 / P2 / P3
        string setupTag;   // - / TW / WD / WR
        char   symbol;     // ASCII プロット用: 1 A B C D E F
        double alphaS = 0.0;
        // 統計
        int    paretoSize = 0;
        double msMin   = 0.0;
        double msMax   = 0.0;
        double costMin = 0.0;
        double costMax = 0.0;
        // P1 との差・比率（同条件で後から設定）
        double deltaMsMin   = 0.0;
        double deltaCostMin = 0.0;
        double ratioMsMin   = 1.0;
        double ratioCostMin = 1.0;
        // パレートフロントの全点
        vector<ParetoPoint> points;
    };

    // ---- NSGA-II 共通実行 ----------------------------------------
    // prob を受け取って numStrategies 回走らせ最終パレートを返す
    // 呼び出し元が delete する
    SolutionSet* runNSGAII(RCPSP_Problem *prob) const;
    static void  attachOperators(Algorithm *algo, RCPSP_Problem *prob);

    // SolutionSet → RunData（点群 + 統計を同時に生成）
    static RunData buildRunData(const string &condTag,
                                const string &modeTag,
                                const string &setupTag,
                                char          symbol,
                                double        alphaS,
                                SolutionSet  *pareto);

    // ---- 各モードの実行 ------------------------------------------
    RunData runP1(double rr, bool rv) const;
    RunData runSetup(ActivitySplittingMode mode,
                     SetupModel            sm,
                     double                rr,
                     bool                  rv) const;

    // ---- 出力関数群 ----------------------------------------------

    // CMP_*.csv  統計サマリー
    static void writeSummaryCSV(const string          &path,
                                const vector<RunData> &runs,
                                const string          &instanceFile,
                                double                 alphaS);

    // PF_*.csv   全パレートフロント点の生データ（散布図用）
    static void writeParetoCSV(const string          &path,
                               const vector<RunData> &runs,
                               const string          &instanceFile,
                               double                 alphaS);

    // FUN_CMP_*.txt  条件×モード×Setup ごとの FUN ファイル
    static void writeFUNFiles(const vector<RunData> &runs,
                              const string          &prefix,
                              double                 alphaS);

    // PFPLOT_CMP_*.txt  条件ごとの ASCII パレートフロント図
    static void writeAsciiParetoPlots(const vector<RunData> &runs,
                                      const string          &prefix,
                                      double                 alphaS);

    // 1 条件分の ASCII プロットを 1 ファイルに描く
    static void writeOnePlot(const string          &path,
                             const string          &condTag,
                             const string          &instanceFile,
                             double                 alphaS,
                             const vector<RunData*> &condRuns);

    // コンソール進捗表示
    static void printProgress(const RunData &r);

    // ---- 文字列ユーティリティ ------------------------------------
    static string toModeTag(ActivitySplittingMode m);
    static string toSetupTag(SetupModel s);
    static string toCondTag(double rr, bool rv);
    static string toBaseNoExt(const string &path);
};

// ============================================================
//  static ユーティリティ
// ============================================================
string NSGAComparisonRunner::toModeTag(ActivitySplittingMode m) {
    switch (m) {
        case ActivitySplittingMode::P1: return "P1";
        case ActivitySplittingMode::P2: return "P2";
        case ActivitySplittingMode::P3: return "P3";
    }
    return "P1";
}
string NSGAComparisonRunner::toSetupTag(SetupModel s) {
    switch (s) {
        case SetupModel::TW: return "TW";
        case SetupModel::WD: return "WD";
        case SetupModel::WR: return "WR";
    }
    return "WD";
}
string NSGAComparisonRunner::toCondTag(double rr, bool rv) {
    int rrInt = static_cast<int>(std::round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}
string NSGAComparisonRunner::toBaseNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

// ============================================================
//  attachOperators
// ============================================================
void NSGAComparisonRunner::attachOperators(Algorithm *algo, RCPSP_Problem *prob) {
    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob->getNumberOfVariables();
    Operator *xover = new PermutationCrossover(crossP);
    Operator *mut   = new PermutationMutation(mutP, prob);
    map<string, void*> selP;
    Operator *sel = new BinaryTournament2(selP);
    algo->addOperator("crossover", xover);
    algo->addOperator("mutation",  mut);
    algo->addOperator("selection", sel);
}

// ============================================================
//  runNSGAII
// ============================================================
SolutionSet* NSGAComparisonRunner::runNSGAII(RCPSP_Problem *prob) const {
    prob->setMaxEvaluations(cfg_.evalsPerStrategy);
    SolutionSet *combined = new SolutionSet(
        cfg_.numStrategies * cfg_.populationSize * 4);

    for (int s = 1; s <= cfg_.numStrategies; ++s) {
        prob->setStrategy(s);
        prob->resetEvalCounter();
        prob->clearStartTimesCache();

        Algorithm *algo = new NSGAII(prob);
        int popSz = cfg_.populationSize, maxEv = cfg_.evalsPerStrategy, ls = 0;
        algo->setInputParameter("populationSize", &popSz);
        algo->setInputParameter("maxEvaluations", &maxEv);
        algo->setInputParameter("useLocalSearch", &ls);
        attachOperators(algo, prob);

        SolutionSet *pop = algo->execute();
        {
            Ranking rk(pop);
            if (rk.getNumberOfSubfronts() > 0) {
                SolutionSet *f0 = rk.getSubfront(0);
                for (int i = 0; i < f0->size(); ++i)
                    combined->add(new Solution(f0->get(i)));
            }
        }
        delete pop;
        delete algo;
    }

    SolutionSet *finalP = new SolutionSet(combined->size());
    {
        Ranking rk(combined);
        if (rk.getNumberOfSubfronts() > 0) {
            SolutionSet *f0 = rk.getSubfront(0);
            for (int i = 0; i < f0->size(); ++i)
                finalP->add(new Solution(f0->get(i)));
        }
    }
    delete combined;
    return finalP;
}

// ============================================================
//  buildRunData: SolutionSet → RunData（点群 + 統計）
// ============================================================
NSGAComparisonRunner::RunData
NSGAComparisonRunner::buildRunData(const string &condTag,
                                    const string &modeTag,
                                    const string &setupTag,
                                    char          symbol,
                                    double        alphaS,
                                    SolutionSet  *pareto)
{
    RunData rd;
    rd.condTag  = condTag;
    rd.modeTag  = modeTag;
    rd.setupTag = setupTag;
    rd.symbol   = symbol;
    rd.alphaS   = alphaS;
    rd.paretoSize = pareto->size();

    if (pareto->size() == 0) {
        rd.msMin = rd.msMax = rd.costMin = rd.costMax = -1.0;
        return rd;
    }

    double msMin   = 1e18, msMax   = -1e18;
    double costMin = 1e18, costMax = -1e18;

    rd.points.reserve(pareto->size());
    for (int i = 0; i < pareto->size(); ++i) {
        double ms   = pareto->get(i)->getObjective(0);
        double cost = pareto->get(i)->getObjective(1);
        rd.points.push_back({ms, cost});
        msMin   = min(msMin,   ms);
        msMax   = max(msMax,   ms);
        costMin = min(costMin, cost);
        costMax = max(costMax, cost);
    }
    // パレートフロントを makespan 昇順に並べておく（プロット・ファイル用）
    sort(rd.points.begin(), rd.points.end(),
         [](const ParetoPoint &a, const ParetoPoint &b){
             return a.makespan < b.makespan;
         });

    rd.msMin   = msMin;
    rd.msMax   = msMax;
    rd.costMin = costMin;
    rd.costMax = costMax;
    return rd;
}

// ============================================================
//  printProgress
// ============================================================
void NSGAComparisonRunner::printProgress(const RunData &r) {
    string label = r.modeTag + (r.setupTag != "-" ? "+" + r.setupTag : "");
    cout << "  [" << r.condTag << "] " << setw(8) << left << label << right
         << " size=" << setw(3) << r.paretoSize
         << "  msMin=" << setw(6) << r.msMin
         << "  costMin=" << fixed << setprecision(1) << setw(8) << r.costMin;
    if (r.setupTag != "-") {
        cout << "  (Δms="
             << (r.deltaMsMin >= 0 ? "+" : "") << fixed << setprecision(0) << r.deltaMsMin
             << " Δcost="
             << (r.deltaCostMin >= 0 ? "+" : "") << fixed << setprecision(1) << r.deltaCostMin
             << ")";
    }
    cout << "\n";
}

// ============================================================
//  runP1 / runSetup
// ============================================================
NSGAComparisonRunner::RunData
NSGAComparisonRunner::runP1(double rr, bool rv) const {
    RCPSP_Problem *prob = new RCPSP_Problem(cfg_.instanceFile, 1, rr, rv);
    SolutionSet *pareto = runNSGAII(prob);
    delete prob;
    RunData rd = buildRunData(toCondTag(rr, rv), "P1", "-", '1', 0.0, pareto);
    delete pareto;
    return rd;
}

NSGAComparisonRunner::RunData
NSGAComparisonRunner::runSetup(ActivitySplittingMode mode,
                                SetupModel            sm,
                                double                rr,
                                bool                  rv) const
{
    // モード×Setup の組み合わせに ASCII 文字を割り当てる
    // P2: TW=A WD=B WR=C   P3: TW=D WD=E WR=F
    char sym = '?';
    if (mode == ActivitySplittingMode::P2) {
        sym = (sm == SetupModel::TW) ? 'A'
            : (sm == SetupModel::WD) ? 'B' : 'C';
    } else {
        sym = (sm == SetupModel::TW) ? 'D'
            : (sm == SetupModel::WD) ? 'E' : 'F';
    }

    RCPSP_Problem_Setup *prob = new RCPSP_Problem_Setup(
        cfg_.instanceFile, mode, sm, cfg_.alphaS, 1, rr, rv);
    SolutionSet *pareto = runNSGAII(prob);
    delete prob;

    RunData rd = buildRunData(toCondTag(rr, rv),
                              toModeTag(mode), toSetupTag(sm),
                              sym, cfg_.alphaS, pareto);
    delete pareto;
    return rd;
}

// ============================================================
//  writeSummaryCSV  ─ 統計サマリー
// ============================================================
void NSGAComparisonRunner::writeSummaryCSV(const string          &path,
                                            const vector<RunData> &runs,
                                            const string          &instanceFile,
                                            double                 alphaS)
{
    ofstream os(path.c_str());
    if (!os) throw runtime_error("Cannot open: " + path);

    string instBase = instanceFile;
    {
        size_t p = instBase.find_last_of("/\\");
        if (p != string::npos) instBase = instBase.substr(p + 1);
    }

    os << "# RCPSP Comparison Summary\n"
       << "# instance=" << instanceFile << "  alphaS=" << alphaS << "\n#\n";

    // ---- [1] 全結果テーブル ----
    os << "[RESULTS]\n"
       << "instance,condition,mode,setup,alphaS,pareto_size,"
       << "makespan_min,makespan_max,cost_min,cost_max,"
       << "delta_ms_min,delta_cost_min,ratio_ms_min,ratio_cost_min\n";

    for (const auto &r : runs) {
        os << instBase << ","
           << r.condTag << "," << r.modeTag << "," << r.setupTag << ","
           << r.alphaS << "," << r.paretoSize << ","
           << r.msMin << "," << r.msMax << ","
           << fixed << setprecision(2) << r.costMin << "," << r.costMax << ","
           << (r.deltaMsMin   >= 0 ? "+" : "") << r.deltaMsMin   << ","
           << (r.deltaCostMin >= 0 ? "+" : "")
           << fixed << setprecision(2) << r.deltaCostMin << ","
           << fixed << setprecision(4) << r.ratioMsMin << "," << r.ratioCostMin
           << "\n";
    }

    // ---- [2] SetupModel 別サマリー（全条件平均） ----
    os << "\n[SUMMARY_BY_SETUP_MODEL]\n"
       << "mode,setup,n_conds,"
       << "avg_delta_ms_min,avg_delta_cost_min,"
       << "avg_ratio_ms_min,avg_ratio_cost_min\n";

    map<pair<string,string>, vector<const RunData*>> byMS;
    for (const auto &r : runs)
        if (r.setupTag != "-")
            byMS[{r.modeTag, r.setupTag}].push_back(&r);

    // 出現順を保持するためキー列を別途保持
    vector<pair<string,string>> msKeys;
    for (const auto &r : runs) {
        auto k = make_pair(r.modeTag, r.setupTag);
        if (r.setupTag != "-" &&
            find(msKeys.begin(), msKeys.end(), k) == msKeys.end())
            msKeys.push_back(k);
    }
    for (const auto &k : msKeys) {
        const auto &rows = byMS[k];
        double sD = 0, sC = 0, sR = 0, sRC = 0;
        for (auto *r : rows) { sD += r->deltaMsMin; sC += r->deltaCostMin;
                               sR += r->ratioMsMin; sRC += r->ratioCostMin; }
        int n = (int)rows.size();
        os << k.first << "," << k.second << "," << n << ","
           << fixed << setprecision(2) << sD/n << "," << sC/n << ","
           << fixed << setprecision(4) << sR/n << "," << sRC/n << "\n";
    }

    // ---- [3] RR/RV 条件別サマリー（全モード平均） ----
    os << "\n[SUMMARY_BY_CONDITION]\n"
       << "condition,n_modes,"
       << "avg_delta_ms_min,avg_delta_cost_min,"
       << "avg_ratio_ms_min,avg_ratio_cost_min\n";

    map<string, vector<const RunData*>> byCond;
    vector<string> condKeys;
    for (const auto &r : runs) {
        if (r.setupTag == "-") continue;
        if (find(condKeys.begin(), condKeys.end(), r.condTag) == condKeys.end())
            condKeys.push_back(r.condTag);
        byCond[r.condTag].push_back(&r);
    }
    for (const auto &ck : condKeys) {
        const auto &rows = byCond[ck];
        double sD = 0, sC = 0, sR = 0, sRC = 0;
        for (auto *r : rows) { sD += r->deltaMsMin; sC += r->deltaCostMin;
                               sR += r->ratioMsMin; sRC += r->ratioCostMin; }
        int n = (int)rows.size();
        os << ck << "," << n << ","
           << fixed << setprecision(2) << sD/n << "," << sC/n << ","
           << fixed << setprecision(4) << sR/n << "," << sRC/n << "\n";
    }

    cout << "[CSV-Summary] " << path << "\n";
}

// ============================================================
//  writeParetoCSV  ─ 全パレートフロント点の生データ（散布図用）
//
//  Excel / Python / R でそのまま読み込んで散布図を描ける形式。
//
//  列: instance, condition, mode, setup, alphaS, makespan, cost
// ============================================================
void NSGAComparisonRunner::writeParetoCSV(const string          &path,
                                           const vector<RunData> &runs,
                                           const string          &instanceFile,
                                           double                 alphaS)
{
    ofstream os(path.c_str());
    if (!os) throw runtime_error("Cannot open: " + path);

    string instBase = instanceFile;
    {
        size_t p = instBase.find_last_of("/\\");
        if (p != string::npos) instBase = instBase.substr(p + 1);
    }

    os << "# Pareto Front Points (all runs)\n"
       << "# instance=" << instanceFile << "  alphaS=" << alphaS << "\n"
       << "# Use this file to draw scatter plots of Pareto fronts.\n#\n";
    os << "instance,condition,mode,setup,alphaS,makespan,cost\n";

    for (const auto &r : runs) {
        for (const auto &pt : r.points) {
            os << instBase << ","
               << r.condTag << "," << r.modeTag << "," << r.setupTag << ","
               << r.alphaS << ","
               << pt.makespan << ","
               << fixed << setprecision(4) << pt.cost << "\n";
        }
    }

    cout << "[CSV-Pareto]  " << path << "\n";
}

// ============================================================
//  writeFUNFiles  ─ 条件×モード×Setup ごとの FUN ファイル
//
//  ファイル名: FUN_CMP_<prefix>_<cond>_<mode>_<setup>.txt
//  内容 (makespan 昇順):
//    # header
//    <makespan> <cost>
//    <makespan> <cost>
//    ...
// ============================================================
void NSGAComparisonRunner::writeFUNFiles(const vector<RunData> &runs,
                                          const string          &prefix,
                                          double                 alphaS)
{
    int alphaInt = static_cast<int>(alphaS * 100);
    for (const auto &r : runs) {
        string fname = "FUN_CMP_" + prefix
                     + "_" + r.condTag
                     + "_" + r.modeTag
                     + "_" + r.setupTag
                     + "_a" + to_string(alphaInt)
                     + ".txt";

        ofstream os(fname.c_str());
        if (!os) { cerr << "[WARN] Cannot open FUN: " << fname << "\n"; continue; }

        os << "# FUN: " << r.condTag
           << " | " << r.modeTag
           << "+" << r.setupTag
           << " | alphaS=" << r.alphaS
           << " | size=" << r.paretoSize << "\n"
           << "# makespan cost\n";

        for (const auto &pt : r.points) {
            os << pt.makespan << " "
               << fixed << setprecision(4) << pt.cost << "\n";
        }
        cout << "[FUN] " << fname << "\n";
    }
}

// ============================================================
//  writeOnePlot  ─ 1 条件分の ASCII パレートフロント図
//
//  描画方針:
//   ・X 軸: makespan（左=小）  Y 軸: cost（上=大）
//   ・全モードの点を同一グリッドに重ねて描画
//   ・同一セルに複数モードが入る場合は '+' で表示
//   ・右端に凡例
//
//  凡例文字:
//   '1'=P1  'A'=P2+TW  'B'=P2+WD  'C'=P2+WR
//   'D'=P3+TW  'E'=P3+WD  'F'=P3+WR  '+'=重複
// ============================================================
void NSGAComparisonRunner::writeOnePlot(const string           &path,
                                         const string           &condTag,
                                         const string           &instanceFile,
                                         double                  alphaS,
                                         const vector<RunData*> &condRuns)
{
    ofstream os(path.c_str());
    if (!os) { cerr << "[WARN] Cannot open plot: " << path << "\n"; return; }

    const int W = 60;   // グリッド幅  (makespan 軸)
    const int H = 20;   // グリッド高さ (cost 軸)

    // ---- 軸範囲を全モード横断で決定 ----
    double msMin   = 1e18, msMax   = -1e18;
    double costMin = 1e18, costMax = -1e18;
    for (auto *r : condRuns) {
        for (const auto &pt : r->points) {
            msMin   = min(msMin,   pt.makespan);
            msMax   = max(msMax,   pt.makespan);
            costMin = min(costMin, pt.cost);
            costMax = max(costMax, pt.cost);
        }
    }
    // 点が 1 つしかない場合に軸が潰れないようマージンを付加
    if (msMax   <= msMin)   { msMin   -= 1; msMax   += 1; }
    if (costMax <= costMin) { costMin -= 1; costMax += 1; }

    // ---- グリッドに文字を配置 ----
    // grid[row][col] = '\0'（空）または文字
    vector<vector<char>> grid(H, vector<char>(W, '\0'));

    auto plotPoint = [&](double ms, double cost, char sym) {
        int col = static_cast<int>((ms   - msMin)   / (msMax   - msMin) * (W - 1));
        int row = static_cast<int>((costMax - cost)  / (costMax - costMin) * (H - 1));
        col = max(0, min(W - 1, col));
        row = max(0, min(H - 1, row));
        char &cell = grid[row][col];
        if      (cell == '\0') cell = sym;
        else if (cell != sym)  cell = '+';   // 複数モードが重なった
    };

    for (auto *r : condRuns)
        for (const auto &pt : r->points)
            plotPoint(pt.makespan, pt.cost, r->symbol);

    // ---- ファイル書き出し ----
    os << "======================================================================\n"
       << " ASCII Pareto Front: " << condTag
       << "  alphaS=" << alphaS << "\n"
       << " instance: " << instanceFile << "\n"
       << " X-axis: Makespan (left=small)  "
       << " Y-axis: Cost (top=large)\n"
       << "----------------------------------------------------------------------\n"
       << " Symbols: 1=P1  A=P2+TW  B=P2+WD  C=P2+WR"
       << "  D=P3+TW  E=P3+WD  F=P3+WR  +=overlap\n"
       << "======================================================================\n\n";

    // Y 軸ラベル幅
    const int YLABEL_W = 9;

    // コスト軸の目盛りを計算（H 行に対して 5 目盛り）
    auto costAtRow = [&](int row) {
        return costMax - (double)row / (H - 1) * (costMax - costMin);
    };

    // 上辺
    os << string(YLABEL_W, ' ') << "+" << string(W, '-') << "+\n";

    for (int row = 0; row < H; ++row) {
        // Y 軸ラベル（5 行おきに表示）
        if (row % 5 == 0) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%7.1f", costAtRow(row));
            os << lbl << " |";
        } else {
            os << string(YLABEL_W, ' ') << "|";
        }

        for (int col = 0; col < W; ++col) {
            char c = grid[row][col];
            os << (c == '\0' ? ' ' : c);
        }
        os << "|\n";
    }

    // 下辺
    os << string(YLABEL_W, ' ') << "+" << string(W, '-') << "+\n";

    // X 軸目盛り
    // 目盛り位置: 0, W/4, W/2, 3W/4, W-1
    auto msAtCol = [&](int col) {
        return msMin + (double)col / (W - 1) * (msMax - msMin);
    };
    os << string(YLABEL_W, ' ') << " ";
    {
        vector<int> ticks = {0, W/4, W/2, 3*W/4, W-1};
        int prevEnd = 0;
        for (int tick : ticks) {
            char lbl[12];
            snprintf(lbl, sizeof(lbl), "%.0f", msAtCol(tick));
            // tick 位置に合わせてスペースを埋める
            int spaces = tick - prevEnd;
            if (spaces > 0) os << string(spaces, ' ');
            os << lbl;
            prevEnd = tick + (int)strlen(lbl);
        }
    }
    os << "\n";
    os << string(YLABEL_W + 2, ' ') << "Makespan -->\n\n";

    // ---- 凡例と統計 ----
    os << "--- Legend & Statistics ---\n";
    os << setw(3) << "Sym"
       << setw(10) << "Mode"
       << setw(6)  << "Setup"
       << setw(7)  << "Size"
       << setw(9)  << "msMin"
       << setw(9)  << "msMax"
       << setw(10) << "costMin"
       << setw(10) << "costMax"
       << setw(9)  << "Δms"
       << setw(10) << "Δcost"
       << "\n";
    os << string(88, '-') << "\n";

    for (auto *r : condRuns) {
        os << setw(3)  << r->symbol
           << setw(10) << r->modeTag
           << setw(6)  << r->setupTag
           << setw(7)  << r->paretoSize
           << setw(9)  << fixed << setprecision(0) << r->msMin
           << setw(9)  << fixed << setprecision(0) << r->msMax
           << setw(10) << fixed << setprecision(1) << r->costMin
           << setw(10) << fixed << setprecision(1) << r->costMax;
        if (r->setupTag != "-") {
            os << setw(9)  << (r->deltaMsMin >= 0 ? "+" : "")
                           << fixed << setprecision(0) << r->deltaMsMin
               << setw(10) << (r->deltaCostMin >= 0 ? "+" : "")
                           << fixed << setprecision(1) << r->deltaCostMin;
        } else {
            os << setw(9) << "(P1 base)" << setw(10) << "";
        }
        os << "\n";
    }
    os << "======================================================================\n";

    cout << "[PLOT] " << path << "\n";
}

// ============================================================
//  writeAsciiParetoPlots  ─ 条件ごとに ASCII プロットを出力
// ============================================================
void NSGAComparisonRunner::writeAsciiParetoPlots(const vector<RunData> &runs,
                                                  const string          &prefix,
                                                  double                 alphaS)
{
    int alphaInt = static_cast<int>(alphaS * 100);

    // condTag → そのランへのポインタ列
    map<string, vector<RunData*>> byCondMap;
    vector<string> condOrder;
    for (const auto &r : runs) {
        if (byCondMap.find(r.condTag) == byCondMap.end())
            condOrder.push_back(r.condTag);
        // 非 const vector に非 const ポインタを入れるため const_cast を使用
        byCondMap[r.condTag].push_back(const_cast<RunData*>(&r));
    }

    for (const auto &cond : condOrder) {
        string fname = "PFPLOT_CMP_" + prefix
                     + "_" + cond
                     + "_a" + to_string(alphaInt)
                     + ".txt";
        writeOnePlot(fname, cond, prefix, alphaS, byCondMap[cond]);
    }
}

// ============================================================
//  runComparison  ─ メイン実験ループ
// ============================================================
void NSGAComparisonRunner::runComparison() const {
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

    cout << "============================================================\n"
         << "[NSGAComparisonRunner] instance=" << cfg_.instanceFile << "\n"
         << "  alphaS=" << cfg_.alphaS
         << "  pop=" << cfg_.populationSize
         << "  evalsPerStrategy=" << cfg_.evalsPerStrategy
         << "  numStrategies=" << cfg_.numStrategies << "\n"
         << "  Conditions=" << conditions.size()
         << "  TotalRuns="
         << conditions.size() * (1 + modes.size() * setups.size()) << "\n"
         << "============================================================\n\n";

    vector<RunData> allRuns;

    for (const auto &c : conditions) {
        const string ctag = toCondTag(c.rr, c.rv);
        cout << "\n---- Condition: " << ctag << " ----\n";

        RCPSP_Problem::resetGlobalCostSeries();

        // ---- P1 ベースライン ----
        cout << "  Running P1...\n";
        RunData p1 = runP1(c.rr, c.rv);
        // delta/ratio は 0/1 のまま
        allRuns.push_back(p1);
        printProgress(p1);

        // ---- P2/P3 × TW/WD/WR ----
        for (auto sm : setups) {
            for (auto m : modes) {
                cout << "  Running " << toModeTag(m)
                     << "+" << toSetupTag(sm) << "...\n";
                RunData r = runSetup(m, sm, c.rr, c.rv);

                // P1 との差・比率
                r.deltaMsMin   = r.msMin   - p1.msMin;
                r.deltaCostMin = r.costMin - p1.costMin;
                r.ratioMsMin   = (p1.msMin   > 0) ? r.msMin   / p1.msMin   : 1.0;
                r.ratioCostMin = (p1.costMin > 0) ? r.costMin / p1.costMin : 1.0;

                allRuns.push_back(r);
                printProgress(r);
            }
        }
    }

    // ---- 出力ファイル生成 ----
    const int alphaInt = static_cast<int>(cfg_.alphaS * 100);
    const string tag   = prefix_ + "_a" + to_string(alphaInt);

    writeSummaryCSV  ("CMP_"     + tag + ".csv",  allRuns, cfg_.instanceFile, cfg_.alphaS);
    writeParetoCSV   ("PF_"      + tag + ".csv",  allRuns, cfg_.instanceFile, cfg_.alphaS);
    writeFUNFiles    (allRuns, prefix_, cfg_.alphaS);
    writeAsciiParetoPlots(allRuns, prefix_, cfg_.alphaS);

    // ---- コンソール最終サマリー ----
    cout << "\n============================================================\n"
         << "[FINAL SUMMARY]  instance=" << cfg_.instanceFile
         << "  alphaS=" << cfg_.alphaS << "\n"
         << "============================================================\n";
    cout << left
         << setw(12) << "Condition"
         << setw(12) << "Mode+Setup"
         << setw(7)  << "Size"
         << setw(8)  << "msMin"
         << setw(10) << "costMin"
         << setw(8)  << "Δms"
         << setw(10) << "Δcost"
         << setw(8)  << "ratio_ms"
         << right << "\n";
    cout << string(75, '-') << "\n";

    string lastCond;
    for (const auto &r : allRuns) {
        if (r.condTag != lastCond) {
            if (!lastCond.empty()) cout << "\n";
            lastCond = r.condTag;
        }
        string label = r.modeTag + (r.setupTag != "-" ? "+" + r.setupTag : "");
        cout << left
             << setw(12) << r.condTag
             << setw(12) << label
             << setw(7)  << r.paretoSize
             << setw(8)  << fixed << setprecision(0) << r.msMin
             << setw(10) << fixed << setprecision(1) << r.costMin
             << setw(8)  << (r.deltaMsMin >= 0 ? "+" : "")
                         << fixed << setprecision(0) << r.deltaMsMin
             << setw(10) << (r.deltaCostMin >= 0 ? "+" : "")
                         << fixed << setprecision(1) << r.deltaCostMin
             << setw(8)  << fixed << setprecision(3) << r.ratioMsMin
             << right << "\n";
    }
    cout << "============================================================\n";
}

// ============================================================
//  main
//   argv[1] : インスタンスファイル（省略時 j30.sm/j301_1.sm）
//   argv[2] : alphaS（省略時 0.5）
// ============================================================
int main(int argc, char **argv) {
    try {
        const string instanceFile =
            (argc >= 2) ? string(argv[1]) : "j30.sm/j301_1.sm";

        double alphaS = 0.5;
        if (argc >= 3) {
            try { alphaS = std::stod(argv[2]); }
            catch (...) {
                cerr << "[WARN] Invalid alphaS '" << argv[2] << "'. Using 0.5.\n";
            }
        }

        NSGAComparisonRunner::Config cfg;
        cfg.instanceFile      = instanceFile;
        cfg.alphaS            = alphaS;
        cfg.populationSize    = 100;
        cfg.evalsPerStrategy  = 50000;
        cfg.numStrategies     = 4;

        NSGAComparisonRunner runner(cfg);
        auto t0 = std::chrono::steady_clock::now();
        runner.runComparison();
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        cout << "[TIME] " << elapsed << " s\n";
        return 0;
    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
