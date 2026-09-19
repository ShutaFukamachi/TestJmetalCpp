#pragma once
#ifndef MAXSHIFT_SENSITIVITY_H
#define MAXSHIFT_SENSITIVITY_H

#include "RCPSP_Problem_MaxShift.h"
#include "Solution.h"
#include <string>
#include <vector>

// ============================================================
//  MaxShiftSensitivityAnalyzer
//
//  max_shift の上限値が目的関数に与える影響を定量的に分析するクラス。
//
//  固定の活動リストに対して max_shift を 0 から T まで段階的に
//  変化させ、各点での (makespan, cost) を記録する。
//
//  用途:
//    - 現在の上限 T/4 が妥当かどうかを検証する
//    - コストが plateau に達するまでに必要な max_shift を推定する
//    - Strategy A の各上限（T/8, T/4, T/2）の設計根拠を得る
//
//  使い方（main.cpp から呼び出す想定）:
//    MaxShiftSensitivityAnalyzer sa("j30.sm/j301_1.sm", 0.0, false);
//    sa.runAndSave("maxshift_sensitivity_RR000_RV0.csv", 40);
// ============================================================
class MaxShiftSensitivityAnalyzer {
public:
    // 1 つのスイープ点
    struct SweepPoint {
        int    maxShift;  // 全ジョブに一様に与えた max_shift 値
        double makespan;  // 評価後のメイクスパン
        double cost;      // 評価後のコスト
    };

    // instanceFile : PSPLIB .sm ファイルパス
    // rr / rv      : 資源削減率 / 時間変動フラグ
    MaxShiftSensitivityAnalyzer(const std::string& instanceFile,
                                 double rr = 0.0, bool rv = false);
    ~MaxShiftSensitivityAnalyzer();

    // ----------------------------------------------------------------
    //  sweep
    //   activityList を固定し、max_shift = 0 〜 T を steps 段階で評価。
    //   戻り値: steps+1 個の SweepPoint（実行不可能解は除外）
    // ----------------------------------------------------------------
    std::vector<SweepPoint> sweep(const std::vector<int>& activityList,
                                   int steps = 40) const;

    // ----------------------------------------------------------------
    //  runAndSave
    //   min-makespan 活動リストと min-cost 活動リストの 2 通りでスイープし、
    //   結果を CSV ファイルに出力する。
    //   コンソールに plateau 検出結果と各上限での値も表示する。
    // ----------------------------------------------------------------
    void runAndSave(const std::string& outCsvPath, int steps = 40);

    int getT() const;
    int getN() const;

private:
    RCPSP_Problem_MaxShift* prob_;
    std::string instanceFile_;
    double      rr_;
    bool        rv_;

    // Solution の vars[0..n-1] から活動リストを抽出
    static std::vector<int> extractActivityList(Solution* sol, int n);
};

#endif // MAXSHIFT_SENSITIVITY_H
