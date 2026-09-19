#pragma once
#ifndef FRONT_LOCAL_SEARCH_H
#define FRONT_LOCAL_SEARCH_H

#include "core/SolutionSet.h"
#include "problems/RCPSP_Problem.h"
#include <string>
#include <vector>

// ============================================================
//  FrontLocalSearch
//
//  GA が出した最終パレートフロントの各解に対し、活動の開始時刻を
//  時間移動してコストを下げる後処理局所探索。
//
//  コア操作: 単一活動コスト one-opt（makespan 保存 or Δ 緩和）
//    各活動 j を先行制約・後続制約・資源制約を満たす範囲で
//    コスト最小のスロットに移動する。makespan を増やさない（Δ=0）
//    or 最大 Δ だけ許容する。
//
//  入力:
//    - RCPSP_Problem*: 評価・コスト計算・インスタンスデータ提供元
//    - SolutionSet*: GA が出した最終フロント（startTimes_ 設定済み）
//  出力:
//    - 局所探索で強化した解を加えた新しい SolutionSet*（非支配集合）
// ============================================================
class FrontLocalSearch {
public:
    struct Config {
        int maxPasses = 5;                     // one-opt の最大巡回数
        std::vector<int> deltas = {0, 1, 2, 4}; // makespan 緩和量
    };

    // 局所探索の統計（1解あたり）
    struct SolStat {
        int    origIdx;       // 元フロント内のインデックス
        double origMakespan;
        double origCost;
        int    delta;         // 適用した Δ
        double newMakespan;
        double newCost;
        double costReduction; // origCost - newCost
        int    passesUsed;
        int    movesApplied;
    };

    explicit FrontLocalSearch(RCPSP_Problem *prob, Config cfg = {});

    // フロント全体に局所探索を適用し、強化された非支配集合を返す。
    // 呼び出し側が返された SolutionSet を delete する責任を持つ。
    // stats が非 null なら各解の統計を追記する。
    SolutionSet* apply(SolutionSet *front, std::vector<SolStat> *stats = nullptr);

    // 局所探索ログを CSV 出力
    static void writeLog(const std::string &path,
                         const std::vector<SolStat> &stats,
                         bool truncate = true);

private:
    RCPSP_Problem *prob_;
    Config cfg_;

    int n_;     // ジョブ数
    int nRes_;  // 資源数
    int T_;     // ホライゾン

    std::vector<std::vector<int>> preds_;  // 直接先行
    std::vector<std::vector<int>> succs_;  // 直接後続

    // 1 解に対する one-opt 局所探索。makespan 上限 = msLimit。
    // 改善された startTimes, 新 makespan, 新 cost, パス数, 移動数を返す。
    struct OneOptResult {
        std::vector<int> startTimes;
        double makespan;
        double cost;
        int    passes;
        int    moves;
    };
    OneOptResult oneOpt(const std::vector<int> &startTimes, int msLimit);

    // 資源使用テーブルを構築
    std::vector<std::vector<int>> buildUsage(const std::vector<int> &startTimes) const;

    // ジョブ j を時刻 t から d_j スロット配置可能か（usage から j を除いた状態）
    bool canPlaceWithout(int j, int t,
                         const std::vector<std::vector<int>> &usageWithoutJ) const;

    // 逆トポロジカル順序（後続→先行）
    std::vector<int> reverseTopoOrder() const;
};

#endif // FRONT_LOCAL_SEARCH_H
