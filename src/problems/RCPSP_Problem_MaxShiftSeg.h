#pragma once
#ifndef RCPSP_PROBLEM_MAXSHIFTSEG_H
#define RCPSP_PROBLEM_MAXSHIFTSEG_H

#include "RCPSP_Problem_MaxShift.h"
#include <string>

// ============================================================
//  RCPSP_Problem_MaxShiftSeg
//
//  MaxShift エンコーディングの「セグメント選択型デコード」版。
//  Rodríguez-Ballesteros, Alcaraz & Anton-Sanchez (ESWA 2026) の
//  区間分割手法を移植し、遺伝子を「窓内の位置セレクタ」に再解釈する。
//
//  従来の MaxShift は窓全体で argmin(cost) するため、
//    (1) 多数の遺伝子型が同一スケジュールに潰される（多対一）
//    (2) 中間スロット（局所高コスト）に構造的に到達できない
//  本クラスは窓をセグメントに分割し、遺伝子が指すセグメント内でのみ
//  argmin を行うことで、デコードをほぼ単射化し到達可能集合を拡張する。
//
//  変数配置:
//    vars[0 .. n-1]   : 活動リスト（先行制約を満たす順列）
//    vars[n .. 2n-1]  : 位置セレクタキー ρ_j ∈ [0, R]（R=1000）
//
//  デコード（P1 serial SGS）:
//    各活動 j について EST → t_mak を求めたうえで:
//      hi_j   = min(T - d_j, t_mak + Wmax_j)
//      K      = numSegments_
//      seg    = min(K-1, floor(ρ_j * K))
//      segLo  = t_mak + round(seg     * (hi_j - t_mak) / K)
//      segHi  = t_mak + round((seg+1) * (hi_j - t_mak) / K)
//      [segLo, segHi] 内でコスト最小・左詰め配置
//      実行可能スロットなし → 窓全体で segLo 最近傍フォールバック
//
//    Wmax_j = round(max(alpha_ * d_j, beta_ * T))
//
//  Strategy → α:
//    S1=0.0, S2=0.5, S3=1.0, S4=1.5
//
//  交叉: MaxShiftCrossover を再利用
//  変異: MaxShiftMutation を halfT_=R で構築して再利用
// ============================================================
class RCPSP_Problem_MaxShiftSeg : public RCPSP_Problem_MaxShift {
public:
    static constexpr int R = 1000;

    explicit RCPSP_Problem_MaxShiftSeg(const std::string &filename,
                                        int    strategy = 3,
                                        double rr       = 0.0,
                                        bool   rv       = false);

    // ---- 評価（セグメント選択型デコード）----
    void evaluate(Solution *solution) override;

    // ---- 初期解生成 ----
    Solution* createRandomTopoSolution()    override;
    Solution* createMakespanExtremeSolution();   // 全 ρ=0 → seg 0（最早側）
    Solution* createCostExtremeSolution();        // 全 ρ=R → 最終セグメント

    // ---- Strategy / α ----
    double getAlpha() const;
    void   setStrategy(int s) override;

    // getEffectiveHalfT シャドーイング: R を返す
    int getEffectiveHalfT() const { return R; }

    // ---- パラメータ ----
    int    getNumSegments() const   { return numSegments_; }
    void   setNumSegments(int k)   { numSegments_ = k; }
    double getBeta() const         { return beta_; }
    void   setBeta(double b)       { beta_ = b; }

    // ---- encodingName（ログ用）----
    std::string encodingName() const override { return "MaxShiftSeg"; }

private:
    double alpha_       = 1.0;
    double beta_        = 0.0;
    int    numSegments_ = 8;
};

#endif // RCPSP_PROBLEM_MAXSHIFTSEG_H
