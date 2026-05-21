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

    // ----------------------------------------------------------------
    //  verifySchedule
    //   evaluate() が生成したスケジュールの先行制約・資源制約を検証する。
    //   違反があれば標準エラーに詳細を出力し false を返す。
    //   違反がなければ true を返す。
    //   デバッグ・単体テスト用。本番評価では呼ばれない。
    // ----------------------------------------------------------------
    bool verifySchedule(Solution *solution) const;
};

#endif // RCPSP_PROBLEM_MAXSHIFT_H
