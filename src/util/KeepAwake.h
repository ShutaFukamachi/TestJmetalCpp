#pragma once
#ifndef RCPSP_UTIL_KEEPAWAKE_H
#define RCPSP_UTIL_KEEPAWAKE_H

// ============================================================
//  enableKeepAwake()
//
//  長時間実行（MIP の複数条件・NSGA-II の複数ストラテジー等）の途中で
//  Windows がアイドル判定でスリープに入り、壁時計ベースの Gurobi TimeLimit
//  や経過時間計測を狂わせる事故を防ぐ（.miss_memory/042 参照:
//  スリープ復帰で ε=131 の解が本来33秒で最適なところ TimeLimit 超過扱いで
//  打ち切られた実例）。
//
//  SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) は
//  電源設定（powercfg 等）を一切変更しない。呼び出したスレッド（プロセス）が
//  生きている間だけ有効な一時的なフラグで、プロセス終了時に自動的に解除される。
//  ノートPCの蓋を閉じる／ユーザーが手動でスリープさせる操作までは防げない。
// ============================================================

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iostream>

inline void enableKeepAwake() {
    EXECUTION_STATE prev = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    if (prev != 0) {
        std::cout << "[KEEPAWAKE] enabled\n";
    } else {
        std::cerr << "[KEEPAWAKE] failed to enable (SetThreadExecutionState returned 0); "
                     "the system may still sleep during a long run.\n";
    }
}
#else
inline void enableKeepAwake() {
    // 非Windows環境では何もしない。
}
#endif

#endif // RCPSP_UTIL_KEEPAWAKE_H
