#include "RCPSP_Problem_Setup.h"
#include "Solution.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <limits>
#include <iostream>
#include <vector>
#include <queue>
#include <cassert>

// ============================================================
//  コンストラクタ
// ============================================================
RCPSP_Problem_Setup::RCPSP_Problem_Setup(
        const std::string    &filename,
        ActivitySplittingMode mode,
        SetupModel            setupModel,
        double                alphaS,
        int                   strategy,
        double                rr,
        bool                  rv)
    : RCPSP_Problem_Splitting(filename, mode, strategy, rr, rv)
    , setupModel_(setupModel)
    , alphaS_(alphaS)
{
    const char *mStr = (setupModel == SetupModel::TW) ? "TW"
                     : (setupModel == SetupModel::WD) ? "WD" : "WR";
    const char *pStr = (mode == ActivitySplittingMode::P2) ? "P2" : "P3";
    std::cout << "[RCPSP_Problem_Setup] mode=" << pStr
              << " setupModel=" << mStr
              << " alphaS=" << alphaS << "\n";
}

// ============================================================
//  computeSetupLen
//
//  cumulDone == 0（最初のパート）はセットアップ不要なので 0 を返す。
//  それ以外は論文の式に従って計算する。
//
//  TW: t_i^s = floor(alphaS / 2 * d_i)          — x に依存しない
//  WD: t_i^s = floor(alphaS * x)                  — 既遂量に比例
//  WR: t_i^s = floor(alphaS * (d_i + 1 - x))     — 残余量に比例
// ============================================================
int RCPSP_Problem_Setup::computeSetupLen(int j, int cumulDone) const
{
    if (cumulDone <= 0) return 0;  // 最初のパートはセットアップなし

    int d = instance.duration[j];
    switch (setupModel_) {
        case SetupModel::TW:
            return static_cast<int>(alphaS_ / 2.0 * d);
        case SetupModel::WD:
            return static_cast<int>(alphaS_ * cumulDone);
        case SetupModel::WR:
            return static_cast<int>(alphaS_ * (d + 1 - cumulDone));
    }
    return 0;
}

// ============================================================
//  applySetupSlots
//
//  t_start 以降で setupLen 枠の「連続した資源確保可能なスロット」を
//  探し、確保できたら usage を更新してセットアップ終了時刻を返す。
//
//  "資源確保可能" = usage[k][ts] + demand[j][k] <= capacityAtTime(k,ts)
//
//  スキャン: t_start から始め、連続 setupLen が取れる最初の窓を採用。
//  窓の途中で不可スロットがあれば、その次から再スキャン（T-max法）。
//
//  戻り値:
//   >= 0 : セットアップ終了直後の時刻（ここから実行再開）
//   -1   : T 内で確保不能
// ============================================================
int RCPSP_Problem_Setup::applySetupSlots(
        int j, int t_start, int setupLen, int T,
        std::vector<std::vector<int>> &usage,
        std::vector<int>              &outSlots,
        double                        &outCost) const
{
    if (setupLen <= 0) return t_start;   // セットアップ不要

    const auto &inst = instance;
    int nRes = inst.nRes;

    // setupLen 枠の連続窓を T-max 法で探す
    int winStart = t_start;
    while (winStart + setupLen <= T) {
        bool ok = true;
        for (int ts = winStart; ts < winStart + setupLen && ok; ++ts) {
            for (int k = 0; k < nRes && ok; ++k) {
                if (usage[k][ts] + inst.demand[j][k] > capacityAtTime(k, ts))
                    ok = false;
            }
            if (!ok) {
                // ts が不可 → ts+1 から再スキャン
                winStart = ts + 1;
                break;
            }
        }
        if (ok) {
            // 窓を確保
            for (int ts = winStart; ts < winStart + setupLen; ++ts) {
                for (int k = 0; k < nRes; ++k)
                    usage[k][ts] += inst.demand[j][k];
                outSlots.push_back(ts);
                outCost += computeSlotCost(j, ts, T);
            }
            return winStart + setupLen;   // 実行再開時刻
        }
    }
    return -1;  // 確保不能
}

// ============================================================
//  executeP2WithSetup
//
//  P2 スキームにセットアップ時間を組み込んだ配置関数。
//
//  ■ 状態マシンの概要
//
//  [通常実行中]
//    γ=1 かつ残余OK → 実行スロット確定、currentBlock に追加
//    γ=1 かつ残余NG → P2 分割ではない（他ジョブ競合）
//                     → currentBlock をロールバック、リスタート
//    γ=0            → RR/RV 起因の P2 分割
//                     → currentBlock を "コミット"、inP2Gap=true
//
//  [ギャップ後（inP2Gap=true）、次の γ=1 スロットが来たとき]
//    → セットアップスロットを確保（applySetupSlots）
//    → inP2Gap=false、currentBlock リセット
//    → 実行再開
//
//  ■ ロールバック対象
//    currentBlock の execSlots + setupSlots のみ。
//    コミット済みブロックはロールバックしない。
//
//  ■ infeasible 処理
//    セットアップが T 内に収まらない、または d 枠を確保できない場合、
//    コミット済みスロット含めすべて usage をロールバックして feasible=false を返す。
// ============================================================
RCPSP_Problem_Setup::SetupP2Result
RCPSP_Problem_Setup::executeP2WithSetup(
        int j, int S_j, int T,
        std::vector<std::vector<int>> &usage) const
{
    const auto &inst = instance;
    int d    = inst.duration[j];
    int nRes = inst.nRes;

    // ---- コミット済み情報 ----
    int    cumulDone  = 0;    // コミット済み実行スロット数
    double totalCost  = 0.0;  // コミット済みコスト
    std::vector<int> allExec, allSetup;
    int firstExec = -1, lastExec = -1;

    // ---- 現在ブロックの状態 ----
    std::vector<int> blkExec, blkSetup;
    double blkCost    = 0.0;
    int    blkOrigin  = S_j;   // このブロックの開始原点（ロールバック基点）
    bool   blkHasSetup = false; // このブロックにセットアップ適用済みか

    // ---- ギャップ状態 ----
    bool inP2Gap = false;  // γ=0 分割後でセットアップ待ち

    // ---- メインループ ----
    int t = S_j;
    while (cumulDone + (int)blkExec.size() < d && t < T) {

        // ── γ チェック（U_kt < demand → RR/RV 不足 → P2 分割許容） ──
        bool gammaOk = true;
        for (int k = 0; k < nRes && gammaOk; ++k)
            if (capacityAtTime(k, t) < inst.demand[j][k]) gammaOk = false;

        if (!gammaOk) {
            // γ=0: P2 分割
            if (!blkExec.empty()) {
                // 現ブロックをコミット
                allExec.insert(allExec.end(), blkExec.begin(), blkExec.end());
                allSetup.insert(allSetup.end(), blkSetup.begin(), blkSetup.end());
                totalCost  += blkCost;
                cumulDone  += (int)blkExec.size();
                lastExec    = blkExec.back();
                blkExec.clear(); blkSetup.clear(); blkCost = 0.0; blkHasSetup = false;
            } else if (blkHasSetup) {
                // セットアップ適用済みだが実行前にまたγ=0
                // → セットアップをロールバックして再度ギャップに入る
                for (int s : blkSetup)
                    for (int k = 0; k < nRes; ++k)
                        usage[k][s] -= inst.demand[j][k];
                blkSetup.clear(); blkCost = 0.0; blkHasSetup = false;
            }
            inP2Gap = true;
            ++t;
            continue;
        }

        // ── γ=1: ギャップ後の最初の実行可能候補 → セットアップ挿入 ──
        if (inP2Gap && !blkHasSetup) {
            int setupLen = computeSetupLen(j, cumulDone);
            if (setupLen > 0) {
                double setupCost = 0.0;
                int resumeT = applySetupSlots(j, t, setupLen, T,
                                              usage, blkSetup, setupCost);
                if (resumeT < 0) {
                    // セットアップ不能 → infeasible
                    goto infeasible;
                }
                blkCost   += setupCost;
                blkOrigin  = t;          // セットアップ開始点がブロック原点
                t          = resumeT;
            } else {
                blkOrigin = t;
            }
            blkHasSetup = true;
            inP2Gap     = false;
            continue;   // t を再チェック（setup 後の t でγ再確認）
        }

        // ── γ=1: 残余容量チェック ──
        bool residualOk = true;
        for (int k = 0; k < nRes && residualOk; ++k)
            if (usage[k][t] + inst.demand[j][k] > capacityAtTime(k, t))
                residualOk = false;

        if (residualOk) {
            // 実行確定
            for (int k = 0; k < nRes; ++k) usage[k][t] += inst.demand[j][k];
            if (firstExec < 0) firstExec = t;
            blkExec.push_back(t);
            blkCost += computeSlotCost(j, t, T);
            ++t;
        } else {
            // γ=1 残余不足 → P2 分割ではない（ロールバック & リスタート）
            for (int s : blkExec)
                for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];
            for (int s : blkSetup)
                for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];

            // firstExec をコミット済みに戻す
            firstExec = allExec.empty() ? -1 : allExec.front();

            // リスタート位置: セットアップがあればその開始点の次、なければ exec 先頭の次
            int restartT = blkExec.empty() ? t : blkExec.front();
            if (blkHasSetup) restartT = blkOrigin;

            blkExec.clear(); blkSetup.clear(); blkCost = 0.0; blkHasSetup = false;

            // コミット済み作業がある場合はギャップ状態を維持（次ブロックもセットアップ必要）
            inP2Gap = (cumulDone > 0);

            t = restartT + 1;
        }

    } // end while

    // ---- 最終ブロックをコミット ----
    if (!blkExec.empty()) {
        allExec.insert(allExec.end(), blkExec.begin(), blkExec.end());
        allSetup.insert(allSetup.end(), blkSetup.begin(), blkSetup.end());
        totalCost += blkCost;
        cumulDone += (int)blkExec.size();
        lastExec   = blkExec.back();
    }

    if (cumulDone < d) goto infeasible;

    return { firstExec, lastExec, totalCost, true, allExec, allSetup };

infeasible:
    // コミット済みスロットも含めすべて usage をロールバック
    for (int s : allExec)   for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];
    for (int s : allSetup)  for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];
    // 現ブロック分もロールバック
    for (int s : blkExec)   for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];
    for (int s : blkSetup)  for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];
    return { -1, -1, 0.0, false, {}, {} };
}

// ============================================================
//  simulateP2WithSetup
//
//  executeP2WithSetup の読み取り専用版。
//  usage をコピーして executeP2WithSetup を呼び、コピーを破棄する。
//  schedObj=1 時の S_j コスト探索ループ内で使用する。
// ============================================================
RCPSP_Problem_Setup::SetupP2Result
RCPSP_Problem_Setup::simulateP2WithSetup(
        int j, int S_j, int T,
        const std::vector<std::vector<int>> &usage) const
{
    std::vector<std::vector<int>> uCopy = usage;
    return executeP2WithSetup(j, S_j, T, uCopy);
}

// ============================================================
//  evaluate() オーバーライド
//
//  基底クラス（RCPSP_Problem_Splitting）の evaluate() と同じ前処理
//  （トポロジカル修復・schedObj・T・maxShift）を行い、
//  配置ステップのみセットアップ時間込みのロジックに差し替える。
// ============================================================
void RCPSP_Problem_Setup::evaluate(Solution *solution)
{
    ++evalCounter_;

    const auto &inst = instance;
    int n    = getNumJobs();
    int nRes = inst.nRes;

    auto &vars = solution->getVars();
    int nVars  = solution->getNumberOfVariables();

    // ---- 活動リスト取得・トポロジカル修復（基底クラスと同じ処理） ----
    std::vector<int> seq(n);
    for (int i = 0; i < n; ++i) seq[i] = vars[i];

    if (!checkTopological(seq)) {
        std::vector<int> indeg(n, 0);
        for (int j = 0; j < n; ++j)
            for (int s : inst.successors[j])
                if (s >= 0 && s < n) ++indeg[s];

        std::vector<int> priority(n, n);
        for (int i = 0; i < n; ++i)
            if (seq[i] >= 0 && seq[i] < n) priority[seq[i]] = i;

        auto cmp = [&](int a, int b){ return priority[a] > priority[b]; };
        std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);
        for (int j = 0; j < n; ++j) if (indeg[j] == 0) pq.push(j);

        std::vector<int> repaired;
        repaired.reserve(n);
        while (!pq.empty()) {
            int j = pq.top(); pq.pop();
            repaired.push_back(j);
            for (int s : inst.successors[j])
                if (s >= 0 && s < n && --indeg[s] == 0) pq.push(s);
        }
        if ((int)repaired.size() == n) {
            seq = repaired;
            for (int i = 0; i < n; ++i) vars[i] = seq[i];
        }
    }

    // ---- schedObj ベクタ取得 ----
    static std::mt19937 localRng(99999);
    std::vector<int> schedObj(n, 0);
    if (nVars >= 2 * n) {
        for (int j = 0; j < n; ++j) {
            int v = vars[n + j];
            schedObj[j] = (v != 0) ? 1 : 0;
        }
    } else {
        std::bernoulli_distribution coin(0.5);
        for (int j = 0; j < n; ++j) schedObj[j] = coin(localRng) ? 1 : 0;
    }
    if (n > 0) schedObj[0]     = 0;
    if (n > 1) schedObj[n - 1] = 0;

    // ---- ホライゾン T ----
    int T = 0;
    for (int j = 0; j < n; ++j) T += inst.duration[j];
    if (!inst.capacity_t.empty() && !inst.capacity_t[0].empty()) {
        int tvT = (int)inst.capacity_t[0].size();
        if (tvT > T) T = tvT;
    }
    if (T <= 0) T = 1;

    // ---- maxShift ----
    std::vector<int> maxShift(n, 0);
    if (outputMaxShift_ >= 0) {
        std::fill(maxShift.begin(), maxShift.end(), outputMaxShift_);
    } else {
        maxShift = buildMaxShiftForEval(T);
    }
    if (n > 0) maxShift[0]     = 0;
    if (n > 1) maxShift[n - 1] = 0;

    // ---- 先行者リスト ----
    std::vector<std::vector<int>> preds(n);
    for (int j = 0; j < n; ++j)
        for (int s : inst.successors[j])
            if (s >= 0 && s < n) preds[s].push_back(j);

    // ---- 資源使用量テーブル・結果配列 ----
    std::vector<std::vector<int>> usage(nRes, std::vector<int>(T, 0));
    std::vector<int> start(n, 0), finish(n, 0);
    solution->execSlots_.assign(n, {});

    double totalCost = 0.0;
    ActivitySplittingMode mode = getSplittingMode();

    // ================================================================
    //  メインループ
    // ================================================================
    for (int pos = 0; pos < n; ++pos) {
        int j = seq[pos];
        int d = inst.duration[j];

        if (d <= 0) {
            start[j] = finish[j] = 0;
            continue;
        }

        // EST
        int est = 0;
        for (int p : preds[j]) est = std::max(est, finish[p]);

        // ============================================================
        //  P2 + セットアップ
        //
        //  RCPSP_Problem_Splitting の P2 と同一パターン:
        //    schedObj=0 / maxShift=0 → EST から即 executeP2WithSetup
        //    schedObj=1 / maxShift>0 → simulateP2WithSetup で S_j を探索し
        //                              最安コストの S_j で executeP2WithSetup
        // ============================================================
        if (mode == ActivitySplittingMode::P2) {

            SetupP2Result res;

            if (schedObj[j] == 0 || maxShift[j] == 0) {
                // makespan 優先: S_j = est から即実行
                res = executeP2WithSetup(j, est, T, usage);
            } else {
                // コスト優先: S_j を [est, est+maxShift] の範囲でドライランし最安を選択
                int    latest   = std::min(T - d, est + maxShift[j]);
                double bestCost = std::numeric_limits<double>::infinity();
                int    bestSj   = est;

                for (int sj = est; sj <= latest; ++sj) {
                    SetupP2Result sim = simulateP2WithSetup(j, sj, T, usage);
                    if (sim.feasible && sim.cost < bestCost) {
                        bestCost = sim.cost;
                        bestSj   = sj;
                    }
                }
                res = executeP2WithSetup(j, bestSj, T, usage);
                if (!res.feasible)
                    res = executeP2WithSetup(j, est, T, usage);  // フォールバック
            }

            if (!res.feasible) {
                solution->setObjective(0, 1e9);
                solution->setObjective(1, 1e9);
                return;
            }
            start[j]   = res.firstExec;
            finish[j]  = res.lastExec + 1;
            totalCost += res.cost;
            solution->execSlots_[j] = std::move(res.execSlots);

        }
        // ============================================================
        //  P3 + セットアップ
        // ============================================================
        else { // mode == P3

            if (schedObj[j] == 0 || maxShift[j] == 0) {
                // ----------------------------------------------------------------
                // P3 makespan 優先: グリーディ実行 + 任意ギャップ後にセットアップ挿入
                //
                // 状態管理:
                //   cumulDone3  : 確定した実行スロット数
                //   needSetup3  : 次の実行前にセットアップが必要
                // ----------------------------------------------------------------
                int    cumulDone3  = 0;
                bool   needSetup3  = false;
                int    firstE3     = -1, lastE3 = -1;
                double cost3       = 0.0;
                std::vector<int> p3Exec, p3Setup;

                int t3 = est;
                while (cumulDone3 < d && t3 < T) {
                    // 資源チェック（γ + 残余 両方）
                    bool canExec = true;
                    for (int k = 0; k < nRes && canExec; ++k)
                        if (usage[k][t3] + inst.demand[j][k] > capacityAtTime(k, t3))
                            canExec = false;

                    if (!canExec) {
                        if (cumulDone3 > 0) needSetup3 = true;
                        ++t3;
                        continue;
                    }

                    // 資源 OK かつセットアップ必要
                    if (needSetup3) {
                        int setupLen = computeSetupLen(j, cumulDone3);
                        if (setupLen > 0) {
                            double sCost = 0.0;
                            int resumeT = applySetupSlots(j, t3, setupLen, T,
                                                          usage, p3Setup, sCost);
                            if (resumeT < 0) {
                                solution->setObjective(0, 1e9);
                                solution->setObjective(1, 1e9);
                                return;
                            }
                            cost3 += sCost;
                            t3     = resumeT;
                        }
                        needSetup3 = false;
                        continue;   // resumeT で再チェック
                    }

                    // 実行
                    for (int k = 0; k < nRes; ++k) usage[k][t3] += inst.demand[j][k];
                    if (firstE3 < 0) firstE3 = t3;
                    lastE3 = t3;
                    cost3 += computeSlotCost(j, t3, T);
                    p3Exec.push_back(t3);
                    ++cumulDone3;
                    ++t3;
                }

                if (cumulDone3 < d) {
                    solution->setObjective(0, 1e9);
                    solution->setObjective(1, 1e9);
                    return;
                }
                start[j]   = firstE3;
                finish[j]  = lastE3 + 1;
                totalCost += cost3;
                solution->execSlots_[j] = std::move(p3Exec);

            } else {
                // ----------------------------------------------------------------
                // P3 cost 優先: 実行可能スロットから安い d_j 枚を選択し、
                //               ギャップ後処理でセットアップを挿入する。
                //
                //   RCPSP_Problem_Splitting の P3 cost パスと同一の候補収集・選択を行い、
                //   ギャップ検出時に applySetupSlots でセットアップを挿入する。
                //
                //   ギャップ幅 < setupLen の場合はシフトせず executeP2WithSetup に
                //   フォールバックする（resource feasibility を保証するため）。
                // ----------------------------------------------------------------
                int windowEnd = std::min(T - 1, est + maxShift[j] + d);

                // 実行可能スロット収集
                std::vector<std::pair<double, int>> candidates;
                candidates.reserve(windowEnd - est + 1);
                for (int t = est; t <= windowEnd; ++t) {
                    bool ok = true;
                    for (int k = 0; k < nRes && ok; ++k)
                        if (usage[k][t] + inst.demand[j][k] > capacityAtTime(k, t))
                            ok = false;
                    if (ok) candidates.push_back({ computeSlotCost(j, t, T), t });
                }

                bool doFallback = ((int)candidates.size() < d);

                if (!doFallback) {
                    // コスト昇順で d 枚選択 → 時刻昇順にソート
                    std::sort(candidates.begin(), candidates.end());
                    std::vector<int> selected;
                    selected.reserve(d);
                    for (int i = 0; i < d; ++i) selected.push_back(candidates[i].second);
                    std::sort(selected.begin(), selected.end());

                    // ---- ギャップ後処理: セットアップ挿入 ----
                    int    cumulDone3P = 0;
                    double cost3P      = 0.0;
                    std::vector<int> p3SetupSlots, finalExec;
                    finalExec.reserve(d);

                    for (int i = 0; i < d && !doFallback; ++i) {
                        int t = selected[i];

                        // ギャップ検出: 前スロットと不連続かつ既遂あり
                        if (i > 0 && cumulDone3P > 0) {
                            int prevT = finalExec.back();
                            if (t > prevT + 1) {
                                int setupLen = computeSetupLen(j, cumulDone3P);
                                if (setupLen > 0) {
                                    int gapStart = prevT + 1;
                                    int gapSize  = t - gapStart;

                                    if (gapSize >= setupLen) {
                                        // ギャップ内にセットアップが収まる
                                        double sCost  = 0.0;
                                        int    resumeT = applySetupSlots(
                                            j, gapStart, setupLen, T,
                                            usage, p3SetupSlots, sCost);
                                        if (resumeT < 0 || resumeT > t) {
                                            // セットアップが t を超えた → fallback
                                            doFallback = true;
                                            break;
                                        }
                                        cost3P += sCost;
                                    } else {
                                        // ギャップ幅 < setupLen → fallback
                                        doFallback = true;
                                        break;
                                    }
                                }
                            }
                        }

                        // exec スロット配置
                        for (int k = 0; k < nRes; ++k) usage[k][t] += inst.demand[j][k];
                        cost3P += computeSlotCost(j, t, T);
                        finalExec.push_back(t);
                        ++cumulDone3P;
                    }

                    if (doFallback) {
                        // 配置済みスロットをロールバック
                        for (int s : p3SetupSlots)
                            for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];
                        for (int s : finalExec)
                            for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];
                    } else {
                        start[j]   = finalExec.front();
                        finish[j]  = finalExec.back() + 1;
                        totalCost += cost3P;
                        solution->execSlots_[j] = std::move(finalExec);
                    }
                }

                // フォールバック: executeP2WithSetup（greedy + setup）
                if (doFallback) {
                    SetupP2Result res2 = executeP2WithSetup(j, est, T, usage);
                    if (!res2.feasible) {
                        solution->setObjective(0, 1e9);
                        solution->setObjective(1, 1e9);
                        return;
                    }
                    start[j]   = res2.firstExec;
                    finish[j]  = res2.lastExec + 1;
                    totalCost += res2.cost;
                    solution->execSlots_[j] = std::move(res2.execSlots);
                }
            }
        }
    } // end mainloop

    // ---- makespan ----
    int makespan = 0;
    for (int j = 0; j < n; ++j)
        if (finish[j] > makespan) makespan = finish[j];

    solution->startTimes_ = start;
    solution->setObjective(0, (double)makespan);
    solution->setObjective(1, totalCost);
}
