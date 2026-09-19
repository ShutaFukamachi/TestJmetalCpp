#include "RCPSP_Problem_MaxShiftSeg.h"
#include "Solution.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

// ============================================================
//  getAlpha
// ============================================================
double RCPSP_Problem_MaxShiftSeg::getAlpha() const {
    switch (strategy_) {
        case 1: return 0.0;   // EST 固定（makespan 専用）
        case 2: return 0.5;   // 軽微コスト探索
        case 3: return 1.0;   // 中程度（デフォルト）
        case 4: return 1.5;   // 積極的コスト探索
        default: return 1.0;
    }
}

// ============================================================
//  setStrategy
// ============================================================
void RCPSP_Problem_MaxShiftSeg::setStrategy(int s) {
    strategy_ = s;
    alpha_    = getAlpha();
    // upperLimit_[n..2n-1] は常に R。基底クラスのように halfT で上書きしない。
}

// ============================================================
//  コンストラクタ
// ============================================================
RCPSP_Problem_MaxShiftSeg::RCPSP_Problem_MaxShiftSeg(
        const std::string &filename, int strategy, double rr, bool rv)
    : RCPSP_Problem_MaxShift(filename, strategy, rr, rv)
{
    alpha_ = getAlpha();

    const int n = getNumJobs();
    for (int i = n; i < 2 * n; ++i)
        upperLimit_[i] = (double)R;
}

// ============================================================
//  buildRandomTopoSeg（ファイルスコープ内部ヘルパー）
// ============================================================
static void buildRandomTopoSeg(
        int n,
        const std::vector<std::vector<int>> &successors,
        std::vector<int> &vars,
        std::mt19937 &rng)
{
    std::vector<int> indeg(n, 0);
    for (int j = 0; j < n; ++j)
        for (int s : successors[j])
            if (s >= 0 && s < n) ++indeg[s];

    std::vector<int> avail;
    avail.reserve(n);
    for (int j = 0; j < n; ++j)
        if (indeg[j] == 0) avail.push_back(j);

    std::vector<int> perm;
    perm.reserve(n);
    while (!avail.empty()) {
        std::uniform_int_distribution<int> dis(0, (int)avail.size() - 1);
        int idx = dis(rng);
        int job = avail[idx];
        avail[idx] = avail.back();
        avail.pop_back();
        perm.push_back(job);
        for (int s : successors[job])
            if (s >= 0 && s < n && --indeg[s] == 0)
                avail.push_back(s);
    }
    if ((int)perm.size() != n) {
        perm.resize(n);
        std::iota(perm.begin(), perm.end(), 0);
    }
    for (int i = 0; i < n; ++i) vars[i] = perm[i];
}

// ============================================================
//  evaluate
//
//  セグメント選択型デコード:
//    各活動 j について:
//      ρ_j    = vars[n+j] / R ∈ [0, 1]
//      Wmax_j = round(max(alpha * d_j, beta * T))
//      hi_j   = min(T - d_j, t_mak + Wmax_j)
//      K セグメントに分割 → ρ が指すセグメント内で argmin(cost)
// ============================================================
void RCPSP_Problem_MaxShiftSeg::evaluate(Solution *solution) {
    ++evalCounter_;

    const int n    = getNumJobs();
    const int nRes = instance.nRes;

    auto &vars = solution->getVars();

    // ---- 1. 活動リスト取得 & 先行制約修復 ----
    std::vector<int> seq(n);
    for (int i = 0; i < n; ++i) seq[i] = vars[i];

    if (!checkTopological(seq)) {
        seq = topoRepair(seq, instance.successors, n);
        for (int i = 0; i < n; ++i) vars[i] = seq[i];
    }

    // ---- 2. ホライゾン T ----
    const int    T     = getHorizon();
    const double alpha = alpha_;
    const double beta  = beta_;
    const int    K     = numSegments_;

    // ---- 3. 先行リスト構築 ----
    std::vector<std::vector<int>> preds(n);
    for (int j = 0; j < n; ++j)
        for (int s : instance.successors[j])
            if (s >= 0 && s < n) preds[s].push_back(j);

    // ---- 4. スケジューリング ----
    std::vector<std::vector<int>> usage(nRes, std::vector<int>(T, 0));
    std::vector<int> finish(n, 0);
    std::vector<int> startArr(n, 0);

    auto canPlace = [&](int j, int t) -> bool {
        int d = instance.duration[j];
        if (d <= 0) return true;
        if (t < 0 || t + d > T) return false;
        for (int tau = t; tau < t + d; ++tau)
            for (int k = 0; k < nRes; ++k)
                if (usage[k][tau] + instance.demand[j][k] > capacityAtTime(k, tau))
                    return false;
        return true;
    };

    auto doPlace = [&](int j, int t) {
        int d = instance.duration[j];
        startArr[j] = t;
        if (d <= 0) { finish[j] = t; return; }
        for (int tau = t; tau < t + d; ++tau)
            for (int k = 0; k < nRes; ++k)
                usage[k][tau] += instance.demand[j][k];
        finish[j] = t + d;
    };

    double totalCost = 0.0;

    for (int pos = 0; pos < n; ++pos) {
        int j = seq[pos];
        int d = instance.duration[j];

        // EST: 先行ジョブの最大完了時刻
        int est = 0;
        for (int p : preds[j]) est = std::max(est, finish[p]);

        if (d <= 0) {
            startArr[j] = est;
            finish[j]   = est;
            continue;
        }

        // t_mak: EST 以降で最初に連続配置可能な時刻
        int t_mak = est;
        while (t_mak < T && !canPlace(j, t_mak)) ++t_mak;

        if (t_mak >= T) {
            // [est, T-d] 内に置けない → est を下回る配置は先行制約違反になるため、
            // 後退探索も est までに限定する（.miss_memory/022, 029 参照）。
            // 見つからなければそのスケジュールは実行不能として扱う。
            t_mak = T - d;
            while (t_mak >= est && !canPlace(j, t_mak)) --t_mak;
            if (t_mak < est) {
                // startTimes_/execSlots_ を必ず n 要素に確定させてから返す（.miss_memory/029）。
                solution->startTimes_ = startArr;
                solution->execSlots_.assign(n, {});
                solution->setObjective(0, 1e9);
                solution->setObjective(1, 1e9);
                return;
            }
        }

        // ---- セグメント選択型配置 ----
        // Wmax_j の計算
        double dj  = (double)d;
        double eff = std::max(alpha * dj, beta * (double)T);
        int    Wmax = (int)std::lround(eff);
        int    hi_j = std::min(T - d, t_mak + Wmax);

        if (hi_j <= t_mak) {
            // 余地なし → 最早配置
            doPlace(j, t_mak);
            totalCost += computeJobCostAt(j, t_mak, T);
            continue;
        }

        // ρ の取得
        int    key = vars[n + j];
        double rho = std::max(0, std::min(R, key)) / (double)R;

        // セグメント番号
        int seg = std::min(K - 1, (int)std::floor(rho * K));
        int range = hi_j - t_mak;  // > 0 が保証されている
        int segLo = t_mak + (int)std::lround((double)seg       * range / (double)K);
        int segHi = t_mak + (int)std::lround((double)(seg + 1) * range / (double)K);
        // segHi は最後のセグメントで hi_j に一致するようにクランプ
        segHi = std::min(segHi, hi_j);

        // セグメント内で argmin(cost) & 実行可能
        int    placedAt = -1;
        double bestCost = std::numeric_limits<double>::max();

        for (int t = segLo; t <= segHi; ++t) {
            if (canPlace(j, t)) {
                double c = computeJobCostAt(j, t, T);
                if (c < bestCost) {  // strict '<' → 左詰め保証
                    bestCost = c;
                    placedAt = t;
                }
            }
        }

        // セグメント内に実行可能スロットが無い場合 → フォールバック
        if (placedAt < 0) {
            // 窓 [t_mak, hi_j] 全体で segLo に最も近い実行可能スロットを探す
            int bestDist = std::numeric_limits<int>::max();
            for (int t = t_mak; t <= hi_j; ++t) {
                if (canPlace(j, t)) {
                    int dist = std::abs(t - segLo);
                    if (dist < bestDist) {
                        bestDist = dist;
                        placedAt = t;
                        bestCost = computeJobCostAt(j, t, T);
                    }
                }
            }
        }

        // それも無ければ t_mak
        if (placedAt < 0) {
            placedAt = t_mak;
            bestCost = computeJobCostAt(j, t_mak, T);
        }

        doPlace(j, placedAt);
        totalCost += bestCost;
    }

#ifndef NDEBUG
    // ---- デバッグビルド限定: 先行制約の自己検証（.miss_memory/029 再発防止）----
    for (int a = 0; a < n; ++a) {
        if (instance.duration[a] <= 0) continue;
        for (int s : instance.successors[a]) {
            if (s < 0 || s >= n || instance.duration[s] <= 0) continue;
            if (finish[a] > startArr[s]) {
                std::cerr << "[RCPSP_Problem_MaxShiftSeg::evaluate][ASSERT] precedence violation: "
                          << "job=" << a << " finish=" << finish[a]
                          << " > job=" << s << " start=" << startArr[s] << "\n";
            }
        }
    }
#endif

    // ---- 5. makespan ----
    int makespan = 0;
    for (int j = 0; j < n; ++j) makespan = std::max(makespan, finish[j]);

    // ---- 6. キャッシュ更新 ----
    solution->startTimes_ = startArr;

    solution->execSlots_.resize(n);
    for (int j = 0; j < n; ++j) {
        int dj = instance.duration[j];
        solution->execSlots_[j].clear();
        for (int i = 0; i < dj; ++i)
            solution->execSlots_[j].push_back(startArr[j] + i);
    }

    solution->setObjective(0, (double)makespan);
    solution->setObjective(1, totalCost);
}

// ============================================================
//  createMakespanExtremeSolution
//  全ジョブ ρ=0 → seg 0（最早側）
// ============================================================
Solution* RCPSP_Problem_MaxShiftSeg::createMakespanExtremeSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();

    Solution *sol = new Solution(this);
    auto &vars = sol->getVars();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopoSeg(n, instance.successors, vars, rng);

    for (int j = 0; j < n; ++j) {
        int idx = n + j;
        if (idx >= nVars) break;
        vars[idx] = 0;
    }
    return sol;
}

// ============================================================
//  createCostExtremeSolution
//  全ジョブ ρ=R → 最終セグメント
// ============================================================
Solution* RCPSP_Problem_MaxShiftSeg::createCostExtremeSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();

    Solution *sol = new Solution(this);
    auto &vars = sol->getVars();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopoSeg(n, instance.successors, vars, rng);

    for (int j = 0; j < n; ++j) {
        int idx = n + j;
        if (idx >= nVars) break;
        vars[idx] = R;
    }
    // ダミー端点は常に 0
    if (n > 0 && n     < nVars) vars[n + 0]     = 0;
    if (n > 1 && n+n-1 < nVars) vars[n + n - 1] = 0;
    return sol;
}

// ============================================================
//  createRandomTopoSolution
//
//  初期解生成:
//    活動リスト: Kahn ベースのランダムトポロジカルソート
//    ρ キー  : グループ分け（MaxShiftDur と同様の思想）
//      30%: 全 ρ=0        (makespan 極端; seg 0)
//      40%: ρ ∈ Uniform[0, R/8]  (保守的; 最早側セグメント)
//      30%: ρ ∈ Uniform[0, R]    (完全ランダム; 全セグメント)
//    ダミー端点は常に key=0 に固定。
// ============================================================
Solution* RCPSP_Problem_MaxShiftSeg::createRandomTopoSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();

    Solution *sol = new Solution(this);
    auto &vars = sol->getVars();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopoSeg(n, instance.successors, vars, rng);

    {
        std::uniform_real_distribution<double> prob01(0.0, 1.0);
        double r = prob01(rng);

        const int smallUpper = std::max(1, R / 8);
        std::uniform_int_distribution<int> ms_small(0, smallUpper);
        std::uniform_int_distribution<int> ms_full(0, R);

        for (int j = 0; j < n; ++j) {
            int idx = n + j;
            if (idx >= nVars) break;

            if (r < 0.30) {
                vars[idx] = 0;              // makespan 極端
            } else if (r < 0.70) {
                vars[idx] = ms_small(rng);  // 保守的
            } else {
                vars[idx] = ms_full(rng);   // ランダム
            }
        }
    }

    // ダミー端点は常に 0
    if (n > 0 && n     < nVars) vars[n + 0]     = 0;
    if (n > 1 && n+n-1 < nVars) vars[n + n - 1] = 0;

    return sol;
}
