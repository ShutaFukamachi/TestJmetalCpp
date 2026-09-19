#include "RCPSP_Problem_MaxShift.h"
#include "Solution.h"

#include <algorithm>
#include <climits>
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

    auto &vars = solution->getVars();

    // ---- 1. 活動リスト取得 & 先行制約修復 ----
    std::vector<int> seq(n);
    for (int i = 0; i < n; ++i) seq[i] = vars[i];

    if (!checkTopological(seq)) {
        seq = topoRepair(seq, instance.successors, n);
        for (int i = 0; i < n; ++i) vars[i] = seq[i];
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
            int v = vars[n + j];
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

    // ============================================================
    //  Parallel SGS（本命A: 生成スキーム変更）
    //   時刻 t を決定点（＝配置済みジョブの完了時刻）に沿って進め、
    //   各 t で eligible（全先行が完了）なジョブを活動リスト位置＝優先度
    //   の昇順に配置する。コストは窓 [t, t+maxShift_j] 内の最安スロットで保持。
    //   置けない（資源待ち）ジョブは次の決定点に持ち越す＝non-delay schedule。
    //   Serial（下の else）は活動リスト順にジョブ単位で EST 配置＝active schedule。
    // ============================================================
    if (parallelSGS_) {
        // 活動リスト位置＝優先度（小さいほど高優先）
        std::vector<int> prio(n, 0);
        for (int i = 0; i < n; ++i) prio[seq[i]] = i;

        std::vector<char> placed(n, false);
        int remaining = n;

        int t = 0;
        int guard = 0;
        const int guardMax = 4 * (n + T) + 16;   // 無限ループ保険

        while (remaining > 0 && t < T && guard++ < guardMax) {
            // ---- eligible 集合: 未配置 && 全先行が配置済みかつ finish<=t ----
            std::vector<int> elig;
            for (int j = 0; j < n; ++j) {
                if (placed[j]) continue;
                bool ready = true;
                int est = 0;
                for (int p : preds[j]) {
                    if (!placed[p]) { ready = false; break; }
                    est = std::max(est, finish[p]);
                }
                if (!ready || est > t) continue;   // 先行未完 or まだ開始不可
                elig.push_back(j);
            }
            // 優先度（活動リスト位置）昇順で処理
            std::sort(elig.begin(), elig.end(),
                      [&](int a, int b) { return prio[a] < prio[b]; });

            for (int j : elig) {
                int d = instance.duration[j];
                if (d <= 0) {
                    // ダミー: 先行完了時刻に配置（makespan には無影響）
                    int est = 0;
                    for (int p : preds[j]) est = std::max(est, finish[p]);
                    startArr[j] = est; finish[j] = est;
                    placed[j] = true; --remaining;
                    continue;
                }
                // コスト窓 [t, t+maxShift[j]] 内の最安の実行可能スロット
                int latest = std::min(T - d, t + maxShift[j]);
                int    placedAt = -1;
                double bestCost = 0.0;
                for (int tt = t; tt <= latest; ++tt) {
                    if (!canPlace(j, tt)) continue;
                    double c = computeJobCostAt(j, tt, T);
                    if (placedAt < 0 || c < bestCost - 1e-9) {
                        bestCost = c; placedAt = tt;
                    }
                }
                if (placedAt >= 0) {
                    doPlace(j, placedAt);
                    totalCost += bestCost;
                    placed[j] = true; --remaining;
                }
                // 窓内で置けない → この決定点では見送り（次の決定点で再考）
            }

            // ---- 次の決定点へ: t より後の最小 finish（無ければ t+1）----
            int nt = INT_MAX;
            for (int j = 0; j < n; ++j)
                if (placed[j] && finish[j] > t) nt = std::min(nt, finish[j]);
            t = (nt == INT_MAX) ? t + 1 : nt;
        }

        if (remaining > 0) {   // 全ジョブ配置できず（実行不能）
            solution->setObjective(0, 1e9);
            solution->setObjective(1, 1e9);
            return;
        }
    } else {

    for (int pos = 0; pos < n; ++pos) {
        int j = seq[pos];
        int d = instance.duration[j];

        // EST = 先行ジョブの最大完了時刻
        int est = 0;
        for (int p : preds[j]) est = std::max(est, finish[p]);

        if (d <= 0) {
            startArr[j] = est;
            finish[j]   = est;
            continue;
        }

        // s_j^mak: EST 以降で最初に連続配置可能な時刻
        int t_mak = est;
        while (t_mak < T && !canPlace(j, t_mak)) ++t_mak;

        if (t_mak >= T) {
            // [est, T-d] 内に置けない → est を下回る配置は先行制約違反になるため、
            // 後退探索も est までに限定する（.miss_memory/022, 029 参照）。
            // 見つからなければそのスケジュールは実行不能として扱う。
            t_mak = T - d;
            while (t_mak >= est && !canPlace(j, t_mak)) --t_mak;
            if (t_mak < est) {
                // startTimes_/execSlots_ を必ず n 要素に確定させてから返す。
                // 未設定（前世代からの古いサイズ・空のまま）だと、この Solution を
                // 後段が「評価済み」として読む際に不整合を招きうるため、
                // 現時点までの部分的な配置結果で確実に上書きする（.miss_memory/029）。
                solution->startTimes_ = startArr;
                solution->execSlots_.assign(n, {});
                solution->setObjective(0, 1e9);
                solution->setObjective(1, 1e9);
                return;
            }
        }

        // B8: 実行区間 [t, t+d) の残容量合計（大きいほど後続の自由度が高い）
        //   ※ usage は j 配置前の状態。全資源・全実行スロットの残りを合算する。
        auto residualScore = [&](int t) -> long long {
            long long s = 0;
            for (int tau = t; tau < t + d; ++tau)
                for (int k = 0; k < nRes; ++k)
                    s += (long long)(capacityAtTime(k, tau)
                                     - usage[k][tau] - instance.demand[j][k]);
            return s;
        };

        // max_shift[j] == 0: 純粋最早配置（コスト探索スキップ）
        // max_shift[j] >  0: [t_mak, t_mak + maxShift[j]] でコスト最小配置
        //   同コストの tie-break: 既定は「最早（左詰め）」。
        //   residualTieBreak_ ON のときは「残容量最大（B8: 資源平準化）」。
        int    placedAt = t_mak;
        double bestCost = computeJobCostAt(j, t_mak, T);
        long long bestResidual = residualTieBreak_ ? residualScore(t_mak) : 0;

        if (maxShift[j] > 0) {
            int latest = std::min(T - d, t_mak + maxShift[j]);
            for (int t = t_mak + 1; t <= latest; ++t) {
                if (!canPlace(j, t)) continue;
                double c = computeJobCostAt(j, t, T);
                if (c < bestCost - 1e-9) {          // より安い → 無条件採用
                    bestCost = c;
                    placedAt = t;
                    if (residualTieBreak_) bestResidual = residualScore(t);
                } else if (residualTieBreak_ && c < bestCost + 1e-9) {
                    // 同コスト → 残容量が大きい方を採用（後続の自由度を残す）
                    long long r = residualScore(t);
                    if (r > bestResidual) { placedAt = t; bestResidual = r; }
                }
                // residualTieBreak_ OFF かつ同コストは何もしない＝現行の最早維持
            }
        }

        doPlace(j, placedAt);
        totalCost += bestCost;
    }
    }  // end else (Serial SGS)

#ifndef NDEBUG
    // ---- デバッグビルド限定: 先行制約の自己検証（.miss_memory/029 再発防止）----
    for (int a = 0; a < n; ++a) {
        if (instance.duration[a] <= 0) continue;
        for (int s : instance.successors[a]) {
            if (s < 0 || s >= n || instance.duration[s] <= 0) continue;
            if (finish[a] > startArr[s]) {
                std::cerr << "[RCPSP_Problem_MaxShift::evaluate][ASSERT] precedence violation: "
                          << "job=" << a << " finish=" << finish[a]
                          << " > job=" << s << " start=" << startArr[s] << "\n";
            }
        }
    }
#endif

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
                             std::vector<int> &vars, std::mt19937 &rng)
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
    for (int i = 0; i < n; ++i) vars[i] = perm[i];
}

// ============================================================
//  createMakespanExtremeSolution
//  全ジョブ max_shift = 0 → 最早配置（makespan 極端解）
// ============================================================
Solution* RCPSP_Problem_MaxShift::createMakespanExtremeSolution() {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();

    Solution *sol   = new Solution(this);
    auto &vars = sol->getVars();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopo(n, instance.successors, vars, rng);

    for (int j = 0; j < n; ++j) {
        int idx = n + j;
        if (idx >= nVars) break;
        vars[idx] = 0;
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
    auto &vars = sol->getVars();

    static thread_local std::mt19937 rng{std::random_device{}()};
    buildRandomTopo(n, instance.successors, vars, rng);

    for (int j = 0; j < n; ++j) {
        int idx = n + j;
        if (idx >= nVars) break;
        vars[idx] = halfT;
    }
    // ダミー端点は 0
    if (n > 0 && n     < nVars) vars[n + 0] = 0;
    if (n > 1 && n+n-1 < nVars) vars[n + n - 1] = 0;
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
    auto &vars = sol->getVars();

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
                vars[idx] = 0;
            } else if (r < 0.70) {
                vars[idx] = ms_small(msRng);
            } else {
                vars[idx] = ms_dist(msRng);
            }
        }
    }

    // ダミー端点は常に 0
    if (n > 0 && n     < nVars) vars[n + 0] = 0;
    if (n > 1 && n+n-1 < nVars) vars[n + n - 1] = 0;

    return sol;
}

// ============================================================
//  createPriorityRuleSolution (A1: priority-rule シード)
// ============================================================
Solution* RCPSP_Problem_MaxShift::createPriorityRuleSolution(int rule) {
    const int n     = getNumJobs();
    const int nVars = getNumberOfVariables();
    const auto &succ = instance.successors;
    const auto &dur  = instance.duration;

    // ---- 先行リスト (predecessors) と入次数 ----
    std::vector<std::vector<int>> preds(n);
    std::vector<int> indeg(n, 0);
    for (int j = 0; j < n; ++j)
        for (int s : succ[j])
            if (s >= 0 && s < n) { preds[s].push_back(j); ++indeg[s]; }

    // ---- トポロジカル順序 (Kahn, 決定論的) ----
    std::vector<int> topo;
    topo.reserve(n);
    {
        std::vector<int> deg = indeg;
        std::vector<int> stack;
        for (int j = 0; j < n; ++j) if (deg[j] == 0) stack.push_back(j);
        while (!stack.empty()) {
            int j = stack.back(); stack.pop_back();
            topo.push_back(j);
            for (int s : succ[j])
                if (s >= 0 && s < n && --deg[s] == 0) stack.push_back(s);
        }
        if ((int)topo.size() != n) { topo.resize(n); std::iota(topo.begin(), topo.end(), 0); }
    }

    // ---- 各ジョブの優先度スコアを計算（高いほど先に選ぶ）----
    std::vector<long long> score(n, 0);
    if (rule == 0) {
        // LFT: CPM で最遅完了時刻を求め、-LFT をスコアにする（LFT 小 = 優先）
        std::vector<int> EF(n, 0), ES(n, 0);
        for (int j : topo) {             // forward pass
            int es = 0;
            for (int p : preds[j]) es = std::max(es, EF[p]);
            ES[j] = es; EF[j] = es + dur[j];
        }
        int CP = 0;
        for (int j = 0; j < n; ++j) CP = std::max(CP, EF[j]);
        std::vector<int> LF(n, CP);
        for (auto it = topo.rbegin(); it != topo.rend(); ++it) {  // backward pass
            int j = *it;
            int lf = CP;
            bool hasSucc = false;
            for (int s : succ[j]) if (s >= 0 && s < n) {
                hasSucc = true;
                lf = std::min(lf, LF[s] - dur[s]);  // LS[s]
            }
            LF[j] = hasSucc ? lf : CP;
        }
        for (int j = 0; j < n; ++j) score[j] = -(long long)LF[j];
    } else if (rule == 1) {
        // MTS: 推移的後続数（多いほど優先）
        for (int j = 0; j < n; ++j) {
            std::vector<char> seen(n, 0);
            std::vector<int> st = {j};
            long long cnt = 0;
            while (!st.empty()) {
                int u = st.back(); st.pop_back();
                for (int s : succ[u]) if (s >= 0 && s < n && !seen[s]) {
                    seen[s] = 1; ++cnt; st.push_back(s);
                }
            }
            score[j] = cnt;
        }
    } else {
        // GRPW: d_j + Σ d_succ（大きいほど優先）
        for (int j = 0; j < n; ++j) {
            long long w = dur[j];
            for (int s : succ[j]) if (s >= 0 && s < n) w += dur[s];
            score[j] = w;
        }
    }

    // ---- list-scheduling: eligible からスコア最良を選ぶ（同点は乱数）----
    static thread_local std::mt19937 prRng{std::random_device{}()};
    std::vector<int> deg = indeg;
    std::vector<int> avail;
    avail.reserve(n);
    for (int j = 0; j < n; ++j) if (deg[j] == 0) avail.push_back(j);

    std::vector<int> perm;
    perm.reserve(n);
    while (!avail.empty()) {
        long long best = LLONG_MIN;
        for (int j : avail) best = std::max(best, score[j]);
        // 最良スコアの候補を集めて乱数で1つ選ぶ
        std::vector<int> tiePos;
        for (int k = 0; k < (int)avail.size(); ++k)
            if (score[avail[k]] == best) tiePos.push_back(k);
        std::uniform_int_distribution<int> dis(0, (int)tiePos.size() - 1);
        int pick = tiePos[dis(prRng)];
        int job  = avail[pick];
        avail[pick] = avail.back();
        avail.pop_back();
        perm.push_back(job);
        for (int s : succ[job])
            if (s >= 0 && s < n && --deg[s] == 0) avail.push_back(s);
    }
    if ((int)perm.size() != n) { perm.resize(n); std::iota(perm.begin(), perm.end(), 0); }

    // ---- Solution 構築: 活動リスト + max_shift=0（EST 配置）----
    Solution *sol = new Solution(this);
    auto &vars = sol->getVars();
    for (int i = 0; i < n; ++i) vars[i] = perm[i];
    for (int j = 0; j < n; ++j) {
        int idx = n + j;
        if (idx >= nVars) break;
        vars[idx] = 0;
    }
    return sol;
}