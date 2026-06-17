#include "RCPSP_Problem_Splitting.h"
#include "Solution.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <limits>
#include <iostream>
#include <vector>
#include <queue>

// ============================================================
//  ファイルローカル乱数生成器（基底クラスの rng とは独立）
// ============================================================
static std::mt19937 splitRng(54321);

// ============================================================
//  コンストラクタ
// ============================================================
RCPSP_Problem_Splitting::RCPSP_Problem_Splitting(
        const std::string &filename,
        ActivitySplittingMode mode,
        int    strategy,
        double rr,
        bool   rv)
    : RCPSP_Problem(filename, strategy, rr, rv), mode_(mode)
{
    const char *modeStr = (mode == ActivitySplittingMode::P2) ? "P2" : "P3";
    std::cout << "[RCPSP_Problem_Splitting] mode=" << modeStr << "\n";
}

// ============================================================
//  P2 シミュレーション（読み取り専用）
//
//  【理論的 P2 の定義に準拠】
//  スキップが許容されるのは「U_kt 自体がジョブ需要を下回る時刻」
//  （RR/RV による容量変動が原因の不足）のみ。
//
//    γ_jkt = 0  ⟺  capacityAtTime(k, t) < demand[j][k]  → スキップ許容
//    γ_jkt = 1  ⟺  capacityAtTime(k, t) >= demand[j][k] → 必ず実行しなければならない
//
//  γ=1 でも他ジョブが資源を使用中（残余不足）の場合は、
//  P2 分割ではなく「連続ブロックの探索リスタート」として扱う。
//  これにより RR=0 かつ RV=0 のとき P1 と同一解空間になる。
//
//  usage は変更しない（コスト比較用 dry-run）。
// ============================================================
RCPSP_Problem_Splitting::P2Result
RCPSP_Problem_Splitting::simulateP2(int j, int S_j, int T,
        const std::vector<std::vector<int>> &usage) const
{
    const auto &inst = instance;
    int d    = inst.duration[j];
    int nRes = inst.nRes;

    // 実行済みスロットを記録する（リスタート時にクリア）
    std::vector<int> execSlots;
    execSlots.reserve(d);
    double cost = 0.0;

    for (int t = S_j; t < T && (int)execSlots.size() < d; ++t) {
        // γ チェック: U_kt < demand → RR/RV による不足 → P2 スキップ許容
        bool gammaOk = true;
        for (int k = 0; k < nRes; ++k) {
            if (capacityAtTime(k, t) < inst.demand[j][k]) {
                gammaOk = false;
                break;
            }
        }
        if (!gammaOk) continue;  // γ=0: P2 分割スキップ

        // 残余容量チェック: 他ジョブが使用中かどうか
        bool residualOk = true;
        for (int k = 0; k < nRes; ++k) {
            if (usage[k][t] + inst.demand[j][k] > capacityAtTime(k, t)) {
                residualOk = false;
                break;
            }
        }

        if (residualOk) {
            execSlots.push_back(t);
            cost += computeSlotCost(j, t, T);
        } else {
            // γ=1 だが他ジョブで残余不足 → P2 では分割不可
            // 再開始点を execSlots[0]+1 にすることで P1 が見つける位置を飛ばさない
            int restartT = execSlots.empty() ? t : execSlots[0];
            execSlots.clear();
            cost = 0.0;
            t = restartT;  // ループの ++t で restartT+1 から再開
        }
    }

    if ((int)execSlots.size() < d) return {-1, -1, 0.0, false};
    return {execSlots.front(), execSlots.back(), cost, true};
}

// ============================================================
//  P2 実行（usage を更新する）
//
//  simulateP2 と同じロジックだが、実際に usage を更新する。
//  リスタート時は既に更新した usage をロールバックする。
// ============================================================
RCPSP_Problem_Splitting::P2Result
RCPSP_Problem_Splitting::executeP2(int j, int S_j, int T,
        std::vector<std::vector<int>> &usage) const
{
    const auto &inst = instance;
    int d    = inst.duration[j];
    int nRes = inst.nRes;

    std::vector<int> execSlots;
    execSlots.reserve(d);
    double cost = 0.0;

    for (int t = S_j; t < T && (int)execSlots.size() < d; ++t) {
        // γ チェック
        bool gammaOk = true;
        for (int k = 0; k < nRes; ++k) {
            if (capacityAtTime(k, t) < inst.demand[j][k]) {
                gammaOk = false;
                break;
            }
        }
        if (!gammaOk) continue;  // γ=0: P2 スキップ

        // 残余容量チェック
        bool residualOk = true;
        for (int k = 0; k < nRes; ++k) {
            if (usage[k][t] + inst.demand[j][k] > capacityAtTime(k, t)) {
                residualOk = false;
                break;
            }
        }

        if (residualOk) {
            for (int k = 0; k < nRes; ++k) usage[k][t] += inst.demand[j][k];
            execSlots.push_back(t);
            cost += computeSlotCost(j, t, T);
        } else {
            // γ=1 残余不足 → ロールバックしてリスタート
            // 再開始点を execSlots[0]+1 にすることで P1 が見つける位置を飛ばさない
            int restartT = execSlots.empty() ? t : execSlots[0];
            for (int s : execSlots)
                for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];
            execSlots.clear();
            cost = 0.0;
            t = restartT;  // ループの ++t で restartT+1 から再開
        }
    }

    if ((int)execSlots.size() < d) {
        // 完了できなかった場合も残余をロールバック
        for (int s : execSlots)
            for (int k = 0; k < nRes; ++k) usage[k][s] -= inst.demand[j][k];
        return {-1, -1, 0.0, false, {}};
    }
    return {execSlots.front(), execSlots.back(), cost, true, execSlots};
}

// ============================================================
//  evaluate() オーバーライド
//
//  基底クラスの evaluate() と同じ前処理（トポロジカル修復、
//  schedObj 取得、T 計算、maxShift 構築）を行い、
//  配置ステップのみ P2/P3 のロジックに差し替える。
// ============================================================
void RCPSP_Problem_Splitting::evaluate(Solution *solution) {
    // ---- 評価カウンタインクリメント ----
    ++evalCounter_;

    const auto &inst = instance;
    int n    = getNumJobs();
    int nRes = inst.nRes;

    auto &vars = solution->getVars();
    int nVars  = solution->getNumberOfVariables();

    // ---- 活動リスト取得・修復 ----
    std::vector<int> seq(n);
    for (int i = 0; i < n; ++i) seq[i] = vars[i];

    if (!checkTopological(seq)) {
        // Kahn法で先行制約を満たすよう修復（基底クラスの private 関数を
        // 直接呼べないため、repairToTopological と同等の処理を再実装）
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
    std::vector<int> schedObj(n, 0);
    if (nVars >= 2 * n) {
        for (int j = 0; j < n; ++j) {
            int v = vars[n + j];
            schedObj[j] = (v != 0) ? 1 : 0;
        }
    } else {
        std::bernoulli_distribution coin(0.5);
        for (int j = 0; j < n; ++j) schedObj[j] = coin(splitRng) ? 1 : 0;
    }
    if (n > 0) schedObj[0]     = 0;
    if (n > 1) schedObj[n - 1] = 0;

    // ---- ホライゾン T の決定 ----
    int T = 0;
    for (int j = 0; j < n; ++j) T += inst.duration[j];
    if (!inst.capacity_t.empty() && !inst.capacity_t[0].empty()) {
        int tvT = (int)inst.capacity_t[0].size();
        if (tvT > T) T = tvT;
    }
    if (T <= 0) T = 1;

    // ---- maxShift ベクタ構築 ----
    // 【バグ修正】
    //   修正前: splitRng(54321) という独立した RNG を使っていたため、
    //           P1 と P2 で同一解を評価しても maxShift が異なり、
    //           schedObj=1 ジョブのシフト量が diverge して
    //           RR=0,RV=0 での P1/P2 同一性（Proposition 2）が崩れていた。
    //   修正後: 基底クラスと同一の静的 rng を経由する buildMaxShiftForEval() を呼ぶ。
    //           → P1/P2 が同じ乱数列を共有し、統計的性質が揃う。
    std::vector<int> maxShift(n, 0);
    if (outputMaxShift_ >= 0) {
        // 出力用再評価モード（setOutputMaxShift(0) → ESS, setOutputMaxShift(N) → 固定シフト）
        std::fill(maxShift.begin(), maxShift.end(), outputMaxShift_);
    } else {
        maxShift = buildMaxShiftForEval(T);
    }
    // 端点は常に 0（buildMaxShiftForEval 内で設定済みだが念のため）
    if (n > 0) maxShift[0]     = 0;
    if (n > 1) maxShift[n - 1] = 0;

    // ---- 先行者リスト構築 ----
    std::vector<std::vector<int>> preds(n);
    for (int j = 0; j < n; ++j)
        for (int s : inst.successors[j])
            if (s >= 0 && s < n) preds[s].push_back(j);

    // ---- 資源使用量テーブル ----
    std::vector<std::vector<int>> usage(nRes, std::vector<int>(T, 0));
    std::vector<int> start(n, 0);
    std::vector<int> finish(n, 0);

    // execSlots_[j]: ジョブjが実際に実行した時刻スロットのリスト（テスト3・4用）
    solution->execSlots_.assign(n, {});

    double totalCost = 0.0;

    // ================================================================
    //  メインループ: 活動リスト順に各ジョブを配置
    // ================================================================
    for (int pos = 0; pos < n; ++pos) {
        int j = seq[pos];
        int d = inst.duration[j];

        if (d <= 0) {
            start[j] = finish[j] = 0;
            continue;
        }

        // 先行制約による最早開始時刻 (EST)
        int est = 0;
        for (int p : preds[j]) est = std::max(est, finish[p]);

        // ================================================================
        //  P2 配置ロジック
        //
        //  schedObj[j] == 0（makespan 優先）:
        //    S_j = est、直ちに P2 グリーディ実行
        //
        //  schedObj[j] == 1（コスト優先）:
        //    S_j を [est, est + maxShift[j]] の範囲でドライランし、
        //    最小コストの S_j で実際に配置する
        // ================================================================
        if (mode_ == ActivitySplittingMode::P2) {

            if (schedObj[j] == 0 || maxShift[j] == 0) {
                // makespan 優先: S_j = est から即実行
                P2Result res = executeP2(j, est, T, usage);
                if (!res.feasible) {
                    solution->setObjective(0, 1e9);
                    solution->setObjective(1, 1e9);
                    return;
                }
                start[j]   = res.firstExec;
                finish[j]  = res.lastExec + 1;
                totalCost += res.cost;
                solution->execSlots_[j] = std::move(res.slots);

            } else {
                // コスト優先: S_j を探索
                int latest  = std::min(T - d, est + maxShift[j]);
                double bestCost = std::numeric_limits<double>::infinity();
                int    bestSj   = est;

                for (int sj = est; sj <= latest; ++sj) {
                    P2Result sim = simulateP2(j, sj, T, usage);
                    if (sim.feasible && sim.cost < bestCost) {
                        bestCost = sim.cost;
                        bestSj   = sj;
                    }
                }

                P2Result res = executeP2(j, bestSj, T, usage);
                if (!res.feasible) {
                    // フォールバック: est から実行
                    res = executeP2(j, est, T, usage);
                    if (!res.feasible) {
                        solution->setObjective(0, 1e9);
                        solution->setObjective(1, 1e9);
                        return;
                    }
                }
                start[j]   = res.firstExec;
                finish[j]  = res.lastExec + 1;
                totalCost += res.cost;
                solution->execSlots_[j] = std::move(res.slots);
            }

        }
        // ================================================================
        //  P3 配置ロジック
        //
        //  schedObj[j] == 0（makespan 優先）:
        //    P2 と同様のグリーディ実行（資源あれば必ず実行）
        //
        //  schedObj[j] == 1（コスト優先）:
        //    [est, est + window] の実行可能スロットから
        //    コストの安い d_j スロットを選んで配置する。
        //    "資源があるのに実行しない" 選択が許容される。
        // ================================================================
        else { // mode_ == P3

            if (schedObj[j] == 0 || maxShift[j] == 0) {
                // ----------------------------------------------------------------
                // P3 makespan 優先: 資源が残余でも使えれば即実行（完全グリーディ）
                // P2 と異なり γ=1 でも他ジョブ競合があれば「スキップ」で進む
                // （P3 は任意時刻で中断・再開可能なため）
                // ----------------------------------------------------------------
                int executed3  = 0;
                int firstExec3 = -1;
                int lastExec3  = -1;
                double cost3   = 0.0;
                std::vector<int> p3Slots;

                for (int t = est; t < T && executed3 < d; ++t) {
                    bool ok = true;
                    for (int k = 0; k < nRes; ++k) {
                        if (usage[k][t] + inst.demand[j][k] > capacityAtTime(k, t)) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        for (int k = 0; k < nRes; ++k) usage[k][t] += inst.demand[j][k];
                        if (firstExec3 == -1) firstExec3 = t;
                        lastExec3 = t;
                        cost3 += computeSlotCost(j, t, T);
                        p3Slots.push_back(t);
                        ++executed3;
                    }
                }

                if (executed3 < d) {
                    solution->setObjective(0, 1e9);
                    solution->setObjective(1, 1e9);
                    return;
                }
                start[j]   = firstExec3;
                finish[j]  = lastExec3 + 1;
                totalCost += cost3;
                solution->execSlots_[j] = std::move(p3Slots);

            } else {
                // コスト優先: 実行可能スロットから安い d_j スロットを選択

                // ウィンドウ: [est, est + maxShift[j] + d] を探索
                // d 分の余裕を加えることで必要数のスロットが確保できる可能性を高める
                int windowEnd = std::min(T - 1, est + maxShift[j] + d);

                // 実行可能スロットを (cost, time) ペアで収集
                std::vector<std::pair<double, int>> candidates;
                candidates.reserve(windowEnd - est + 1);

                for (int t = est; t <= windowEnd; ++t) {
                    bool canExec = true;
                    for (int k = 0; k < nRes; ++k) {
                        int cap = capacityAtTime(k, t);
                        if (cap == 0 || usage[k][t] + inst.demand[j][k] > cap) {
                            canExec = false;
                            break;
                        }
                    }
                    if (canExec) {
                        double c = computeSlotCost(j, t, T);
                        candidates.push_back({c, t});
                    }
                }

                if ((int)candidates.size() >= d) {
                    // コスト昇順で上位 d スロットだけ確定（全体ソート不要）
                    std::partial_sort(candidates.begin(), candidates.begin() + d,
                                      candidates.end());

                    // 選択した d スロットを時刻順にソートしながらコストを集計
                    std::vector<int> selected;
                    selected.reserve(d);
                    double slotCostSum = 0.0;
                    for (int i = 0; i < d; ++i) {
                        selected.push_back(candidates[i].second);
                        slotCostSum += candidates[i].first;  // 既計算コストを再利用
                    }
                    std::sort(selected.begin(), selected.end());

                    // 実際に配置
                    int firstExec = selected.front();
                    int lastExec  = selected.back();
                    for (int t : selected) {
                        for (int k = 0; k < nRes; ++k) {
                            usage[k][t] += inst.demand[j][k];
                        }
                    }

                    start[j]   = firstExec;
                    finish[j]  = lastExec + 1;
                    totalCost += slotCostSum;
                    solution->execSlots_[j] = std::move(selected);

                } else {
                    // ウィンドウ内に十分なスロットがない場合は P2 グリーディにフォールバック
                    P2Result res = executeP2(j, est, T, usage);
                    if (!res.feasible) {
                        solution->setObjective(0, 1e9);
                        solution->setObjective(1, 1e9);
                        return;
                    }
                    start[j]   = res.firstExec;
                    finish[j]  = res.lastExec + 1;
                    totalCost += res.cost;
                    solution->execSlots_[j] = std::move(res.slots);
                }
            }
        }
    } // end mainloop

    // ---- makespan 計算 ----
    int makespan = 0;
    for (int j = 0; j < n; ++j) {
        if (finish[j] > makespan) makespan = finish[j];
    }

    // ---- 結果をソリューションに格納 ----
    // start[j] = 各ジョブの最初の実行時刻（ガントチャート・出力用）
    solution->startTimes_ = start;

    solution->setObjective(0, (double)makespan);
    solution->setObjective(1, totalCost);
}