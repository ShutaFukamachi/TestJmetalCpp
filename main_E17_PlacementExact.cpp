// ============================================================
//  main_E17_PlacementExact.cpp  —  E17: 順列固定＋配置厳密化（matheuristic）
//
//  目的: NSGA-II(MaxShift) が見つけた各 makespan 水準について、
//        「配置（開始時刻）だけ」を小規模 MIP で厳密にコスト最小化し、
//        Serial SGS の貪欲配置が残す系統的コスト差 (+6.4% 等) を詰める。
//
//  二層構造（研究上の新規性）:
//        上層 = GA が活動リスト（順列）と makespan 水準を探索
//        下層 = MIP が makespan 水準を固定してコスト最適な配置を厳密に解く
//        （MIP は evaluate() 内ではなく、最終フロントの後処理として1回だけ呼ぶ。
//          旧 FBI の失敗＝全評価で justification して2目的を壊した轍を踏まない）
//
//  「配置厳密化」の定式化（RCPSP_MIP_Solver::solveCost を再利用）:
//        min  Σ_j c_j(s_j)
//        s.t. 先行制約, 時変資源制約, makespan ≤ M
//        ※ 活動リストは先行制約以外に妥当な硬制約を課さないため、
//          恣意的な順序制約は追加しない（total-order 制約は SGS 開始時刻が
//          リスト順に単調でないため過剰・不健全）。makespan 水準 M を
//          GA から受け取ることが「順列/水準の固定」に相当する。
//
//  計測: 各 MIP ソルブの壁時計時間を出力する。合計が過大なら採用しない
//        （ユーザー方針: 計算コストが増えすぎるなら不採用）。
//
//  使い方: E17 [instance] [rr] [evalsPerStrategy] [maxLevels] [perSolveTimeLimit]
//     例:  E17 j30.sm/j3047_1.sm 0.50 80000 12 60
// ============================================================
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "core/Algorithm.h"
#include "core/SolutionSet.h"
#include "Solution.h"
#include "metaheuristics/nsgaII/NSGAII.h"
#include "problems/RCPSP_Problem_MaxShift.h"
#include "problems/RCPSP_MIP_Solver.h"
#include "operators/crossover/MaxShiftCrossover.h"
#include "operators/mutation/MaxShiftMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"

using namespace std;

// 1 戦略の NSGA-II front0 を返す（MaxShift P1, A1 シード ON）
static void runStrategyInto(const string &instFile, double rr, bool rv,
                            int strategy, int popSize, int evals,
                            SolutionSet *sink)
{
    auto *prob = new RCPSP_Problem_MaxShift(instFile, strategy, rr, rv);
    prob->resetEvalCounter();

    Algorithm *algo = new NSGAII(prob);
    int popSz = popSize, maxEv = evals, lsFlag = 0, a1 = 1;
    algo->setInputParameter("populationSize", &popSz);
    algo->setInputParameter("maxEvaluations", &maxEv);
    algo->setInputParameter("useLocalSearch", &lsFlag);
    algo->setInputParameter("a1PrioritySeed", &a1);

    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob->getNumberOfVariables();
    algo->addOperator("crossover", new MaxShiftCrossover(crossP));
    algo->addOperator("mutation",  new MaxShiftMutation(mutP, prob));
    map<string, void*> sel;
    algo->addOperator("selection", new BinaryTournament2(sel));

    SolutionSet *pop = algo->execute();
    Ranking ranking(pop);
    if (ranking.getNumberOfSubfronts() > 0) {
        SolutionSet *f0 = ranking.getSubfront(0);
        for (int i = 0; i < f0->size(); ++i)
            sink->add(new Solution(f0->get(i)));
    }
    delete pop;
    delete algo;
    delete prob;
}

int main(int argc, char **argv) {
    string instFile = (argc >= 2) ? argv[1] : "j30.sm/j3047_1.sm";
    double rr       = (argc >= 3) ? atof(argv[2]) : 0.50;
    int    evals    = (argc >= 4) ? atoi(argv[3]) : 80000;
    int    maxLevels= (argc >= 5) ? atoi(argv[4]) : 12;   // 厳密化する makespan 水準の上限
    double tLimit   = (argc >= 6) ? atof(argv[5]) : 60.0; // 1 ソルブの時間制限[秒]
    const bool rv   = false;
    const int  popSize = 100;
    const int  numStr  = 4;

    cout << "=== E17: 順列固定＋配置厳密化 (GA makespan levels -> MIP cost-optimal placement) ===\n";
    cout << "instance=" << instFile << "  rr=" << rr << "  rv=" << rv
         << "  evals/strategy=" << evals << "  strategies=" << numStr
         << "  maxLevels=" << maxLevels << "  perSolveTimeLimit=" << tLimit << "s\n\n";

    // ---- 上層: GA でフロント取得（4戦略 combined）----
    auto tGA0 = chrono::steady_clock::now();
    SolutionSet *combined = new SolutionSet(numStr * popSize * 4);
    for (int s = 1; s <= numStr; ++s)
        runStrategyInto(instFile, rr, rv, s, popSize, evals, combined);

    // makespan 水準ごとの GA 最小コストを集計（実行可能解のみ）
    map<int, double> gaCostByMs;
    Ranking finalR(combined);
    if (finalR.getNumberOfSubfronts() > 0) {
        SolutionSet *f0 = finalR.getSubfront(0);
        for (int i = 0; i < f0->size(); ++i) {
            double ms = f0->get(i)->getObjective(0);
            double c  = f0->get(i)->getObjective(1);
            if (ms >= 1e8) continue;
            int M = (int)llround(ms);
            auto it = gaCostByMs.find(M);
            if (it == gaCostByMs.end() || c < it->second) gaCostByMs[M] = c;
        }
    }
    delete combined;
    double gaSec = chrono::duration<double>(chrono::steady_clock::now() - tGA0).count();

    if (gaCostByMs.empty()) {
        cout << "[E17] GA front is empty (all infeasible). Abort.\n";
        return 1;
    }

    cout << "[E17] GA combined front: " << gaCostByMs.size()
         << " distinct makespan levels  (GA time=" << fixed << setprecision(1)
         << gaSec << "s)\n";
    cout << "      makespan range = [" << gaCostByMs.begin()->first << ", "
         << gaCostByMs.rbegin()->first << "]\n\n";

    // ---- 下層: 各 makespan 水準を MIP でコスト最適配置 ----
    // 計算コストを抑えるため、makespan 昇順で最大 maxLevels 水準のみ厳密化する。
    RCPSP_MIP_Solver mip(instFile, rr, rv);
    RCPSP_MIP_Solver::Config cfg;
    cfg.timeLimit = tLimit;
    cfg.threads   = 4;
    cfg.verbose   = false;

    cout << left << setw(9) << "makespan"
         << right << setw(14) << "GA_cost"
         << setw(14) << "MIP_cost"
         << setw(10) << "gap%"
         << setw(10) << "solve_s"
         << setw(9) << "status" << "\n";
    cout << string(66, '-') << "\n";

    // フロント全体を代表させるため、makespan 水準を等間隔サンプリングする
    // （最小 makespan 端＝時間圧縮側 と 最小コスト端＝高 makespan 側 の両方を含める）。
    vector<pair<int,double>> levels(gaCostByMs.begin(), gaCostByMs.end());
    vector<int> pick;
    int nLv = (int)levels.size();
    if (nLv <= maxLevels) {
        for (int i = 0; i < nLv; ++i) pick.push_back(i);
    } else {
        for (int s = 0; s < maxLevels; ++s)
            pick.push_back((int)llround((double)s * (nLv - 1) / (maxLevels - 1)));
        pick.erase(unique(pick.begin(), pick.end()), pick.end());
    }

    int    level = 0;
    double mipTotalSec = 0.0;
    double sumGaGap = 0.0; int gapCount = 0;
    double worstSolve = 0.0;
    for (int idx : pick) {
        int    M      = levels[idx].first;
        double gaCost = levels[idx].second;

        auto t0 = chrono::steady_clock::now();
        RCPSP_MIP_Solver::Result r = mip.solveCost(M, cfg);
        double sec = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
        mipTotalSec += sec;
        worstSolve = max(worstSolve, sec);

        string status = !r.feasible ? "INFEAS" : (r.optimal ? "opt" : "gap");
        double gapPct = (r.feasible && r.cost > 0)
                        ? 100.0 * (gaCost - r.cost) / r.cost : 0.0;
        if (r.feasible) { sumGaGap += gapPct; ++gapCount; }

        cout << left << setw(9) << M
             << right << fixed << setprecision(0)
             << setw(14) << gaCost
             << setw(14) << (r.feasible ? r.cost : 0.0)
             << setprecision(2)
             << setw(10) << gapPct
             << setw(10) << sec
             << setw(9) << status << "\n";
        ++level;
    }

    cout << string(66, '-') << "\n";
    cout << "levels refined = " << gapCount
         << "   mean GA-over-MIP cost gap = " << fixed << setprecision(2)
         << (gapCount ? sumGaGap / gapCount : 0.0) << "%\n";
    cout << "[TIME] GA=" << setprecision(1) << gaSec
         << "s   MIP total=" << mipTotalSec
         << "s   MIP worst single solve=" << worstSolve << "s   (levels="
         << level << ")\n";
    cout << "(gap% = (GA_cost - MIP_cost)/MIP_cost; 正なら貪欲配置が損している分)\n";
    return 0;
}
