#pragma once
#ifndef RCPSP_MIP_TRANSFORM_H
#define RCPSP_MIP_TRANSFORM_H

#include "RCPSP_Reader.h"
#include "RCPSP_MIP_Solver.h"
#include <vector>

// ============================================================
//  RCPSP_MIP_Transform
//
//  P2/P3 を「新しい変数・制約を追加せず、既存の P1 MIP 定式化を
//  そのまま流用する」ために、instance をメモリ上で変換するユーティリティ。
//  RCPSP_MIP_Solver 本体（buildModel 等）は一切変更しない。
// ============================================================

// ----------------------------------------------------------------
//  P3: 単位分割（任意中断）
//
//  所要時間 d_j の活動 j を、所要時間 1 の単位活動 d_j 個の鎖に置き換える。
//  時間軸・容量プロファイル・コスト表は元インスタンスのものをそのまま使う。
// ----------------------------------------------------------------
struct UnitSplitMapping {
    // 元ジョブ j の単位活動範囲（新ジョブID、両端含む）
    std::vector<int> firstUnit;
    std::vector<int> lastUnit;
    // 新ジョブID -> 元ジョブID
    std::vector<int> unitToOrig;
};

RCPSP_Instance buildUnitSplitInstance(const RCPSP_Instance &orig, UnitSplitMapping &mapping);

// ----------------------------------------------------------------
//  P2: 時間軸圧縮（休暇時のみ中断）
//
//  全資源の容量が同時に 0 になる時刻（休暇日）を時間軸から取り除いて
//  圧縮する。圧縮後の問題は通常の P1（連続実行）として解ける。
// ----------------------------------------------------------------
struct TimeCompression {
    std::vector<int> compressedToOriginal;  // size T'; 圧縮後index -> 元の絶対時刻
    std::vector<int> originalToCompressed;  // size T ; 元の絶対時刻 -> 圧縮後index（除去された時刻は -1）
    int T_original   = 0;
    int T_compressed = 0;
    int numRemoved    = 0;
};

// capacity_t（元の時間軸）から、全資源同時に容量0となる時刻を検出して圧縮マッピングを作る。
TimeCompression buildTimeCompression(const std::vector<std::vector<int>> &capacity_t);

// 圧縮後の instance を作る（duration/demand/successors/capacity は元のまま、
// capacity_t のみ圧縮軸に再インデックスする）。
RCPSP_Instance buildCompressedInstance(const RCPSP_Instance &orig, const TimeCompression &tc);

// 圧縮軸のコスト表を作る（圧縮後時刻 t' には、対応する元の時刻 t の c_k(t) を割り当てる）。
// solver は正本コスト表がロード済み（またはロード可能）な状態であること。
// horizonForLookup は元インスタンスの horizon（getHorizon()、圧縮前）を渡す。
std::vector<std::vector<double>> buildRemappedCostTable(
        const RCPSP_MIP_Solver &solver,
        const TimeCompression  &tc,
        int                     horizonForLookup);

// ----------------------------------------------------------------
//  共通: 実行時刻リストによるスケジュール表現
//  （P2/P3 とも、単一の開始時刻では中断を表現できないため）
// ----------------------------------------------------------------
struct RealSchedule {
    bool feasible = false;
    double makespan = 1e9;
    double cost = 1e9;
    bool optimal = false;
    double gap = 1.0;
    double solveSeconds = 0.0;
    // execTimes[origJob] = 実際に実行した時刻（絶対・元の時間軸）の昇順リスト
    std::vector<std::vector<int>> execTimes;
};

#endif // RCPSP_MIP_TRANSFORM_H
