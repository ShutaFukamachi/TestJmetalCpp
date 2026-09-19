// ============================================================
//  main_MIP_Verify.cpp  —  MIP が時変容量(RR/RV)を正しく解いているかの検証
//
//  目的: 「RR/RV を上げても MIP の makespan があまり悪化しない」のが
//        正しい厳密解なのか、それとも 016 型の緩和バグ（容量0/時変容量を
//        すり抜けている）なのかを決定的に判定する。
//
//  方法（016 の相互検証プロトコル）:
//    1. RCPSP_MIP_Solver で makespan 最小解を解く。
//    2. 得た開始時刻を、MIP 自身が使った capacity_t に対して feasibility 検証:
//       - 先行制約違反
//       - 資源制約違反（usage > capacityAtTime）
//       - 休暇日(cap=0)を貫通して実行しているジョブ数
//    3. 違反 0 なら MIP は正しい（gap は NSGA 側の弱さ）。違反ありなら MIP 緩和バグ。
//
//  使い方: MIPVERIFY [instance] [rr] [rv]
//     例:  MIPVERIFY j30.sm/j309_1.sm 0.50 1
// ============================================================
#include <climits>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "problems/RCPSP_MIP_Solver.h"

using namespace std;

int main(int argc, char **argv) {
    string inst = (argc >= 2) ? argv[1] : "j30.sm/j309_1.sm";
    double rr   = (argc >= 3) ? atof(argv[2]) : 0.50;
    bool   rv   = (argc >= 4) ? (atoi(argv[3]) != 0) : true;

    cout << "=== MIP time-varying-capacity verification ===\n";
    cout << "instance=" << inst << "  rr=" << rr << "  rv=" << rv << "\n\n";

    RCPSP_MIP_Solver prob(inst, rr, rv);

    const int n    = prob.getNumJobs();
    const int nRes = prob.getNumResources();
    const int T    = prob.getHorizon();
    const auto &dur    = prob.getDurations();
    const auto &dem    = prob.getDemand();
    const auto &succ   = prob.getSuccessors();

    // ---- capacity_t の休暇日(cap=0)スロット統計 ----
    int zeroSlots = 0, minCap = INT_MAX, maxCap = 0;
    for (int k = 0; k < nRes; ++k)
        for (int t = 0; t < T; ++t) {
            int c = prob.getCapacityAtTime(k, t);
            if (c == 0) ++zeroSlots;
            minCap = min(minCap, c);
            maxCap = max(maxCap, c);
        }
    cout << "horizon T=" << T << "  nRes=" << nRes
         << "  capacity range=[" << minCap << ", " << maxCap << "]"
         << "  zero(vacation) slots=" << zeroSlots << "\n\n";

    // ---- MIP: makespan 最小解 ----
    RCPSP_MIP_Solver::Config cfg;
    cfg.verbose = false;
    cfg.timeLimit = 120.0;
    cout << "[MIP] solving min-makespan...\n";
    auto res = prob.solveMakespan(cfg);
    if (!res.feasible) {
        cout << "[MIP] INFEASIBLE / no solution\n";
        return 1;
    }
    cout << "[MIP] makespan=" << (int)res.makespan
         << "  cost=" << fixed << setprecision(0) << res.cost
         << "  optimal=" << (res.optimal ? "yes" : "no(gap)") << "\n\n";

    const auto &st = res.startTimes;

    // ---- 検証 1: 先行制約 ----
    int precViol = 0;
    for (int i = 0; i < n; ++i) {
        if (dur[i] <= 0) continue;
        for (int s : succ[i]) {
            if (s < 0 || s >= n || dur[s] <= 0) continue;
            if (st[i] + dur[i] > st[s]) {
                ++precViol;
                if (precViol <= 5)
                    cout << "  [prec] job " << i << "(s=" << st[i] << ",d=" << dur[i]
                         << ") -> job " << s << "(s=" << st[s] << ")\n";
            }
        }
    }

    // ---- 検証 2: 資源制約 + 休暇日貫通 ----
    vector<vector<int>> usage(nRes, vector<int>(T, 0));
    int vacPierce = 0;   // 休暇日(cap=0)を実行区間に含むジョブ数
    for (int j = 0; j < n; ++j) {
        if (dur[j] <= 0) continue;
        bool pierced = false;
        for (int t = st[j]; t < st[j] + dur[j] && t < T; ++t) {
            for (int k = 0; k < nRes; ++k) {
                usage[k][t] += dem[j][k];
                if (dem[j][k] > 0 && prob.getCapacityAtTime(k, t) == 0)
                    pierced = true;
            }
        }
        if (pierced) {
            ++vacPierce;
            if (vacPierce <= 8)
                cout << "  [vac-pierce] job " << j << " runs [" << st[j]
                     << "," << (st[j] + dur[j]) << ") through a cap=0 slot\n";
        }
    }
    int resViol = 0;
    for (int t = 0; t < T; ++t)
        for (int k = 0; k < nRes; ++k) {
            int cap = prob.getCapacityAtTime(k, t);
            if (usage[k][t] > cap) {
                ++resViol;
                if (resViol <= 8)
                    cout << "  [res] k=" << k << " t=" << t
                         << " usage=" << usage[k][t] << " > cap=" << cap << "\n";
            }
        }

    cout << "\n---- VERDICT ----\n";
    cout << "precedence violations : " << precViol << "\n";
    cout << "resource   violations : " << resViol  << "\n";
    cout << "vacation-day pierces  : " << vacPierce << "\n";
    if (precViol == 0 && resViol == 0 && vacPierce == 0) {
        cout << ">>> MIP schedule is FEASIBLE under capacity_t.\n";
        cout << ">>> The MIP correctly handles time-varying capacity.\n";
        cout << ">>> The large NSGA gap is a genuine heuristic weakness, NOT a MIP bug.\n";
    } else {
        cout << ">>> MIP schedule VIOLATES capacity_t (RELAXED / bug, cf. .miss_memory/016).\n";
    }
    return 0;
}
