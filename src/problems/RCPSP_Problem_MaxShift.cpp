#include "RCPSP_Problem_MaxShift.h"
#include "Solution.h"
#include "Variable.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

// ============================================================
//  コンストラクタ
//   基底クラス RCPSP_Problem のコンストラクタを呼び、
//   vars[n..2n-1] の上限を T/2 に更新する。
//   （基底クラスは schedObj 用に upperLimit_=1 にしているため）
// ============================================================
// ============================================================
//  getEffectiveHalfT
//   strategy に対応する max_shift 上限を返す。
// ============================================================
int RCPSP_Problem_MaxShift::getEffectiveHalfT() const {
    int T = getHorizon();
    switch (strategy_) {
        case 1: return 0;                       // EST 固定
        case 2: return std::max(1, T / 8);      // 軽微コスト探索
        case 3: return std::max(1, T / 4);      // 中程度（デフォルト）
        case 4: return std::max(1, T / 2);      // 積極的コスト探索
        default: return std::max(1, T / 4);
    }
}

// ============================================================
//  setStrategy
//   strategy を変更し、vars[n..2n-1] の上限も更新する。
// ============================================================
void RCPSP_Problem_MaxShift::setStrategy(int s) {
    strategy_ = s;
    int halfT = getEffectiveHalfT();
    int n = getNumJobs();
    for (int i = n; i < 2 * n; ++i) {
        upperLimit_[i] = (double)halfT;
    }
}

// ============================================================
//  コンストラクタ
// ============================================================
RCPSP_Problem_MaxShift::RCPSP_Problem_MaxShift(
        const std::string &filename, int strategy, double rr, bool rv)
    : RCPSP_Problem(filename, strategy, rr, rv)
{
    int n     = getNumJobs();
    int halfT = getEffectiveHalfT();  // strategy 依存の上限

    // vars[n..2n-1] の変数上限を strategy に応じた値に更新
    for (int i = n; i < 2 * n; ++i) {
        upperLimit_[i] = (double)halfT;
    }
}

// ============================================================
//  getHorizon
//   evaluate() が使う T と同じロジックで計算したホライゾンを返す。
//   変異オペレータが σ = T/8 の計算に使用する。
// ============================================================
int RCPSP_Problem_MaxShift::getHorizon() const {
    int T = 0;
    for (int d : instance.duration) T += d;
    if (!instance.capacity_t.empty() && !instance.capacity_t[0].empty()) {
        int tvT = static_cast<int>(instance.capacity_t[0].size());
        if (tvT > T) T = tvT;
    }
    return (T > 0) ? T : 1;
}

// ============================================================
//  evaluate
//
//  エンコーディング:
//    vars[0..n-1]  : 活動リスト
//    vars[n..2n-1] : max_shift リスト（直接読み取る）
//
//  スケジューリング:
//    全活動について [s_j^mak, s_j^mak + max_shift_j] の範囲で
//    コスト最小（同コストなら最早）の連続開始時刻に配置する。
// ============================================================
void RCPSP_Problem_MaxShift::evaluate(Solution *solution) {
    ++evalCounter_;

    const int n    = getNumJobs();
    const int nRes = instance.nRes;

    Variable **vars = solution->getDecisionVariables();

    // ---- 1. 活動リスト取得 & 先行制約修復 ----
    std::vector<int> seq(n);
    for (int i = 0; i < n; ++i) seq[i] = (int)vars[i]->getValue();

    if (!checkTopological(seq)) {
        seq = topoRepair(seq, instance.successors, n);
        for (int i = 0; i < n; ++i) vars[i]->setValue((double)seq[i]);
    }

    // ---- 2. ホライゾン T ----
    const int T     = getHorizon();
    const int halfT = getEffectiveHalfT();  // strategy 依存の上限 (0/T/8/T/4/T/2)

    // ---- 3. max_shift 取得 ----
    //   outputMaxShift_ >= 0 のとき: 出力用固定値（感度分析・再評価など）
    //   それ以外            : vars[n+j] から直接読み取り [0, halfT] にクリップ
    std::vector<int> maxShift(n, 0);
    if (outputMaxShift_ >= 0) {
        std::fill(maxShift.begin(), maxShift.end(), outputMaxShift_);
    } else {
        for (int j = 0; j < n; ++j) {
            int v = (int)vars[n + j]->getValue();
            maxShift[j] = std::max(0, std::min(halfT, v));
        }
    }
    // ダミー端点は常に最早配置
    if (n > 0) maxShift[0]     = 0;
    if (n > 1) maxShift[n - 1] = 0;

    // ---- 4. 先行リスト構築 ----
    std::vector<std::vector<int>> preds(n);
    for (int j = 0; j < n; ++j)
        for (int s : instance.successors[j])
            if (s >= 0 && s < n) preds[s].push_back(j);

    // ---- 5. スケジューリング ----
    std::vector<std::vector<int>> usage(nRes, std::vector<int>(T, 0));
    std::vector<int> finish(n, 0);
    std::vector<int> startArr(n, 0);

    // canPlace: ジョブ j を時刻 t から d_j スロット連続配置可能か
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

    // doPlace: usage テーブルを更新して配置
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

        if (d <= 0) {
            startArr[j] = 0;
            finish[j]   = 0;
            continue;
        }

        // EST = 先行ジョブの最大完了時刻
        int est = 0;
        for (int p : preds[j]) est = std::max(est, finish[p]);

        // s_j^mak: EST 以降で最初に連続配置可能な時刻
        int t_mak = est;
        while (t_mak < T && !canPlace(j, t_mak)) ++t_mak;

        if (t_mak >= T) {
            // ホライゾン末尾から後退探索（フォールバック）
            t_mak = std::max(0, T - d);
            while (t_mak >= 0 && !canPlace(j, t_mak)) --t_mak;
            if (t_mak < 0) {
                solution->setObjective(0, 1e9);
                solution->setObjective(1, 1e9);
                return;
            }
        }

        // max_shift[j] == 0: 純粋最早配置（コスト探索スキップ）
        // max_shift[j] >  0: [t_mak, t_mak + maxShift[j]] でコスト最小・左詰め配置
        int    placedAt = t_mak;
        double bestCost = computeJobCostAt(j, t_mak, T);

        if (maxShift[j] > 0) {
            int latest = std::min(T - d, t_mak + maxShift[j]);
            for (int t = t_mak + 1; t <= latest; ++t) {
                if (canPlace(j, t)) {
                    double c = computeJobCostAt(j, t, T);
                    if (c < bestCost) {     // strict '<' → 左詰め保証
                        bestCost = c;
                        placedAt = t;
                    }
                }
            }
        }

        doPlace(j, placedAt);
        totalCost += bestCost;
    }

    // ---- 6. makespan ----
    int makespan = 0;
    for (int j = 0; j < n; ++j) makespan = std::max(makespan, finish[j]);

    // ---- 7. キャッシュ・execSlots 更新 ----
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
//  buildRandomTopo (内部ヘルパー)
//  ランダムトポロジカルソートを生成して vars[0..n-1] に書き込む
// ============================================================
static void buildRandomTopo(int n, const std::vector<std::vector<int>> &successors,
                             Variable **vars, std::mt19937 &rng)
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
            if (s >= 0 && s < n && --indeg[s] == 0) avail.push_back(s);
    }
    if ((int)perm.size() != n) {
        perm.resize(n);
        std::iota(perm.begin(), perm.end(), 0);
    }
    for (int i = 0; i < n; ++i) vars[i]->setValue((double)perm[i]);
}

// ============================================================
//  createMakespanExtremeSolution
//  全ジョブ max_shift = 0 → 最早配置（makespan 極端解）
// ============================================================
Solution* RCPSP_Problem_MaxShift::createMakespanExtremeSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();

    Solution *sol   = new Solution(this);
    Variable **vars = sol->getDecisionVariables();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopo(n, instance.successors, vars, rng);

    for (int j = 0; j < n; ++j) {
        int idx = n + j;
        if (idx >= nVars) break;
        vars[idx]->setValue(0.0);
    }
    return sol;
}

// ============================================================
//  createCostExtremeSolution
//  全ジョブ max_shift = T/4 → コスト最小方向への探索（cost 極端解）
// ============================================================
Solution* RCPSP_Problem_MaxShift::createCostExtremeSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();
    const int halfT = getEffectiveHalfT();  // strategy 依存の上限

    Solution *sol   = new Solution(this);
    Variable **vars = sol->getDecisionVariables();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopo(n, instance.successors, vars, rng);

    for (int j = 0; j < n; ++j) {
        int idx = n + j;
        if (idx >= nVars) break;
        vars[idx]->setValue((double)halfT);
    }
    // ダミー端点は 0
    if (n > 0 && n     < nVars) vars[n + 0]->setValue(0.0);
    if (n > 1 && n+n-1 < nVars) vars[n + n - 1]->setValue(0.0);
    return sol;
}

// ============================================================
//  createRandomTopoSolution
//
//  初期解生成:
//    活動リスト : Kahn ベースのランダムトポロジカルソート
//    max_shift  : clip( round( N(T/4, T/8) ), 0, T/2 )
//    ダミー端点 : max_shift = 0
// ============================================================
Solution* RCPSP_Problem_MaxShift::createRandomTopoSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();
    const int T     = getHorizon();
    const int halfT = getEffectiveHalfT();  // strategy 依存の上限

    Solution *sol = new Solution(this);
    Variable **vars = sol->getDecisionVariables();

    // ---- 活動リスト: ランダムトポロジカルソート ----
    static thread_local std::mt19937 msRng{std::random_device{}()};
    buildRandomTopo(n, instance.successors, vars, msRng);


    //
    //   解ごとに以下の確率でグループを選択する:
    //     30%: Extreme Makespan Priority  → 全ジョブ max_shift = 0
    //     40%: Moderate Makespan Priority → Uniform[0, max(1, T/100)]
    //     30%: Random                    → Uniform[0, T/4]（従来通り）
    //
    //   初期集団にメイクスパン最小解とその周辺解を確実に含めることで、
    //   NSGA-II の非支配ソートで makespan 側の解が淘汰されにくくなる。
    {
        std::uniform_real_distribution<double> prob01(0.0, 1.0);
        double r = prob01(msRng);

        std::uniform_int_distribution<int> ms_dist(0, halfT);  // Random (r >= 0.70)

        const int smallUpper = std::max(1, T / 100);
        std::uniform_int_distribution<int> ms_small(0, smallUpper);  // Moderate

        for (int j = 0; j < n; ++j) {
            int idx = n + j;
            if (idx >= nVars) break;

            if (r < 0.30) {
                vars[idx]->setValue(0.0);
            } else if (r < 0.70) {
                vars[idx]->setValue((double)ms_small(msRng));
            } else {
                vars[idx]->setValue((double)ms_dist(msRng));
            }
        }
    }

    // ダミー端点は常に 0
    if (n > 0 && n     < nVars) vars[n + 0]->setValue(0.0);
    if (n > 1 && n+n-1 < nVars) vars[n + n - 1]->setValue(0.0);

    return sol;
}
