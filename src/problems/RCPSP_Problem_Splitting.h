#pragma once
#ifndef RCPSP_PROBLEM_SPLITTING_H
#define RCPSP_PROBLEM_SPLITTING_H

#include "RCPSP_Problem.h"
#include <vector>
#include <string>

// ============================================================
//  ActivitySplittingMode
//
//  P1: 分割なし   → RCPSP_Problem（基底クラス）がそのまま対応
//  P2: Non-preemptive splitting（資源不足時のみ中断許可）
//  P3: Preemptive splitting（任意時刻で中断・再開許可）
// ============================================================
enum class ActivitySplittingMode {
    P1 = 1,   // 分割なし（基底クラス RCPSP_Problem と同等）
    P2 = 2,   // 資源不足時のみ中断
    P3 = 3    // 任意時刻で中断・再開
};

// ============================================================
//  RCPSP_Problem_Splitting
//
//  RCPSP_Problem を継承し、evaluate() をオーバーライドして
//  P2 / P3 のスケジュール生成スキームを実装する。
//
//  エンコーディング（活動リスト + 目的リスト）は基底クラスと同じ。
//  個体のエンコーディング変更は不要であり、スケジュール生成のみ異なる。
//
//  ■ P2 スケジュール生成アルゴリズム（先生の指摘に基づく実装）
//    先行制約 EST から順にスキャン。資源が確保できる時刻には実行
//    （x_jt=1）、確保できない時刻はスキップ。d_j 単位分が累積した
//    ら完了。資源が足りているのに意図的にスキップすることは禁止。
//
//  ■ P3 スケジュール生成アルゴリズム
//    schedObj=0（makespan優先）: P2 と同様のグリーディ実行。
//    schedObj=1（コスト優先）  : EST 以降の実行可能スロットから
//                                コストの安い d_j スロットを選択。
//                                資源が足りていても実行しない選択が可能。
// ============================================================
class RCPSP_Problem_Splitting : public RCPSP_Problem {
public:
    // ----------------------------------------------------------------
    //  コンストラクタ
    //   mode     : P2 または P3（P1 の場合は基底クラスをそのまま使う）
    //   strategy : maxShift 戦略（基底クラスと同じ 1〜4）
    //   rr / rv  : 時間依存容量パラメータ（基底クラスと同じ）
    // ----------------------------------------------------------------
    explicit RCPSP_Problem_Splitting(const std::string &filename,
                                      ActivitySplittingMode mode,
                                      int    strategy = 4,
                                      double rr       = 0.0,
                                      bool   rv       = false);

    // evaluate() をオーバーライドして P2/P3 スケジュール生成を実装
    void evaluate(Solution *solution) override;

    ActivitySplittingMode getSplittingMode() const { return mode_; }

private:
    ActivitySplittingMode mode_;

    // ----------------------------------------------------------------
    //  内部ヘルパー
    // ----------------------------------------------------------------

    // P2 シミュレーション結果
    struct P2Result {
        int    firstExec = -1;  // 最初に実行した時刻（-1: 失敗）
        int    lastExec  = -1;  // 最後に実行した時刻（-1: 失敗）
        double cost      = 0.0; // 実行コスト合計
        bool   feasible  = false; // d_j 単位を蓄積できたか
        std::vector<int> slots; // 実行時刻スロット列（executeP2 のみ設定; simulateP2 は空）
    };

    // P2 をシミュレート（usage を変更しない読み取り専用）
    //   S_j    : 開始時刻
    //   T      : ホライゾン
    //   usage  : 現在の資源使用量テーブル（読み取り専用）
    //   j      : ジョブ番号
    P2Result simulateP2(int j, int S_j, int T,
                        const std::vector<std::vector<int>> &usage) const;

    // P2 を実行（usage を更新する）
    P2Result executeP2(int j, int S_j, int T,
                       std::vector<std::vector<int>> &usage) const;
};

#endif // RCPSP_PROBLEM_SPLITTING_H