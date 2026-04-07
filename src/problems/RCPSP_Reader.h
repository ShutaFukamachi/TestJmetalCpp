#pragma once
#ifndef RCPSP_READER_H
#define RCPSP_READER_H

#include <string>
#include <vector>

struct RCPSP_Instance {
    int nJobs = 0;
    int nRes  = 0;

    std::vector<int>              duration;    // duration[j]
    std::vector<std::vector<int>> demand;      // demand[j][k]
    std::vector<int>              capacity;    // capacity[k]  ← .sm から読んだ定数容量
    std::vector<std::vector<int>> successors;  // successors[j] (0-based)

    // ============================================================
    // [追加フィールド] capacity_t[k][t]
    //   RR/RV による時間依存容量テーブル
    //   空（size==0）のとき → capacity[k] が使われる（従来通り）
    //   capacity_t[k][t] == 0 の時刻 → 資源休暇（使用不可）
    // ============================================================
    std::vector<std::vector<int>> capacity_t;
};

// PSPLIB .sm ファイルを読み込む（変更なし）
RCPSP_Instance readPSPLIB_SM(const std::string &filename);

#endif // RCPSP_READER_H

