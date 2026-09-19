#pragma once
#ifndef RCPSP_PROBLEM_MAXSHIFTDUR_H
#define RCPSP_PROBLEM_MAXSHIFTDUR_H

#include "RCPSP_Problem_MaxShift.h"
#include <string>

// ============================================================
//  RCPSP_Problem_MaxShiftDur
//
//  MaxShift エンコーディングの「所要時間ベース遅延スケーリング」版。
//  Gonçalves, Mendes & Resende (2008, EJOR) の random-key GA における
//    Delay = gene × 1.5 × MaxDur
//  を MaxShift に移植。グローバルな MaxDur の代わりに各活動自身の
//  所要時間 d_j を使用することで、活動単位の細粒度な制御を実現する。
//
//  変数配置:
//    vars[0 .. n-1]   : 活動リスト（先行制約を満たす順列）
//    vars[n .. 2n-1]  : 正規化遅延比率キー ρ_j ∈ [0, R]（R=1000）
//                       デコード: rho_j = vars[n+j] / (double)R
//
//  スケジューリングルール（P1, serial SGS）:
//    1. 先行活動の完了時刻から EST を求め、
//       最早実行可能時刻 t_mak を計算する（既存 MaxShift と同じ）
//    2. 遅延窓幅を duration-scaled で計算:
//       eff_j  = max(alpha * d_j,  beta * T)   (beta=0 で無効)
//       win_j  = round(rho_j * eff_j)
//    3. [t_mak, min(T - d_j, t_mak + win_j)] でコスト最小・左詰め配置
//    4. rho_j = 0 (vars[n+j]=0) → win_j=0 → 最早配置（makespan 優先）
//
//  Strategy → α の対応（getAlpha()）:
//    S1: 0.0  EST 固定（makespan 専用）
//    S2: 0.5  軽微コスト探索
//    S3: 1.0  中程度（デフォルト; 所要時間1本分が窓上限）
//    S4: 1.5  積極的コスト探索（原典値）
//
//  変数上限:
//    vars[0..n-1]   : [0, n-1]   (活動リスト, 基底クラスと同じ)
//    vars[n..2n-1]  : [0, R]     (比率キー; 基底クラスの halfT に相当)
//
//  交叉: MaxShiftCrossover をそのまま再利用可能
//        （提供した親の値を継承するだけで，絶対値か比率キーかを問わない）
//
//  変異: MaxShiftDurMutation を使用
//        （rho キーの上限を R=1000 に固定）
//
//  検証:
//    verifySchedule() は RCPSP_Problem_MaxShift::verifySchedule() を継承
//    （P1 serial SGS のスケジュール形式は同一）
// ============================================================
class RCPSP_Problem_MaxShiftDur : public RCPSP_Problem_MaxShift {
public:
    // 解像度: rho キーの上限 [0, R]
    static constexpr int R = 1000;

    explicit RCPSP_Problem_MaxShiftDur(const std::string &filename,
                                        int    strategy = 3,
                                        double rr       = 0.0,
                                        bool   rv       = false);

    // ---- 評価（duration-scaled window） ----
    void evaluate(Solution *solution) override;

    // ---- 初期解生成 ----
    Solution* createRandomTopoSolution()    override;
    Solution* createMakespanExtremeSolution();   // 全 rho = 0 → EST 配置
    Solution* createCostExtremeSolution();        // 全 rho = R → 最大窓, 端点は 0

    // ---- Strategy / α ----
    // strategy に応じた alpha 係数を返す
    double getAlpha() const;

    // strategy を更新し alpha_ を切り替える
    // (vars[n..2n-1] の上限は常に R なのでループ更新は不要)
    void setStrategy(int s) override;

    // getEffectiveHalfT のシャドーイング: R を返す
    // 注意: RCPSP_Problem_MaxShift::getEffectiveHalfT() は virtual でないため
    //       ポリモーフィックに解決されない。
    //       MaxShiftDurMutation が R を直接参照するため、実質的に使われる。
    int getEffectiveHalfT() const { return R; }

    // ---- ハイブリッド窓オプション ----
    // beta > 0 のとき: eff_j = max(alpha * d_j, beta * T)
    // beta = 0.0 (デフォルト) = 無効（純粋 duration-scaled）
    double getBeta() const   { return beta_; }
    void   setBeta(double b) { beta_ = b; }

    // ログ用エンコーディング名
    std::string encodingName() const override { return "MaxShiftDur"; }

private:
    double alpha_ = 1.0;  // strategy-dependent coefficient
    double beta_  = 0.0;  // hybrid window coefficient (0.0 = disabled)
};

#endif // RCPSP_PROBLEM_MAXSHIFTDUR_H
