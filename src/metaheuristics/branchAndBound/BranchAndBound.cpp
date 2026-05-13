#include "BranchAndBound.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>

// ============================================================
//  コンストラクタ
// ============================================================
BranchAndBound::BranchAndBound(RCPSP_Problem *problem)
    : problem_(problem)
    , nJobs_(problem->getNumJobs())
    , nRes_(problem->getNumResources())
{
    // 先行ジョブリスト構築 (successors の逆引き)
    const auto &succs = problem_->getSuccessors();
    preds_.resize(nJobs_);
    for (int j = 0; j < nJobs_; ++j) {
        for (int s : succs[j]) {
            preds_[s].push_back(j);
        }
    }

    // ホライゾン = 全処理時間の合計 (時間依存容量テーブルが長ければそちら)
    horizon_ = 0;
    for (int d : problem_->getDurations()) horizon_ += d;
    const auto &cap_t = problem_->getCapacityT();
    if (!cap_t.empty() && !cap_t[0].empty()) {
        int tvT = (int)cap_t[0].size();
        if (tvT > horizon_) horizon_ = tvT;
    }
    if (horizon_ <= 0) horizon_ = 100;

    computeESTLFT();

    // 各ジョブの最安コストを事前計算
    cheapestCost_.resize(nJobs_, 0.0);
    for (int j = 0; j < nJobs_; ++j) {
        cheapestCost_[j] = cheapestCostForJob(j);
    }
}

// ============================================================
//  EST / LFT 計算
// ============================================================
void BranchAndBound::computeESTLFT() {
    const auto &dur   = problem_->getDurations();
    const auto &succs = problem_->getSuccessors();

    EST_.assign(nJobs_, 0);
    LFT_.assign(nJobs_, horizon_);

    // 前向きパス: EST 伝播
    bool changed = true;
    while (changed) {
        changed = false;
        for (int j = 0; j < nJobs_; ++j) {
            for (int s : succs[j]) {
                int cand = EST_[j] + dur[j];
                if (cand > EST_[s]) { EST_[s] = cand; changed = true; }
            }
        }
    }

    // 後ろ向きパス: LFT 伝播
    LFT_[nJobs_ - 1] = horizon_;
    changed = true;
    while (changed) {
        changed = false;
        for (int j = nJobs_ - 1; j >= 0; --j) {
            for (int s : succs[j]) {
                int cand = LFT_[s] - dur[s];
                if (cand < LFT_[j]) { LFT_[j] = cand; changed = true; }
            }
        }
    }
}

// ============================================================
//  cheapestCostForJob
//  [EST_[j], LFT_[j]-dur[j]] の範囲で連続 dur[j] 時間の最安スロット
//  (資源制約は無視した楽観的な下界)
// ============================================================
double BranchAndBound::cheapestCostForJob(int j) {
    const auto &dur = problem_->getDurations();
    int d = dur[j];
    if (d <= 0) return 0.0;

    int tLow  = EST_[j];
    int tHigh = LFT_[j] - d;
    if (tHigh < tLow) return 0.0;

    double minCost = std::numeric_limits<double>::max();
    for (int t = tLow; t <= tHigh; ++t) {
        double c = problem_->computeJobCostAt(j, t, horizon_);
        if (c < minCost) minCost = c;
    }
    return (minCost == std::numeric_limits<double>::max()) ? 0.0 : minCost;
}

// ============================================================
//  C_min: 資源無視・最安配置のコスト下界
// ============================================================
double BranchAndBound::computeCmin() {
    double total = 0.0;
    for (int j = 0; j < nJobs_; ++j) total += cheapestCost_[j];
    return total;
}

// ============================================================
//  資源配置
// ============================================================
bool BranchAndBound::canPlace(int j, int t,
                               const std::vector<std::vector<int>> &usage) {
    const auto &dur    = problem_->getDurations();
    const auto &demand = problem_->getDemand();
    const auto &cap    = problem_->getCapacity();
    const auto &cap_t  = problem_->getCapacityT();
    int d = dur[j];
    if (d <= 0) return true;
    if (t < 0 || t + d > horizon_) return false;

    for (int tau = t; tau < t + d; ++tau) {
        for (int k = 0; k < nRes_; ++k) {
            int capK = (!cap_t.empty() && k < (int)cap_t.size()
                        && tau < (int)cap_t[k].size())
                       ? cap_t[k][tau] : cap[k];
            if (usage[k][tau] + demand[j][k] > capK) return false;
        }
    }
    return true;
}

void BranchAndBound::placeJob(int j, int t,
                               std::vector<std::vector<int>> &usage) {
    const auto &dur    = problem_->getDurations();
    const auto &demand = problem_->getDemand();
    int d = dur[j];
    for (int tau = t; tau < t + d; ++tau)
        for (int k = 0; k < nRes_; ++k)
            usage[k][tau] += demand[j][k];
}

void BranchAndBound::unplaceJob(int j, int t,
                                 std::vector<std::vector<int>> &usage) {
    const auto &dur    = problem_->getDurations();
    const auto &demand = problem_->getDemand();
    int d = dur[j];
    for (int tau = t; tau < t + d; ++tau)
        for (int k = 0; k < nRes_; ++k)
            usage[k][tau] -= demand[j][k];
}

double BranchAndBound::jobCostAt(int j, int t) {
    return problem_->computeJobCostAt(j, t, horizon_);
}

// ============================================================
//  下界
// ============================================================
int BranchAndBound::lbMakespan(const std::vector<int>  &startTime,
                                const std::vector<bool> &scheduled) {
    const auto &dur = problem_->getDurations();
    int lb = 0;
    // (a) 配置済みの最大完了時刻
    for (int j = 0; j < nJobs_; ++j)
        if (scheduled[j])
            lb = std::max(lb, startTime[j] + dur[j]);
    // (b) 未スケジュールの EST[j]+dur[j]
    for (int j = 0; j < nJobs_; ++j)
        if (!scheduled[j])
            lb = std::max(lb, EST_[j] + dur[j]);
    return lb;
}

double BranchAndBound::lbCost(const std::vector<bool> &scheduled,
                               double currentCost) {
    double lb = currentCost;
    for (int j = 0; j < nJobs_; ++j)
        if (!scheduled[j]) lb += cheapestCost_[j];
    return lb;
}

// ============================================================
//  再帰分岐
// ============================================================
void BranchAndBound::branch(
    std::vector<int>              &startTime,
    std::vector<bool>             &scheduled,
    std::vector<std::vector<int>> &usage,
    double                         currentCost,
    double                         epsilon,
    BnBSolution*                  &best)
{
    const auto &dur = problem_->getDurations();

    // ── 次に配置するジョブを選ぶ (最小 LFT の eligible job) ──
    int nextJob = -1;
    int minLFT  = std::numeric_limits<int>::max();
    for (int j = 1; j < nJobs_; ++j) {    // job 0 は初期化時に配置済み
        if (scheduled[j]) continue;
        bool eligible = true;
        for (int p : preds_[j]) {
            if (!scheduled[p]) { eligible = false; break; }
        }
        if (!eligible) continue;
        if (LFT_[j] < minLFT) { minLFT = LFT_[j]; nextJob = j; }
    }

    // 全ジョブ配置完了 → 解を更新
    if (nextJob == -1) {
        int makespan = 0;
        for (int j = 0; j < nJobs_; ++j)
            makespan = std::max(makespan, startTime[j] + dur[j]);
        if (best == nullptr || makespan < best->makespan) {
            delete best;
            best = new BnBSolution{makespan, currentCost, startTime};
        }
        return;
    }

    // ── グローバル枝刈り (ループ前) ──────────────────────────
    // ① コスト下界
    double lbCostBase = lbCost(scheduled, currentCost);
    if (lbCostBase > epsilon) return;

    // ② メイクスパン下界
    if (best != nullptr) {
        if (lbMakespan(startTime, scheduled) >= best->makespan) return;
    }

    // ── 時刻の列挙 ──────────────────────────────────────────
    int j     = nextJob;
    int d     = dur[j];

    // 先行完了を考慮した動的 EST
    int dynEST = EST_[j];
    for (int p : preds_[j])
        dynEST = std::max(dynEST, startTime[p] + dur[p]);

    int tHigh = LFT_[j] - d;

    for (int t = dynEST; t <= tHigh; ++t) {
        // ③ LFT 超過チェック
        if (t + d > LFT_[j]) break;

        // ④ 資源実行可能チェック
        if (!canPlace(j, t, usage)) continue;

        double addCost = jobCostAt(j, t);
        double newCost = currentCost + addCost;

        // ① コスト下界 (j を配置後の残コスト込み)
        //   lbCostBase - cheapestCost_[j] + addCost > epsilon?
        if (lbCostBase - cheapestCost_[j] + addCost > epsilon) continue;

        // ② メイクスパン下界 (j の完了時刻)
        if (best != nullptr) {
            if (t + d >= best->makespan) continue;
        }

        // 配置・再帰・バックトラック
        startTime[j] = t;
        scheduled[j] = true;
        placeJob(j, t, usage);

        branch(startTime, scheduled, usage, newCost, epsilon, best);

        unplaceJob(j, t, usage);
        scheduled[j] = false;
        startTime[j] = -1;
    }
}

// ============================================================
//  ε固定で BnB を1回実行
// ============================================================
BnBSolution* BranchAndBound::solveForEpsilon(double epsilon) {
    std::vector<int>              startTime(nJobs_, -1);
    std::vector<bool>             scheduled(nJobs_, false);
    std::vector<std::vector<int>> usage(nRes_, std::vector<int>(horizon_, 0));

    // ダミー開始ジョブ (job 0) を t=0 に配置
    startTime[0] = 0;
    scheduled[0] = true;

    BnBSolution *best = nullptr;
    branch(startTime, scheduled, usage, 0.0, epsilon, best);
    return best;
}

// ============================================================
//  ε制約法: パレートフロントを求める
// ============================================================
std::vector<BnBSolution> BranchAndBound::solveEpsilonConstraint(int numDivisions) {
    std::cout << "[BnB] Starting epsilon-constraint method...\n";

    // STEP 0-1: C_min (資源無視・最安配置のコスト下界)
    double C_min = computeCmin();
    std::cout << "[BnB] C_min = " << C_min << "\n";

    // STEP 0-2: C_max (ε=∞ でメイクスパン最小化 → その解のコストが C_max)
    std::cout << "[BnB] Solving unconstrained (epsilon=inf) for C_max...\n";
    BnBSolution *unc = solveForEpsilon(std::numeric_limits<double>::infinity());
    if (!unc) {
        std::cerr << "[BnB] No feasible solution found!\n";
        return {};
    }
    double C_max = unc->cost;
    std::cout << "[BnB] C_max = " << C_max
              << "  (makespan=" << unc->makespan << ")\n";

    std::vector<BnBSolution> paretoFront;
    paretoFront.push_back(*unc);
    delete unc;

    if (C_max <= C_min + 1e-9 || numDivisions <= 0) {
        return paretoFront;
    }

    // STEP 1: ε を C_max から C_min まで numDivisions 分割で走査
    double step       = (C_max - C_min) / numDivisions;
    double lastCost   = C_max;   // 直前の解のコスト

    for (int k = 1; k <= numDivisions; ++k) {
        // 固定刻みと「解コスト-1」の小さい方を ε とする
        double epsFixed = C_max - k * step;
        double epsilon  = std::min(epsFixed, lastCost - 1.0);

        if (epsilon < C_min - 1e-9) break;

        std::cout << "[BnB] k=" << k << "  epsilon=" << epsilon << "\n";

        BnBSolution *sol = solveForEpsilon(epsilon);
        if (sol) {
            std::cout << "[BnB]   -> makespan=" << sol->makespan
                      << "  cost=" << sol->cost << "\n";
            paretoFront.push_back(*sol);
            lastCost = sol->cost;
            delete sol;
        } else {
            std::cout << "[BnB]   -> no solution\n";
        }
    }

    // makespan 昇順にソート
    std::sort(paretoFront.begin(), paretoFront.end(),
              [](const BnBSolution &a, const BnBSolution &b) {
                  return a.makespan < b.makespan;
              });

    std::cout << "[BnB] Pareto front size = " << paretoFront.size() << "\n";
    return paretoFront;
}