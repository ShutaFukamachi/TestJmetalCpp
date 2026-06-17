#include "MaxShiftSensitivity.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

// ============================================================
//  コンストラクタ / デストラクタ
// ============================================================
MaxShiftSensitivityAnalyzer::MaxShiftSensitivityAnalyzer(
        const std::string& instanceFile, double rr, bool rv)
    : instanceFile_(instanceFile), rr_(rr), rv_(rv)
{
    // strategy=4 (upper=T/2) で構築。
    // setOutputMaxShift() で上限を超える値も指定できるため、
    // 問題の上限設定は感度分析の結果に影響しない。
    prob_ = new RCPSP_Problem_MaxShift(instanceFile, 4, rr, rv);
}

MaxShiftSensitivityAnalyzer::~MaxShiftSensitivityAnalyzer() {
    delete prob_;
}

// ============================================================
//  extractActivityList
// ============================================================
std::vector<int> MaxShiftSensitivityAnalyzer::extractActivityList(
        Solution* sol, int n)
{
    std::vector<int> al(n);
    const auto &vars = sol->getVars();
    for (int i = 0; i < n; ++i)
        al[i] = vars[i];
    return al;
}

int MaxShiftSensitivityAnalyzer::getT() const { return prob_->getHorizon(); }
int MaxShiftSensitivityAnalyzer::getN() const { return prob_->getNumJobs(); }

// ============================================================
//  sweep
//   activityList を固定し max_shift = 0..T を steps 段階で評価する。
// ============================================================
std::vector<MaxShiftSensitivityAnalyzer::SweepPoint>
MaxShiftSensitivityAnalyzer::sweep(
        const std::vector<int>& activityList, int steps) const
{
    const int T = prob_->getHorizon();
    const int n = prob_->getNumJobs();

    // 一時解を生成（変数の型・数を合わせるために createMakespanExtremeSolution を流用）
    Solution* sol  = prob_->createMakespanExtremeSolution();
    auto &vars = sol->getVars();

    std::vector<SweepPoint> results;
    results.reserve(steps + 1);

    for (int step = 0; step <= steps; ++step) {
        int ms_val = (step * T) / steps;

        // 活動リストをリセット（evaluate() がトポロジカル修復で書き換える場合に備える）
        for (int i = 0; i < n; ++i)
            vars[i] = activityList[i];

        prob_->setOutputMaxShift(ms_val);
        prob_->evaluate(sol);

        double makespan = sol->getObjective(0);
        double cost     = sol->getObjective(1);

        if (makespan < 1e8 && cost < 1e8)
            results.push_back({ms_val, makespan, cost});
    }

    prob_->setOutputMaxShift(-1);
    delete sol;
    return results;
}

// ============================================================
//  runAndSave
// ============================================================
void MaxShiftSensitivityAnalyzer::runAndSave(
        const std::string& outCsvPath, int steps)
{
    const int T = prob_->getHorizon();
    const int n = prob_->getNumJobs();

    std::cout << "\n[MaxShiftSensitivity] instance=" << instanceFile_
              << "  n=" << n << "  T=" << T
              << "  RR=" << rr_ << "  RV=" << (rv_ ? 1 : 0) << "\n";
    std::cout << "  Thresholds: T/8=" << T/8
              << "  T/4=" << T/4
              << "  T/2=" << T/2 << "\n";
    std::cout << "  steps=" << steps << "  output=" << outCsvPath << "\n";

    // --- 参照活動リストを生成 ---
    // min-makespan リスト: max_shift=0 で評価した最早スケジュール
    // min-cost リスト    : max_shift=T/2 で評価したコスト最小スケジュール
    Solution* solMS   = prob_->createMakespanExtremeSolution();
    Solution* solCost = prob_->createCostExtremeSolution();

    prob_->setOutputMaxShift(0);
    prob_->evaluate(solMS);
    prob_->evaluate(solCost);
    prob_->setOutputMaxShift(-1);

    const std::vector<int> alMS   = extractActivityList(solMS,   n);
    const std::vector<int> alCost = extractActivityList(solCost, n);

    const double msMS_base   = solMS->getObjective(0);
    const double costMS_base = solMS->getObjective(1);
    const double msCo_base   = solCost->getObjective(0);
    const double costCo_base = solCost->getObjective(1);

    std::cout << "  [MS-list]   max_shift=0: makespan=" << static_cast<int>(msMS_base)
              << "  cost=" << std::fixed << std::setprecision(1) << costMS_base << "\n";
    std::cout << "  [Cost-list] max_shift=0: makespan=" << static_cast<int>(msCo_base)
              << "  cost=" << costCo_base << "\n";

    delete solMS;
    delete solCost;

    // --- スイープ実行 ---
    const auto sweepMS   = sweep(alMS,   steps);
    const auto sweepCost = sweep(alCost, steps);

    // --- CSV 出力 ---
    std::ofstream ofs(outCsvPath);
    if (!ofs) {
        std::cerr << "[MaxShiftSensitivity] Cannot open: " << outCsvPath << "\n";
        return;
    }

    ofs << "# MaxShift sensitivity analysis\n";
    ofs << "# instance=" << instanceFile_
        << "  T=" << T
        << "  T/8=" << T/8
        << "  T/4=" << T/4
        << "  T/2=" << T/2 << "\n";
    ofs << "max_shift,"
           "makespan_msList,cost_msList,"
           "makespan_costList,cost_costList\n";

    const size_t nPts = std::min(sweepMS.size(), sweepCost.size());
    for (size_t i = 0; i < nPts; ++i) {
        ofs << sweepMS[i].maxShift << ","
            << static_cast<int>(sweepMS[i].makespan)   << ","
            << std::fixed << std::setprecision(1) << sweepMS[i].cost  << ","
            << static_cast<int>(sweepCost[i].makespan) << ","
            << sweepCost[i].cost << "\n";
    }
    std::cout << "[MaxShiftSensitivity] Saved: " << outCsvPath << "\n";

    // --- コンソールサマリ ---
    // コストが最小になる max_shift を特定
    auto findMinCostShift = [](const std::vector<SweepPoint>& pts) {
        double minC = std::numeric_limits<double>::max();
        int    opt  = 0;
        for (auto& p : pts) if (p.cost < minC) { minC = p.cost; opt = p.maxShift; }
        return std::make_pair(opt, minC);
    };
    auto [optMS,   minCostMS]   = findMinCostShift(sweepMS);
    auto [optCost, minCostCost] = findMinCostShift(sweepCost);

    std::cout << "\n  --- Summary ---\n";
    std::cout << "  [MS-list]   min-cost at max_shift=" << optMS
              << " (cost=" << std::fixed << std::setprecision(1) << minCostMS << ")\n";
    std::cout << "  [Cost-list] min-cost at max_shift=" << optCost
              << " (cost=" << minCostCost << ")\n";

    // 各上限候補での値を表示
    const int thresholds[] = {0, T/8, T/4, T/2, T};
    const char* labels[]   = {"0", "T/8", "T/4", "T/2", "T"};
    std::cout << "\n  max_shift | [MS-list] makespan / cost        | [Cost-list] makespan / cost\n";
    std::cout << "  ----------+-------------------------------------+----------------------------\n";
    for (int ti = 0; ti < 5; ++ti) {
        int thr = thresholds[ti];
        // 最も近い sweep 点を探す
        auto nearest = [&](const std::vector<SweepPoint>& pts) -> const SweepPoint* {
            const SweepPoint* best = nullptr;
            int minDist = INT_MAX;
            for (auto& p : pts) {
                int d = std::abs(p.maxShift - thr);
                if (d < minDist) { minDist = d; best = &p; }
            }
            return best;
        };
        const SweepPoint* pMS   = nearest(sweepMS);
        const SweepPoint* pCost = nearest(sweepCost);
        std::cout << "  " << std::setw(9) << labels[ti] << " |"
                  << " ms=" << std::setw(4) << (pMS   ? static_cast<int>(pMS->makespan)   : -1)
                  << "  cost=" << std::setw(9) << std::fixed << std::setprecision(1) << (pMS   ? pMS->cost   : 0.0)
                  << " |"
                  << " ms=" << std::setw(4) << (pCost ? static_cast<int>(pCost->makespan) : -1)
                  << "  cost=" << std::setw(9) << (pCost ? pCost->cost : 0.0)
                  << "\n";
    }

    // 判定: T/4 でコストが plateau に達しているか？
    // plateau 判定: T/4 以降でのコスト改善が T/4 でのコストの 1% 未満
    auto getAtThresh = [&](const std::vector<SweepPoint>& pts, int thr) -> double {
        double bestC = std::numeric_limits<double>::max();
        int    minD  = INT_MAX;
        for (auto& p : pts) {
            int d = std::abs(p.maxShift - thr);
            if (d < minD) { minD = d; bestC = p.cost; }
        }
        return bestC;
    };
    double costAtQ4_MS   = getAtThresh(sweepMS,   T/4);
    double costAtQ4_Co   = getAtThresh(sweepCost, T/4);
    double improvementMS   = (costAtQ4_MS   - minCostMS)   / costAtQ4_MS   * 100.0;
    double improvementCost = (costAtQ4_Co   - minCostCost) / costAtQ4_Co   * 100.0;

    std::cout << "\n  [Verdict] T/4 beyond improvement:\n";
    std::cout << "    MS-list:   " << std::setprecision(2) << improvementMS
              << "% cost reduction beyond T/4 → "
              << (improvementMS > 1.0 ? "T/4 is TOO SMALL, consider T/2" : "T/4 is adequate") << "\n";
    std::cout << "    Cost-list: " << improvementCost
              << "% cost reduction beyond T/4 → "
              << (improvementCost > 1.0 ? "T/4 is TOO SMALL, consider T/2" : "T/4 is adequate") << "\n";
}
