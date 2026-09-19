// ============================================================
//  main_ParallelSGS_AB.cpp  —  生成スキーム A/B（Serial SGS ⇄ Parallel SGS）
//
//  目的: ゼミ助言「配置でなく生成スキーム/解構造」を受けた本命A。
//        MaxShift エンコーディングのデコーダを Serial SGS（現行）から
//        Parallel SGS（時間駆動・non-delay）に切替えたとき、最終パレート
//        フロント（makespan × cost）が改善するか＝到達可能スケジュール集合
//        が変わって makespan の床（j309_1 RR050 で 139, MIP最適=125）を
//        下げられるかを、フロントのグラフで判断するためのデータを出力する。
//
//  方法: 4戦略並列の combined front を条件ごとに trials 試行し、全試行の
//        和集合から非支配解を取り出して FUN_PSGS_<tag>_<cond>.txt に書き出す。
//        Parallel SGS は新規コードのため、フロント全解の先行/資源制約違反を
//        検証し、違反があれば警告する（.miss_memory/018,019 の教訓）。
//
//  条件: SER (Serial, baseline) / PAR (Parallel) /
//        SER+A1 / PAR+A1  （A1=priority-rule シード）
//
//  使い方: PSGS [instance] [rr] [trials] [evalsPerStrategy]
//     例:  PSGS j30.sm/j309_1.sm 0.50 3 100000
//
//  出力後: python ../visualize_psgs.py --tag <tag>  でフロントを重ね描画。
// ============================================================
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
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
#include "operators/crossover/MaxShiftCrossover.h"
#include "operators/mutation/MaxShiftMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"

using namespace std;

// フロント上の1解（目的値 + 開始時刻ベクトル: 実行可能性検証用）
struct FrontPoint {
    double ms;
    double cost;
    vector<int> start;
};

// ---- 実行可能性検証（ダミー端点 d<=0 は無視）----
static void checkFeasibility(RCPSP_Problem_MaxShift *prob,
                             const vector<int> &start,
                             int &precViol, int &resViol)
{
    const int n    = prob->getNumJobs();
    const int nRes = prob->getNumResources();
    const auto &dur  = prob->getDurations();
    const auto &dem  = prob->getDemand();
    const auto &cap  = prob->getCapacity();
    const auto &capT = prob->getCapacityT();
    const auto &succ = prob->getSuccessors();
    const int T = prob->getHorizon();
    auto capAt = [&](int k, int t) -> int {
        if (!capT.empty() && !capT[k].empty() && t < (int)capT[k].size())
            return capT[k][t];
        return cap[k];
    };
    precViol = 0; resViol = 0;
    if ((int)start.size() < n) { precViol = -1; return; }
    for (int i = 0; i < n; ++i) {
        if (dur[i] <= 0) continue;
        for (int s : succ[i]) {
            if (s < 0 || s >= n || dur[s] <= 0) continue;
            if (start[i] + dur[i] > start[s]) ++precViol;
        }
    }
    vector<vector<int>> usage(nRes, vector<int>(T, 0));
    for (int j = 0; j < n; ++j) {
        if (dur[j] <= 0) continue;
        for (int t = start[j]; t < start[j] + dur[j] && t < T; ++t)
            for (int k = 0; k < nRes; ++k) usage[k][t] += dem[j][k];
    }
    for (int t = 0; t < T; ++t)
        for (int k = 0; k < nRes; ++k)
            if (usage[k][t] > capAt(k, t)) ++resViol;
}

// ---- 1試行（4戦略 combined front）を実行し、front 上の解を collector に追加 ----
static void runOnce(const string &instFile, double rr, bool rv,
                    int popSize, int evalsPerStrategy, int numStr,
                    int a1flag, int parFlag,
                    vector<FrontPoint> &collector,
                    RCPSP_Problem_MaxShift *&checkerProb)
{
    using StratResult = pair<SolutionSet*, RCPSP_Problem_MaxShift*>;
    vector<future<StratResult>> futures;
    futures.reserve(numStr);

    for (int s = 1; s <= numStr; ++s) {
        futures.push_back(async(launch::async,
            [=]() -> StratResult {
                auto *prob = new RCPSP_Problem_MaxShift(instFile, s, rr, rv);
                prob->resetEvalCounter();
                prob->setParallelSGS(parFlag != 0);   // 生成スキーム切替

                Algorithm *algo = new NSGAII(prob);
                int popSz  = popSize;
                int maxEv  = evalsPerStrategy;
                int lsFlag = 0;
                int a1     = a1flag;
                algo->setInputParameter("populationSize", &popSz);
                algo->setInputParameter("maxEvaluations", &maxEv);
                algo->setInputParameter("useLocalSearch",  &lsFlag);
                algo->setInputParameter("a1PrioritySeed",   &a1);

                double crossP = 0.9;
                double mutP   = 1.0 / (double)prob->getNumberOfVariables();
                algo->addOperator("crossover", new MaxShiftCrossover(crossP));
                algo->addOperator("mutation",  new MaxShiftMutation(mutP, prob));
                map<string, void*> sel;
                algo->addOperator("selection", new BinaryTournament2(sel));

                SolutionSet *pop = algo->execute();

                SolutionSet *result = new SolutionSet(popSize * 4);
                Ranking ranking(pop);
                if (ranking.getNumberOfSubfronts() > 0) {
                    SolutionSet *f0 = ranking.getSubfront(0);
                    for (int i = 0; i < f0->size(); ++i)
                        result->add(new Solution(f0->get(i)));
                }
                delete pop;
                delete algo;
                return {result, prob};
            }));
    }

    SolutionSet *combined = new SolutionSet(numStr * popSize * 4);
    vector<RCPSP_Problem_MaxShift*> probs;
    for (int s = 0; s < numStr; ++s) {
        auto [res, prob] = futures[s].get();
        for (int i = 0; i < res->size(); ++i)
            combined->add(new Solution(res->get(i)));
        delete res;
        probs.push_back(prob);
    }

    Ranking finalR(combined);
    if (finalR.getNumberOfSubfronts() > 0) {
        SolutionSet *f0 = finalR.getSubfront(0);
        for (int i = 0; i < f0->size(); ++i) {
            double ms   = f0->get(i)->getObjective(0);
            double cost = f0->get(i)->getObjective(1);
            if (ms >= 1e8 || cost >= 1e8) continue;   // 実行不能解を除外
            collector.push_back({ms, cost, f0->get(i)->startTimes_});
        }
    }

    // 検証用に prob を1つ流用（同一インスタンス）。呼び出し側で delete。
    if (checkerProb == nullptr && !probs.empty()) {
        checkerProb = probs[0];
        probs[0] = nullptr;
    }
    delete combined;
    for (auto *p : probs) delete p;
}

// ---- (ms,cost) の非支配フィルタ（最小化2目的）----
static vector<FrontPoint> nonDominated(const vector<FrontPoint> &pts) {
    vector<FrontPoint> out;
    for (size_t i = 0; i < pts.size(); ++i) {
        bool dom = false;
        for (size_t k = 0; k < pts.size(); ++k) {
            if (k == i) continue;
            // pts[k] が pts[i] を支配するか
            bool le = pts[k].ms <= pts[i].ms && pts[k].cost <= pts[i].cost;
            bool lt = pts[k].ms <  pts[i].ms || pts[k].cost <  pts[i].cost;
            if (le && lt) { dom = true; break; }
        }
        if (!dom) {
            // 完全重複（同一 ms,cost）を1点に集約
            bool dup = false;
            for (const auto &o : out)
                if (std::abs(o.ms - pts[i].ms) < 1e-9 &&
                    std::abs(o.cost - pts[i].cost) < 1e-6) { dup = true; break; }
            if (!dup) out.push_back(pts[i]);
        }
    }
    return out;
}

int main(int argc, char **argv) {
    string instFile         = (argc >= 2) ? argv[1] : "j30.sm/j309_1.sm";
    double rr               = (argc >= 3) ? atof(argv[2]) : 0.50;
    int    trials           = (argc >= 4) ? atoi(argv[3]) : 3;
    int    evalsPerStrategy = (argc >= 5) ? atoi(argv[4]) : 100000;
    const bool rv           = false;
    const int  popSize      = 100;
    const int  numStr       = 4;

    // 出力ファイル名タグ（例: j309_1_RR050）
    string base = instFile;
    size_t slash = base.find_last_of("/\\");
    if (slash != string::npos) base = base.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != string::npos) base = base.substr(0, dot);
    int rrPct = (int)std::lround(rr * 100.0);
    char rrbuf[16]; snprintf(rrbuf, sizeof(rrbuf), "RR%03d", rrPct);
    string tag = base + "_" + rrbuf;

    cout << "=== Parallel SGS A/B (generation scheme) ===\n";
    cout << "instance=" << instFile << "  rr=" << rr << "  rv=" << rv
         << "  trials=" << trials << "  evals/strategy=" << evalsPerStrategy
         << "  popSize=" << popSize << "  strategies=" << numStr << "\n";
    cout << "tag=" << tag << "\n\n";

    // 条件定義: A1 の有無 × Serial/Parallel の4条件
    struct C { string name; string fn; int a1; int par; };
    const vector<C> C4 = {
        {"SER      (Serial, baseline)",  "serial",      0, 0},
        {"PAR      (Parallel SGS)",      "parallel",    0, 1},
        {"SER+A1   (Serial + A1 seed)",  "serial_a1",   1, 0},
        {"PAR+A1   (Parallel + A1 seed)","parallel_a1", 1, 1},
    };

    auto t0 = chrono::steady_clock::now();

    cout << left << setw(30) << "condition"
         << right << setw(8) << "ms*" << setw(12) << "cost@ms*"
         << setw(12) << "minCost" << setw(8) << "front"
         << setw(14) << "infeas-drop" << "\n";
    cout << string(84, '-') << "\n";

    for (const auto &c : C4) {
        vector<FrontPoint> all;
        RCPSP_Problem_MaxShift *checker = nullptr;
        for (int t = 0; t < trials; ++t)
            runOnce(instFile, rr, rv, popSize, evalsPerStrategy, numStr,
                    c.a1, c.par, all, checker);

        // ---- 実行不能解を非支配計算の前に除外 ----
        //  Serial デコーダの末尾フォールバック（RCPSP_Problem_MaxShift.cpp:179-188）は
        //  [est,T) に置けないジョブを est より前に配置し得るため、稀に先行制約違反の
        //  phantom（makespan が真の最適を下回る等）を生む（.miss_memory/018 と同型）。
        //  これを front に残すと serial が不当に良く見えるので、両スキームとも
        //  実行可能解のみでフロントを構成する。
        int infeasCount = 0, worstPrec = 0, worstRes = 0;
        vector<FrontPoint> feas;
        if (checker) {
            for (const auto &p : all) {
                int pv = 0, rvio = 0;
                checkFeasibility(checker, p.start, pv, rvio);
                if (pv == 0 && rvio == 0) {
                    feas.push_back(p);
                } else {
                    ++infeasCount;
                    worstPrec = max(worstPrec, pv);
                    worstRes  = max(worstRes,  rvio);
                }
            }
        } else {
            feas = all;
        }

        vector<FrontPoint> front = nonDominated(feas);

        // 統計（実行可能フロントのみ）
        double msStar = 1e18, costAtMs = 1e18, minCost = 1e18;
        for (const auto &p : front) {
            if (p.ms < msStar) { msStar = p.ms; costAtMs = p.cost; }
            minCost = min(minCost, p.cost);
        }

        // FUN ファイル出力
        string fname = "FUN_PSGS_" + tag + "_" + c.fn + ".txt";
        ofstream ofs(fname);
        for (const auto &p : front)
            ofs << (long long)p.ms << " " << fixed << setprecision(1) << p.cost << "\n";
        ofs.close();

        cout << left << setw(30) << c.name << right
             << setw(8) << (int)msStar
             << setw(12) << fixed << setprecision(0) << costAtMs
             << setw(12) << minCost
             << setw(8) << (int)front.size();
        if (infeasCount == 0)
            cout << setw(14) << "clean";
        else
            cout << setw(8) << "dropped" << "(" << infeasCount
                 << " prec<=" << worstPrec << " res<=" << worstRes << ")";
        cout << "   -> " << fname << "\n";

        delete checker;
    }

    cout << string(84, '-') << "\n";
    cout << "(lower ms* / lower cost = better front; MIP optimum for j309_1 RR050 = 125)\n";
    cout << "plot: python ../visualize_psgs.py --tag " << tag << "\n";

    double elapsed = chrono::duration<double>(
        chrono::steady_clock::now() - t0).count();
    cout << "[TIME] " << elapsed << " s\n";
    return 0;
}
