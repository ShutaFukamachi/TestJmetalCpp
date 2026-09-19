#pragma once
#ifndef RCPSP_CONDITIONS_H
#define RCPSP_CONDITIONS_H

#include <vector>

// ============================================================
//  RCPSP 実験条件（RR/RV）の単一定義。
//
//  RR=0.75 は MIP・NSGA ともに実務的に破綻することが実測で判明したため
//  （MIP: horizon が最大653まで伸びバイナリ変数が約2万個に膨張し、120秒以内に
//   SolCount=0 のまま終わる条件がある／NSGA: フロントが単一 makespan に退化する）、
//  既定の定量比較の対象からは外し、RCPSP_LIMIT_CONDITIONS()（限界条件の分析用）
//  として別枠で保持する。
//
//  RR=0.75 を比較対象に戻したい場合は、各ランナーが参照している
//  RCPSP_STANDARD_CONDITIONS() の呼び出しを RCPSP_ALL_CONDITIONS() に
//  差し替えるだけでよい（このファイル自体は変更不要）。
// ============================================================

struct RCPSP_Cond {
    double rr;
    bool   rv;
};

// 比較対象（既定）: RR ∈ {0.00, 0.25, 0.50} × RV ∈ {off, on}
inline const std::vector<RCPSP_Cond>& RCPSP_STANDARD_CONDITIONS() {
    static const std::vector<RCPSP_Cond> c = {
        {0.00, false}, {0.00, true},
        {0.25, false}, {0.25, true},
        {0.50, false}, {0.50, true},
    };
    return c;
}

// 限界条件: RR=0.75 × RV ∈ {off, on}（別解析用。既定の一括実行では使わない）
inline const std::vector<RCPSP_Cond>& RCPSP_LIMIT_CONDITIONS() {
    static const std::vector<RCPSP_Cond> c = {
        {0.75, false}, {0.75, true},
    };
    return c;
}

// 全条件（RR=0.75 を含む8条件）: RR=0.75 を再有効化する場合や網羅検証で使用
inline const std::vector<RCPSP_Cond>& RCPSP_ALL_CONDITIONS() {
    static const std::vector<RCPSP_Cond> c = {
        {0.00, false}, {0.00, true},
        {0.25, false}, {0.25, true},
        {0.50, false}, {0.50, true},
        {0.75, false}, {0.75, true},
    };
    return c;
}

#endif // RCPSP_CONDITIONS_H
