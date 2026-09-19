#include "RCPSP_Problem_MaxShiftDur.h"
#include "Solution.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

// ============================================================
//  RCPSP_Problem_MaxShift::verifySchedule
//
//  RCPSP_Problem_MaxShift.h で宣言されているが .cpp に実装がないため
//  ここで提供する（C++ では任意の翻訳単位にメンバ関数を定義できる）。
//
//  evaluate() が生成したスケジュール（solution->startTimes_）が
//  先行制約・資源制約を満たすか検証する。
//  違反があれば標準エラーに詳細を出力して false を返す。
// ============================================================
bool RCPSP_Problem_MaxShift::verifySchedule(Solution *solution) const {
    const int n    = getNumJobs();
    const int nRes = instance.nRes;

    if ((int)solution->startTimes_.size() < n) {
        std::cerr << "[verifySchedule] startTimes_ のサイズが不足 ("
                  << solution->startTimes_.size() << " < " << n << ")\n";
        return false;
    }

    const auto &start = solution->startTimes_;
    const auto &dur   = instance.duration;
    bool ok = true;

    // ---- 先行制約チェック ----
    for (int i = 0; i < n; ++i) {
        for (int s : instance.successors[i]) {
            if (s < 0 || s >= n) continue;
            int fi = start[i] + dur[i];
            if (fi > start[s]) {
                std::cerr << "[verifySchedule] 先行制約違反: job=" << i
                          << " finish=" << fi << " > job=" << s
                          << " start=" << start[s] << "\n";
                ok = false;
            }
        }
    }

    // ---- 資源制約チェック ----
    const int T = getHorizon();
    std::vector<std::vector<int>> usage(nRes, std::vector<int>(T, 0));

    for (int j = 0; j < n; ++j) {
        int d = dur[j];
        if (d <= 0) continue;
        int t0 = start[j];
        for (int tau = t0; tau < t0 + d && tau < T; ++tau)
            for (int k = 0; k < nRes; ++k)
                usage[k][tau] += instance.demand[j][k];
    }

    for (int tau = 0; tau < T; ++tau) {
        for (int k = 0; k < nRes; ++k) {
            int cap = capacityAtTime(k, tau);
            if (usage[k][tau] > cap) {
                std::cerr << "[verifySchedule] 資源制約違反: t=" << tau
                          << " k=" << k << " usage=" << usage[k][tau]
                          << " cap=" << cap << "\n";
                ok = false;
            }
        }
    }

    return ok;
}

// ============================================================
//  getAlpha
// ============================================================
double RCPSP_Problem_MaxShiftDur::getAlpha() const {
    switch (strategy_) {
        case 1: return 0.0;   // EST 固定（makespan 専用）
        case 2: return 0.5;   // 軽微コスト探索
        case 3: return 1.0;   // 中程度（デフォルト）
        case 4: return 1.5;   // 積極的コスト探索（原典値）
        default: return 1.0;
    }
}

// ============================================================
//  setStrategy
//   strategy_ と alpha_ を更新する。
//   vars[n..2n-1] の上限は常に R のまま（変更不要）。
// ============================================================
void RCPSP_Problem_MaxShiftDur::setStrategy(int s) {
    strategy_ = s;
    alpha_    = getAlpha();
    // upperLimit_[n..2n-1] は常に R。基底クラスのように halfT で上書きしない。
}

// ============================================================
//  コンストラクタ
//   基底クラス RCPSP_Problem_MaxShift のコンストラクタを呼んだあと、
//   vars[n..2n-1] の上限を R に上書き（基底が halfT を設定してしまうため）。
// ============================================================
RCPSP_Problem_MaxShiftDur::RCPSP_Problem_MaxShiftDur(
        const std::string &filename, int strategy, double rr, bool rv)
    : RCPSP_Problem_MaxShift(filename, strategy, rr, rv)
{
    alpha_ = getAlpha();

    const int n = getNumJobs();
    for (int i = n; i < 2 * n; ++i)
        upperLimit_[i] = (double)R;
}

// ============================================================
//  buildRandomTopoDur（ファイルスコープ内部ヘルパー）
//  Kahn ベースのランダムトポロジカルソートを vars[0..n-1] に書き込む。
//  RCPSP_Problem_MaxShift.cpp 内の buildRandomTopo と同一ロジック。
// ============================================================
static void buildRandomTopoDur(
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
//  エンコーディング:
//    vars[0..n-1]  : 活動リスト
//    vars[n..2n-1] : 正規化遅延比率キー [0, R]
//
//  スケジューリング（P1 serial SGS）:
//    各活動 j について:
//      rho_j  = vars[n+j] / (double)R              [0, 1]
//      eff_j  = max(alpha * d_j,  beta * T)
//      win_j  = round(rho_j * eff_j)               整数
//      [t_mak, min(T - d_j, t_mak + win_j)] でコスト最小・左詰め配置
// ============================================================
void RCPSP_Problem_MaxShiftDur::evaluate(Solution *solution) {
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

    // ---- 3. 遅延窓幅の計算（duration-scaled）----
    std::vector<int> winArr(n, 0);
    for (int j = 0; j < n; ++j) {
        int    key = vars[n + j];
        double rho = std::max(0, std::min(R, key)) / (double)R;
        double dj  = (double)instance.duration[j];
        double eff = std::max(alpha * dj, beta * (double)T);
        winArr[j]  = (int)std::lround(rho * eff);
    }
    // ダミー端点は常に最早配置（d_j=0 で自動的に 0 になるが明示的にガード）
    if (n > 0) winArr[0]     = 0;
    if (n > 1) winArr[n - 1] = 0;

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

        // EST: 先行ジョブの最大完了時刻
        int est = 0;
        for (int p : preds[j]) est = std::max(est, finish[p]);

        if (d <= 0) {
            // ダミー端点（d=0）は配置不要だが、
            // startTimes_ に先行制約を反映した EST を記録する（verifySchedule 用）
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

        // win_j == 0: 最早配置（コスト探索スキップ）
        // win_j >  0: [t_mak, t_mak + win_j] でコスト最小・左詰め配置
        int    placedAt = t_mak;
        double bestCost = computeJobCostAt(j, t_mak, T);

        if (winArr[j] > 0) {
            int latest = std::min(T - d, t_mak + winArr[j]);
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

#ifndef NDEBUG
    // ---- デバッグビルド限定: 先行制約の自己検証（.miss_memory/029 再発防止）----
    for (int a = 0; a < n; ++a) {
        if (instance.duration[a] <= 0) continue;
        for (int s : instance.successors[a]) {
            if (s < 0 || s >= n || instance.duration[s] <= 0) continue;
            if (finish[a] > startArr[s]) {
                std::cerr << "[RCPSP_Problem_MaxShiftDur::evaluate][ASSERT] precedence violation: "
                          << "job=" << a << " finish=" << finish[a]
                          << " > job=" << s << " start=" << startArr[s] << "\n";
            }
        }
    }
#endif

    // ---- 6. makespan ----
    int makespan = 0;
    for (int j = 0; j < n; ++j) makespan = std::max(makespan, finish[j]);

    // ---- 7. キャッシュ更新（既存 MaxShift と同一作法）----
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
//  全ジョブ rho = 0 → win = 0 → 最早配置（makespan 極端解）
// ============================================================
Solution* RCPSP_Problem_MaxShiftDur::createMakespanExtremeSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();

    Solution *sol = new Solution(this);
    auto &vars = sol->getVars();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopoDur(n, instance.successors, vars, rng);

    for (int j = 0; j < n; ++j) {
        int idx = n + j;
        if (idx >= nVars) break;
        vars[idx] = 0;      // rho = 0 → win = 0 → EST 配置
    }
    return sol;
}

// ============================================================
//  createCostExtremeSolution
//  全ジョブ rho = 1.0 (key=R) → 最大窓でコスト探索（cost 極端解）
//  ダミー端点は 0 に固定。
// ============================================================
Solution* RCPSP_Problem_MaxShiftDur::createCostExtremeSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();

    Solution *sol = new Solution(this);
    auto &vars = sol->getVars();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopoDur(n, instance.successors, vars, rng);

    for (int j = 0; j < n; ++j) {
        int idx = n + j;
        if (idx >= nVars) break;
        vars[idx] = R;      // rho = 1.0 → 最大窓
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
//    rho キー  : グループ分け（既存 MaxShift の思想を比率空間で踏襲）
    //      20%: 全 rho = 0          (makespan 極端; key=0)
    //      40%: rho ∈ Uniform[0, R/8]   (中庸; 比率空間の 12.5%)
    //      40%: rho ∈ Uniform[0, R]     (完全ランダム; 全比率空間)
//    ダミー端点は常に key=0 に固定。
// ============================================================
Solution* RCPSP_Problem_MaxShiftDur::createRandomTopoSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();

    Solution *sol = new Solution(this);
    auto &vars = sol->getVars();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopoDur(n, instance.successors, vars, rng);

    {
        std::uniform_real_distribution<double> prob01(0.0, 1.0);
        double r = prob01(rng);

        // smallUpper: R/8 = 125 (全比率空間の 12.5%)
        const int smallUpper = std::max(1, R / 8);
        std::uniform_int_distribution<int> ms_small(0, smallUpper);
        std::uniform_int_distribution<int> ms_full(0, R);

        for (int j = 0; j < n; ++j) {
            int idx = n + j;
            if (idx >= nVars) break;

            if (r < 0.20) {
                vars[idx] = 0;              // makespan 極端
            } else if (r < 0.60) {
                vars[idx] = ms_small(rng);  // 中庸（R/8 窓）
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
