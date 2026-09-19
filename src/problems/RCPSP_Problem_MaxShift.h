#pragma once
#ifndef RCPSP_PROBLEM_MAXSHIFT_H
#define RCPSP_PROBLEM_MAXSHIFT_H

#include "RCPSP_Problem.h"
#include <string>
#include <vector>

// ============================================================
//  RCPSP_Problem_MaxShift
//
//  RCPSP_Problem の派生クラス。エンコーディングを変更:
//
//  変数配置:
//    vars[0 .. n-1]   : 活動リスト（先行制約を満たす順列）
//    vars[n .. 2n-1]  : max_shift リスト（各活動の最大遅延量, 非負整数 [0, T/2]）
//
//  スケジューリングルール（全活動共通）:
//    1. 先行活動の完了時刻から EST を求め、
//       最早実行可能時刻 s_j^mak を計算する
//    2. [s_j^mak, s_j^mak + max_shift_j] の実行可能開始時刻を列挙する
//       （資源制約を満たす連続 d_j スロット）
//    3. コスト最小の時刻に配置する（同コストなら最早時刻：左詰め）
//    4. max_shift_j = 0 のとき → s_j^mak のみ候補 → 最早配置（従来の makespan 優先と同等）
//
//  初期解生成:
//    max_shift_j = clip( round( N(T/4, T/8) ), 0, T/2 )
//    ダミー端点 (j=0, j=n-1) は常に 0
//
//  交叉: MaxShiftCrossover
//    活動リスト : Hartmann (1998) の 2 点交叉（変更なし）
//    max_shift  : ジョブを提供した親の max_shift 値を継承
//
//  変異: MaxShiftMutation
//    活動リスト : Boctor (1996) の挿入変異（変更なし）
//    max_shift  : 確率 1/n で N(0, T/8) ノイズを加算し [0, T/2] にクリップ
// ============================================================
class RCPSP_Problem_MaxShift : public RCPSP_Problem {
public:
    explicit RCPSP_Problem_MaxShift(const std::string &filename,
                                     int    strategy = 4,
                                     double rr       = 0.0,
                                     bool   rv       = false);

    // スケジューリング評価（max_shift エンコーディング版）
    void evaluate(Solution *solution) override;

    // ランダムトポロジカル順序 + N(T/4,T/8) で max_shift を初期化
    Solution* createRandomTopoSolution() override;

    // 極端解シード: メイクスパン最優先（全ジョブ max_shift = 0）
    Solution* createMakespanExtremeSolution();

    // 極端解シード: コスト最優先（全ジョブ max_shift = T/4）
    Solution* createCostExtremeSolution();

    // ----------------------------------------------------------------
    //  createPriorityRuleSolution (A1: priority-rule シード)
    //   RCPSP の優先規則で活動リストを生成する list-scheduling。
    //   eligible 集合（先行が全て確定したジョブ）から優先度最良を選ぶ。
    //   同点は乱数でタイブレークするため、同じ rule でも多様な順列を生成できる。
    //     rule = 0 : LFT  (Latest Finish Time 昇順, CPM ベース。makespan に最有効)
    //     rule = 1 : MTS  (Most Total Successors 降順, 推移的後続数)
    //     rule = 2 : GRPW (Greatest Rank Positional Weight 降順, d_j + Σ d_succ)
    //   max_shift は全ジョブ 0（EST 配置）。呼び出し側で必要なら付与する。
    // ----------------------------------------------------------------
    Solution* createPriorityRuleSolution(int rule);

    // 変異オペレータが σ 計算・クリップに使用するホライゾン T を返す
    int getHorizon() const;

    // ----------------------------------------------------------------
    //  getEffectiveHalfT
    //   strategy に応じた max_shift 上限を返す。
    //     strategy=1 → 0      (EST 固定, makespan 専用)
    //     strategy=2 → T/8    (軽微コスト探索)
    //     strategy=3 → T/4    (中程度, デフォルト)
    //     strategy=4 → T/2    (積極的コスト探索)
    // ----------------------------------------------------------------
    int getEffectiveHalfT() const;

    // strategy を変更し、変数上限 (vars[n..2n-1]) も同時に更新する。
    void setStrategy(int s) override;

    // ログ用エンコーディング名
    std::string encodingName() const override { return "MaxShift"; }

    // ----------------------------------------------------------------
    //  verifySchedule
    //   evaluate() が生成したスケジュールの先行制約・資源制約を検証する。
    //   違反があれば標準エラーに詳細を出力し false を返す。
    //   違反がなければ true を返す。
    //   デバッグ・単体テスト用。本番評価では呼ばれない。
    // ----------------------------------------------------------------
    bool verifySchedule(Solution *solution) const;

    // ----------------------------------------------------------------
    //  B8: 同コスト tie-break を「最早」→「資源平準化（残容量最大）」に切替
    //   evaluate() のコスト探索で、コストが同点の配置候補の中から、
    //   実行区間 [t, t+d) の残容量合計が最大（＝後続ジョブの自由度が高い）
    //   位置を選ぶ。コスト自体は変えないため 2 目的の cost を悪化させない。
    //   makespan の床を下げられるかを A/B で検証する目的で toggle 化。
    //   （旧 B5: FBI は 2 目的でコストを破壊し A1 に劣位のため 2026-07-22 に削除。
    //     詳細は .miss_memory/018, 019 参照）
    // ----------------------------------------------------------------
    void setResidualTieBreak(bool b) { residualTieBreak_ = b; }
    bool getResidualTieBreak() const { return residualTieBreak_; }

    // ----------------------------------------------------------------
    //  生成スキーム切替: Serial SGS（既定）⇄ Parallel SGS
    //   ゼミ助言「配置でなく生成スキーム/解構造」を受けた本命A。
    //   Serial SGS  : 活動リスト順にジョブを1つずつ EST 配置（現行）。
    //                 → active schedule 集合を探索。
    //   Parallel SGS: 時刻を進めながら、その時点で eligible なジョブを
    //                 活動リスト位置（優先度）順に配置（時間駆動）。
    //                 → non-delay schedule 集合を探索＝到達可能な
    //                   スケジュール集合そのものが変わる（B8/E17 で
    //                   「配置に伸びしろ無し」と判明したため生成スキームを変える）。
    //   コスト目的は両者とも max_shift 窓 [t, t+maxShift_j] 内の最安スロット
    //   探索で保持する。default OFF（＝Serial）で A/B できるよう toggle 化。
    // ----------------------------------------------------------------
    void setParallelSGS(bool b) { parallelSGS_ = b; }
    bool getParallelSGS() const { return parallelSGS_; }

private:
    bool residualTieBreak_ = false;
    bool parallelSGS_      = false;
};

#endif // RCPSP_PROBLEM_MAXSHIFT_H
