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
                                        bool   rv       = false,
                                        const std::string &costDir = "costs");

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
    // 窓幅の非線形マッピング win = round(rho^k * eff)。既定 k=1.0 で従来と完全に同一。
    // 実需の rho_needed が中央値 0.03 程度に偏っている（.miss_memory/040）ことへの対策の実験用。
    double getRhoPower() const { return rhoPower_; }
    void   setRhoPower(double k) { rhoPower_ = k; }
    // 目標工期の遺伝子（2026-10-24、既定 OFF）。vars[n+0]（開始ダミーの窓幅で、従来は使われない）を目標工期 D に使い、
    // 各作業の窓の右端を LST_j(D) = D − tail_j（先行関係だけの最長路で逆算した最遅開始）で打ち切る。
    //   D = CP + (g/R)^2 × (T − CP)   （CP = 先行関係だけのクリティカルパス長）
    // 工期に余裕のある作業だけが遅れられるので、遅らせが工期の鎖に積み上がるのを防ぐ（ε 制約法の考え方を復号に入れる）。
    void setDeadlineGene(bool on);
    bool getDeadlineGene() const { return deadlineMode_; }
    // 目標工期だけで窓を決める（作業ごとの窓幅の遺伝子を使わず、窓は常に最大＝ LST_j(D) で打ち切るだけ）。探索的。
    void setDeadlineOnly(bool on) { deadlineOnly_ = on; if (on) setDeadlineGene(true); }
    // 暦を考えた最遅開始（2026-10-24）: LST を先行関係だけでなく、容量の暦（休暇日・容量削減）で作業が実行できない時刻を避けて逆算する。
    // 他の作業による資源の取り合いは考えない（容量そのものが足りない時刻だけ避ける）。
    void setDeadlineCalendar(bool on) { deadlineCal_ = on; if (on) setDeadlineGene(true); }
    // 目標工期の働きの記録（診断用、計算には影響しない）: 窓を持つ作業の数、目標工期で窓が短くなった数、短くなったコマ数の合計
    long long statWin_ = 0, statClamp_ = 0, statCut_ = 0;
    // λ 遺伝子（2026-10-28、既定 OFF）: vars[n+n-1]（終端ダミーの窓幅で、従来は使われない）を u=g/R とし、
    // 窓の中の配置を「コスト + λ_j × (t − t_mak)」の最小で選ぶ。λ_j = κ u^2 ×（その作業の t_mak での1コマあたりのコスト）。
    // 少しの節約のために何周期も先へ飛ぶ遅らせ（休暇ありで工期の鎖を伸ばす主因、tools/ext1028/rv1_diag.py）を抑える。
    void setLambdaGene(bool on, double kappa = 0.2) { lambdaMode_ = on; lambdaKappa_ = kappa; }
    bool getLambdaGene() const { return lambdaMode_; }

    // ログ用エンコーディング名
    std::string encodingName() const override { return "MaxShiftDur"; }

protected:
    // 目標工期 D（deadlineMode_ のときだけ意味がある。P2/P3 の派生クラスでも使う）
    int deadlineFromVars(const std::vector<int> &vars, int T) const {
        const int n = (int)tail_.size();
        if (!deadlineMode_ || n == 0 || (int)vars.size() <= n) return T;
        const int cp = tail_[0];
        const double u = std::max(0, std::min(R, vars[n])) / (double)R;
        return cp + (int)std::lround(u * u * (double)std::max(0, T - cp));
    }
    bool lambdaMode_ = false;     // λ 遺伝子（既定 OFF）
    double lambdaKappa_ = 0.2;
    bool deadlineMode_ = false;   // 目標工期の遺伝子（既定 OFF）
    std::vector<int> tail_;       // tail_[j] = j の開始から終端までの先行関係だけの最長路（所要時間込み）
private:
    double alpha_ = 1.0;  // strategy-dependent coefficient
    double beta_  = 0.0;  // hybrid window coefficient (0.0 = disabled)
    double rhoPower_ = 1.0;  // rho の指数（1.0 = 従来）
    bool deadlineOnly_ = false;   // 目標工期だけで窓を決める（既定 OFF）
    bool deadlineCal_ = false;    // 暦を考えた最遅開始（既定 OFF）
    std::vector<int> revOrder_;   // 後続が先に来る順（暦つきの逆算に使う）
};

#endif // RCPSP_PROBLEM_MAXSHIFTDUR_H
