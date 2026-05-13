#pragma once
#ifndef RCPSP_PROBLEM_SETUP_H
#define RCPSP_PROBLEM_SETUP_H

#include "RCPSP_Problem_Splitting.h"
#include <vector>
#include <string>

// ============================================================
//  SetupModel — Vanhoucke & Coelho (2019) のセットアップ時間モデル
//
//  d_i  : ジョブ i の総所要量（作業時間）
//  x    : この再開直前までの累積作業量
//  %s   : セットアップ係数（alphaS）
//
//  TW : t_i^s = floor(%s / 2 * d_i)          全作業量に比例（固定）
//  WD : t_i^s = floor(%s * x)                 既遂作業量に比例
//  WR : t_i^s = floor(%s * (d_i + 1 - x))    残余作業量に比例
//
//  いずれも「最初のパート（cumulDone == 0）」にはセットアップ不要のため
//  computeSetupLen() は cumulDone == 0 のとき必ず 0 を返す。
// ============================================================
enum class SetupModel {
    TW,   // Total Work: 全作業量依存
    WD,   // Work Done: 既遂作業量依存
    WR    // Work Remaining: 残余作業量依存
};

// ============================================================
//  RCPSP_Problem_Setup
//
//  RCPSP_Problem_Splitting を継承し、P2/P3 のスケジューリング時に
//  中断→再開のセットアップ時間（TW / WD / WR）を挿入する。
//
//  ■ セットアップ時間の挿入タイミング
//    P2: γ=0 (RR/RV 容量不足) によるギャップ後の再開時
//    P3 makespan 優先: 任意のスキップ後の再開時
//    P3 cost 優先   : 選択スロットのギャップ後の再開時（後処理）
//
//  ■ セットアップ中の資源占有
//    セットアップ期間は "作業は進まないが資源を占有し続ける" 扱い。
//    usage テーブルに demand を加算し、コストも計上する。
//
//  ■ P2 γ=1 リスタートとの区別
//    γ=1 かつ残余不足によるリスタートは P2 分割ではなく配置失敗。
//    この場合は現ブロックのセットアップ + 実行スロットをロールバックし、
//    セットアップなしで再試行する（真の P2 分割ではないため）。
//
//  ■ schedObj の扱い
//    P2: schedObj=1 かつ maxShift>0 のとき、simulateP2WithSetup で S_j を探索し
//        最小コストの S_j で executeP2WithSetup を実行する（Splitting と同一パターン）。
//    P3 makespan: schedObj=0 または maxShift=0 のときグリーディ実行（セットアップ挿入込み）。
//    P3 cost: 実行可能スロットから安価な d_j 枚を選択し、ギャップ後処理でセットアップ挿入。
//             ギャップ幅不足でセットアップが収まらない場合は executeP2WithSetup にフォールバック。
// ============================================================
class RCPSP_Problem_Setup : public RCPSP_Problem_Splitting {
public:
    // ----------------------------------------------------------------
    //  コンストラクタ
    //   mode      : P2 または P3
    //   setupModel: TW / WD / WR
    //   alphaS    : セットアップ係数 (%s)
    //   strategy  : maxShift 戦略 (1〜4)
    //   rr / rv   : 時間依存容量パラメータ
    // ----------------------------------------------------------------
    RCPSP_Problem_Setup(const std::string &filename,
                        ActivitySplittingMode mode,
                        SetupModel            setupModel,
                        double                alphaS,
                        int                   strategy = 4,
                        double                rr       = 0.0,
                        bool                  rv       = false);

    // P2/P3 + セットアップ時間を考慮した評価関数
    void evaluate(Solution *solution) override;

    SetupModel getSetupModel() const { return setupModel_; }
    double     getAlphaS()    const { return alphaS_; }

private:
    SetupModel setupModel_;
    double     alphaS_;

    // ----------------------------------------------------------------
    //  computeSetupLen
    //   j         : ジョブ番号
    //   cumulDone : この再開直前までの累積作業量
    //   戻り値    : セットアップ所要時間（スロット数）
    //               cumulDone == 0 のとき必ず 0
    // ----------------------------------------------------------------
    int computeSetupLen(int j, int cumulDone) const;

    // ----------------------------------------------------------------
    //  applySetupSlots
    //   j        : ジョブ番号
    //   t_start  : セットアップ開始希望時刻
    //   setupLen : 必要スロット数
    //   T        : ホライゾン
    //   usage    : 資源使用量テーブル（書き換える）
    //   outSlots : 確保したセットアップスロットの出力先
    //   outCost  : セットアップ期間のコスト出力先
    //
    //   t_start 以降で setup_len 枠の連続した実行可能スロットを探し、
    //   usage を更新してコストを返す。
    //   戻り値: セットアップ終了直後の時刻（実行再開可能時刻）
    //           -1 のとき T 内で確保不能（infeasible）
    // ----------------------------------------------------------------
    int applySetupSlots(int j, int t_start, int setupLen, int T,
                        std::vector<std::vector<int>> &usage,
                        std::vector<int>              &outSlots,
                        double                        &outCost) const;

    // ----------------------------------------------------------------
    //  executeP2WithSetup
    //   P2 分割スキームにセットアップ時間を組み込んだ配置関数。
    //   usage を直接更新する。
    //
    //   戻り値: P2Result（基底クラスの構造体を流用）
    //     feasible == false のとき usage はロールバック済み
    // ----------------------------------------------------------------
    struct SetupP2Result {
        int             firstExec = -1;
        int             lastExec  = -1;
        double          cost      = 0.0;
        bool            feasible  = false;
        std::vector<int> execSlots;
        std::vector<int> setupSlots;
    };

    // ----------------------------------------------------------------
    //  simulateP2WithSetup
    //   executeP2WithSetup の読み取り専用版（usage を変更しない）。
    //   schedObj=1 時の S_j コスト探索ループ内で使用する。
    //   内部で usage をコピーして executeP2WithSetup を呼ぶ。
    // ----------------------------------------------------------------
    SetupP2Result simulateP2WithSetup(int j, int S_j, int T,
                                      const std::vector<std::vector<int>> &usage) const;

    SetupP2Result executeP2WithSetup(int j, int S_j, int T,
                                     std::vector<std::vector<int>> &usage) const;
};

#endif // RCPSP_PROBLEM_SETUP_H
