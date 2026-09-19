// ============================================================
//  main_MIP_RCPSP.cpp  (RCPSPMIPCpp ターゲット)
//
//  Gurobi MIP によるRCSP 厳密解法ランナー。
//  ε 制約法で Pareto フロントを生成し、NSGA-II ヒューリスティック
//  の結果（FUN_ENC_*.txt）と比較可能な形式で出力する。
//
//  対象: j30 インスタンス（変数数 ≈ 5120、Gurobi で数秒〜数分で解ける）
//
//  出力ファイル (cmake-build-*/ 直下):
//    FUN_MIP_<prefix>_<cond>.txt      Pareto フロント (makespan cost)
//    SCHED_MIP_<prefix>_<cond>.txt    スケジュール詳細
//
//  使い方:
//    RCPSPMIPCpp                          # j30 全インスタンス × 8 条件
//    RCPSPMIPCpp j30.sm/j3011_1.sm        # 単一インスタンス
// ============================================================

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "problems/RCPSP_MIP_Solver.h"
#include "problems/RCPSP_MIP_Transform.h"
#include "problems/RCPSP_MIP_SolverP2Activity.h"
#include "problems/RCPSP_Conditions.h"

using namespace std;
namespace fs = std::filesystem;

// ============================================================
//  j30 インスタンスリスト（main.cpp と同一）
// ============================================================
static const vector<string> J30_INSTANCES = {
    "j30.sm/j3011_1.sm", "j30.sm/j3017_1.sm", "j30.sm/j3026_1.sm",
    "j30.sm/j3047_1.sm", "j30.sm/j309_1.sm",
};

// ============================================================
//  ユーティリティ
// ============================================================
static string toBaseNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

// NSGA の SCHED_ENC_*.txt（MIP の SCHED_MIP_*.txt と同一フォーマット）から
// 前線の先頭解の各ジョブ開始時刻を読み込み、MIP の warm start として使う。
// 失敗時（ファイル無し・ジョブ数不一致・前線空）は false を返す。
static bool loadWarmStartFromSched(const string &path, int nJobsExpected, vector<int> &outStart) {
    ifstream in(path);
    if (!in) return false;

    int nJ, nRes;
    if (!(in >> nJ >> nRes)) return false;
    if (nJ != nJobsExpected) return false;

    for (int j = 0; j < nJ; ++j) { int d; in >> d; }                 // durations
    for (int j = 0; j < nJ; ++j)
        for (int k = 0; k < nRes; ++k) { int v; in >> v; }           // demand
    for (int k = 0; k < nRes; ++k) { int v; in >> v; }                // capacity

    int tCap;
    if (!(in >> tCap)) return false;
    for (int k = 0; k < nRes; ++k)
        for (int t = 0; t < tCap; ++t) { int v; in >> v; }            // capacity_t

    int frontCount;
    if (!(in >> frontCount) || frontCount <= 0) return false;

    double ms, cost;
    if (!(in >> ms >> cost)) return false;
    outStart.assign(nJ, 0);
    for (int j = 0; j < nJ; ++j)
        if (!(in >> outStart[j])) return false;

    return true;
}

static string toCondTag(double rr, bool rv) {
    int rrInt = static_cast<int>(round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

// ============================================================
//  FUN / SCHED ファイル出力（NSGA-II と同形式）
// ============================================================
static void writeResults(
        const string                             &outPrefix,
        const vector<RCPSP_MIP_Solver::Result>   &front,
        const RCPSP_MIP_Solver                   &prob)
{
    // outPrefix = "{instance}_{cond}" なのでインスタンス名を先頭トークンから取得
    string instName = outPrefix.substr(0, outPrefix.find('_', outPrefix.find('_') + 1));
    const string mipDir = "results/FUN_MIP/" + instName + "/";
    fs::create_directories(mipDir);
    const string funPath   = mipDir + "FUN_MIP_"   + outPrefix + ".txt";
    const string schedPath = mipDir + "SCHED_MIP_" + outPrefix + ".txt";

    ofstream funFile  (funPath.c_str());
    ofstream schedFile(schedPath.c_str());

    int nJobs = prob.getNumJobs();
    int nRes  = prob.getNumResources();

    // SCHED ヘッダー
    schedFile << nJobs << " " << nRes << "\n";
    const auto &dur    = prob.getDurations();
    const auto &demand = prob.getDemand();
    const auto &cap    = prob.getCapacity();
    const auto &cap_t  = prob.getCapacityT();

    for (int j = 0; j < nJobs; ++j) {
        schedFile << dur[j];
        if (j + 1 < nJobs) schedFile << " ";
    }
    schedFile << "\n";
    for (int j = 0; j < nJobs; ++j) {
        for (int k = 0; k < nRes; ++k) {
            schedFile << demand[j][k];
            if (k + 1 < nRes) schedFile << " ";
        }
        schedFile << "\n";
    }
    for (int k = 0; k < nRes; ++k) {
        schedFile << cap[k];
        if (k + 1 < nRes) schedFile << " ";
    }
    schedFile << "\n";

    if (cap_t.empty() || cap_t[0].empty()) {
        schedFile << "0\n";
    } else {
        int T_cap = (int)cap_t[0].size();
        schedFile << T_cap << "\n";
        for (int k = 0; k < nRes; ++k) {
            for (int t = 0; t < T_cap; ++t) {
                schedFile << cap_t[k][t];
                if (t + 1 < T_cap) schedFile << " ";
            }
            schedFile << "\n";
        }
    }

    // Pareto フロント
    schedFile << front.size() << "\n";
    for (const auto &r : front) {
        funFile << r.makespan << " " << r.cost << "\n";
        schedFile << r.makespan << " " << r.cost;
        for (int j = 0; j < nJobs; ++j)
            schedFile << " " << (j < (int)r.startTimes.size() ? r.startTimes[j] : 0);
        schedFile << "\n";
    }

    // GAP ファイル（どの点が最適証明済みか・Stage2でどれだけmakespanが詰まったかを別途記録。
    // FUN_MIP_*.txt の形式は変更しない）
    const string gapPath = mipDir + "GAP_MIP_" + outPrefix + ".csv";
    ofstream gapFile(gapPath.c_str());
    gapFile << "makespan,cost,gap,optimal,solve_seconds,makespan_before_tighten\n";
    if (front.empty()) {
        gapFile << "# no incumbent found (SolCount=0)\n";
    } else {
        for (const auto &r : front) {
            gapFile << r.makespan << "," << fixed << setprecision(4) << r.cost << ","
                     << r.gap << "," << (r.optimal ? 1 : 0) << ","
                     << setprecision(2) << r.solveSeconds << ","
                     << setprecision(0) << r.makespanBeforeTighten << "\n";
        }
    }

    cout << "[FILES] " << funPath << " / " << schedPath << " / " << gapPath << "\n";
}

// ============================================================
//  P2/P3: instance のスナップショット取得
// ============================================================
static RCPSP_Instance snapshotInstance(const RCPSP_MIP_Solver &solver) {
    RCPSP_Instance inst;
    inst.nJobs      = solver.getNumJobs();
    inst.nRes       = solver.getNumResources();
    inst.duration   = solver.getDurations();
    inst.demand     = solver.getDemand();
    inst.capacity   = solver.getCapacity();
    inst.successors = solver.getSuccessors();
    inst.capacity_t = solver.getCapacityT();
    return inst;
}

// P3: 単位分割モデルのソルブ結果（unit job id 単位）を、元ジョブ単位の
// 実行時刻リストに集約する。P3 は時間軸を圧縮しないため、cost/makespan は
// 単位分割モデルの値をそのまま使える（元の時間軸・コスト表を一切変更していない）。
static vector<RealSchedule> aggregateP3(
        const vector<RCPSP_MIP_Solver::Result> &mipFront,
        const RCPSP_Instance                   &orig,
        const UnitSplitMapping                 &mapping)
{
    vector<RealSchedule> out;
    out.reserve(mipFront.size());
    int nOrig = orig.nJobs;

    for (const auto &r : mipFront) {
        RealSchedule rs;
        rs.feasible     = r.feasible;
        rs.makespan     = r.makespan;
        rs.cost         = r.cost;
        rs.optimal      = r.optimal;
        rs.gap          = r.gap;
        rs.solveSeconds = r.solveSeconds;
        rs.execTimes.assign(nOrig, {});

        for (int j = 0; j < nOrig; ++j) {
            int f = mapping.firstUnit[j];
            int l = mapping.lastUnit[j];
            for (int u = f; u <= l; ++u) {
                rs.execTimes[j].push_back(u < (int)r.startTimes.size() ? r.startTimes[u] : 0);
            }
            std::sort(rs.execTimes[j].begin(), rs.execTimes[j].end());
        }
        out.push_back(std::move(rs));
    }
    return out;
}

// P2: 圧縮軸モデルのソルブ結果を元の時間軸に展開する。makespan・cost は
// 圧縮時の値を使わず、展開後の実行時刻から元の時間軸（正本コスト表）で
// 再計算する（呼び出し元で RCPSP_MIP_Solver::restoreCostTable() 済みであること）。
static vector<RealSchedule> expandP2(
        const vector<RCPSP_MIP_Solver::Result> &mipFront,
        const RCPSP_Instance                   &orig,
        const TimeCompression                  &tc,
        const RCPSP_MIP_Solver                 &solver,
        int                                     horizonOriginal)
{
    vector<RealSchedule> out;
    out.reserve(mipFront.size());
    int nOrig = orig.nJobs;

    auto toReal = [&](int compIdx) {
        if (compIdx >= 0 && compIdx < (int)tc.compressedToOriginal.size())
            return tc.compressedToOriginal[compIdx];
        return compIdx;  // 範囲外は保険（通常起きない）
    };

    for (const auto &r : mipFront) {
        RealSchedule rs;
        rs.feasible     = r.feasible;
        rs.optimal      = r.optimal;
        rs.gap          = r.gap;
        rs.solveSeconds = r.solveSeconds;
        rs.execTimes.assign(nOrig, {});

        double realCost = 0.0;
        for (int j = 0; j < nOrig; ++j) {
            int d         = orig.duration[j];
            int compStart = j < (int)r.startTimes.size() ? r.startTimes[j] : 0;
            if (d <= 0) {
                rs.execTimes[j].push_back(toReal(compStart));
                continue;
            }
            for (int u = 0; u < d; ++u) {
                int realT = toReal(compStart + u);
                rs.execTimes[j].push_back(realT);
                realCost += solver.getSlotCost(j, realT, horizonOriginal);
            }
        }
        rs.makespan = rs.execTimes[nOrig - 1].empty() ? 1e9 : (double)rs.execTimes[nOrig - 1][0];
        rs.cost     = realCost;
        out.push_back(std::move(rs));
    }
    return out;
}

// ============================================================
//  P2/P3 用 FUN / SCHED / GAP 出力
//  （SCHED は単一開始時刻ではなく、ジョブごとの実行時刻リストを書く）
// ============================================================
static void writeResultsSplit(
        const string                &outPrefix,
        const string                &modeTag,      // "P2" | "P3"
        const vector<RealSchedule>  &front,
        const RCPSP_Instance        &orig)
{
    string instName = outPrefix.substr(0, outPrefix.find('_', outPrefix.find('_') + 1));
    const string dir = "results/FUN_MIP_" + modeTag + "/" + instName + "/";
    fs::create_directories(dir);
    const string funPath   = dir + "FUN_MIP_"   + modeTag + "_" + outPrefix + ".txt";
    const string schedPath = dir + "SCHED_MIP_" + modeTag + "_" + outPrefix + ".txt";
    const string gapPath   = dir + "GAP_MIP_"   + modeTag + "_" + outPrefix + ".csv";

    ofstream funFile  (funPath.c_str());
    ofstream schedFile(schedPath.c_str());

    int nJobs = orig.nJobs;
    int nRes  = orig.nRes;

    schedFile << nJobs << " " << nRes << "\n";
    for (int j = 0; j < nJobs; ++j) {
        schedFile << orig.duration[j];
        if (j + 1 < nJobs) schedFile << " ";
    }
    schedFile << "\n";
    for (int j = 0; j < nJobs; ++j) {
        for (int k = 0; k < nRes; ++k) {
            schedFile << orig.demand[j][k];
            if (k + 1 < nRes) schedFile << " ";
        }
        schedFile << "\n";
    }
    for (int k = 0; k < nRes; ++k) {
        schedFile << orig.capacity[k];
        if (k + 1 < nRes) schedFile << " ";
    }
    schedFile << "\n";

    if (orig.capacity_t.empty() || orig.capacity_t[0].empty()) {
        schedFile << "0\n";
    } else {
        int T_cap = (int)orig.capacity_t[0].size();
        schedFile << T_cap << "\n";
        for (int k = 0; k < nRes; ++k) {
            for (int t = 0; t < T_cap; ++t) {
                schedFile << orig.capacity_t[k][t];
                if (t + 1 < T_cap) schedFile << " ";
            }
            schedFile << "\n";
        }
    }

    // Pareto フロント（実行時刻リスト形式: 単一開始時刻では中断を表現できないため）
    schedFile << front.size() << "\n";
    for (const auto &rs : front) {
        funFile << rs.makespan << " " << rs.cost << "\n";
        schedFile << rs.makespan << " " << rs.cost << "\n";
        for (int j = 0; j < nJobs; ++j) {
            const auto &et = rs.execTimes[j];
            schedFile << et.size();
            for (int t : et) schedFile << " " << t;
            schedFile << "\n";
        }
    }

    const string gapHeader = "makespan,cost,gap,optimal,solve_seconds\n";
    ofstream gapFile(gapPath.c_str());
    gapFile << gapHeader;
    if (front.empty()) {
        gapFile << "# no incumbent found (SolCount=0)\n";
    } else {
        for (const auto &rs : front) {
            gapFile << rs.makespan << "," << fixed << setprecision(4) << rs.cost << ","
                     << rs.gap << "," << (rs.optimal ? 1 : 0) << ","
                     << setprecision(2) << rs.solveSeconds << "\n";
        }
    }

    cout << "[FILES] " << funPath << " / " << schedPath << " / " << gapPath << "\n";
}

// ============================================================
//  1インスタンス × 1条件 の MIP 実行（P2/P3: 単位分割・時間圧縮）
// ============================================================
static void runMIPSplit(const string &instanceFile,
                        double rr, bool rv,
                        const RCPSP_MIP_Solver::Config &cfg,
                        const string &modeTag)
{
    const string prefix = toBaseNoExt(instanceFile);
    const string ctag   = toCondTag(rr, rv);

    cout << "\n------------------------------------------------------------\n";
    cout << "[MIP " << modeTag << "] " << prefix << "  " << ctag
         << "  (timeLimit=" << cfg.timeLimit << "s/solve)\n";
    cout << "------------------------------------------------------------\n";

    RCPSP_MIP_Solver solver(instanceFile, rr, rv);
    RCPSP_Instance orig = snapshotInstance(solver);
    int T_original = solver.getHorizon();

    auto t0 = chrono::steady_clock::now();
    vector<RealSchedule> front;
    bool skipped = false;

    if (modeTag == "P3") {
        UnitSplitMapping mapping;
        RCPSP_Instance splitInst = buildUnitSplitInstance(orig, mapping);
        cout << "  [P3] nJobs: orig=" << orig.nJobs
             << "  unit-split=" << splitInst.nJobs
             << "  (ratio=" << fixed << setprecision(2)
             << (double)splitInst.nJobs / std::max(1, orig.nJobs) << "x)\n";

        solver.replaceInstance(splitInst);
        int Tsplit = solver.getHorizon();
        cout << "  [P3] horizon T: orig=" << T_original << "  after-split=" << Tsplit
             << (Tsplit == T_original ? "  (unchanged, OK)" : "  (!! MISMATCH !!)") << "\n";
        cout << "  [P3] approx binary vars (nJobs x T upper bound): "
             << (long long)splitInst.nJobs * Tsplit << "\n";

        auto mipFront = solver.solvePareto(cfg);
        front = aggregateP3(mipFront, orig, mapping);

    } else if (modeTag == "P2") {
        // ------------------------------------------------------------
        // P2（活動ごと圧縮）: NSGA 側 RCPSP_Problem_Splitting の定義
        //   γ_jkt=0 ⟺ capacityAtTime(k,t) < demand[j][k] → 中断してよい
        // に完全一致させた定式化。活動ごとに独立した実行可能時刻集合 A_j を持つため
        // 全活動で共有する単一圧縮軸では表現できず、RCPSP_MIP_SolverP2Activity に
        // 独立実装した（.miss_memory 参照: 旧「全体圧縮」実装は RR050_RV1 で NSGA 定義との
        // 乖離が最大44%あった。旧実装は下の "P2-legacy" 分岐として参照用に残してある）。
        // ------------------------------------------------------------
        auto scale = computeP2ActivityScale(solver);
        if (!scale.feasiblePrecheck) {
            cout << "  [P2-activity] job " << scale.infeasibleJob
                 << ": A_j サイズ不足のためどこにも置けない → このインスタンス/条件は実行不能\n";
            skipped = true;
        } else {
            int minM = *std::min_element(scale.mLen.begin(), scale.mLen.end());
            int maxM = *std::max_element(scale.mLen.begin(), scale.mLen.end());
            cout << "  [P2-activity] |A_j| range: [" << minM << ", " << maxM << "]"
                 << "  (orig T=" << T_original << ")"
                 << "  total binary vars=" << scale.totalBinaryVars
                 << "  (P1比 " << fixed << setprecision(2)
                 << (double)scale.totalBinaryVars / std::max(1LL, (long long)orig.nJobs * T_original)
                 << "x)\n";

            front = solveParetoP2ActivityWise(solver, cfg);
        }
    } else if (modeTag == "P2-legacy") {
        // ------------------------------------------------------------
        // 旧実装（全体圧縮）: 全資源が同時に容量0になる「休暇日」のみを時間軸から
        // 除去する。NSGA 側 P2 定義とは別モデル（ジョブ・資源ごとの個別容量不足は
        // 捉えない特殊ケース）と判明したため "P2" の既定からは外したが、参考・比較用に
        // 削除せず --split-mode P2-legacy として残す。出力は results/FUN_MIP_P2-legacy/。
        // ------------------------------------------------------------
        if (!rv) {
            cout << "  [P2-legacy] RV=off: 容量ゼロ期間が存在しないため P1 と同一。実行をスキップします。\n";
            skipped = true;
        } else {
            TimeCompression tc = buildTimeCompression(orig.capacity_t);
            cout << "  [P2-legacy] capacity_t: T_original=" << tc.T_original
                 << "  removed(all-res-zero)=" << tc.numRemoved
                 << "  T_compressed=" << tc.T_compressed << "\n";
            if (tc.numRemoved == 0) {
                cout << "  [P2-legacy] 全資源同時容量ゼロの期間が0件のため P1 と同一。実行をスキップします。\n";
                skipped = true;
            } else {
                RCPSP_Instance compressed = buildCompressedInstance(orig, tc);
                auto remapped = buildRemappedCostTable(solver, tc, T_original);

                solver.replaceInstance(compressed);
                RCPSP_MIP_Solver::overrideCostTable(remapped);

                auto mipFront = solver.solvePareto(cfg);

                RCPSP_MIP_Solver::restoreCostTable();
                front = expandP2(mipFront, orig, tc, solver, T_original);
            }
        }
    } else {
        cerr << "[ERROR] runMIPSplit: unknown split mode '" << modeTag << "'\n";
        return;
    }

    double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t0).count();

    if (skipped) {
        cout << "  [SKIP] " << modeTag << " skipped for " << prefix << "_" << ctag << "\n";
        return;
    }

    cout << "[MIP " << modeTag << " DONE] " << prefix << "_" << ctag
         << "  |PF|=" << front.size()
         << "  time=" << fixed << setprecision(1) << elapsed << "s";
    if (!front.empty())
        cout << "  (" << fixed << setprecision(2)
             << elapsed / std::max<size_t>(1, front.size()) << "s/point)";
    cout << "\n";

    writeResultsSplit(prefix + "_" + ctag, modeTag, front, orig);
}

// ============================================================
//  1インスタンス × 1条件 の MIP 実行
// ============================================================
static void runMIP(const string &instanceFile,
                   double rr, bool rv,
                   const RCPSP_MIP_Solver::Config &cfg)
{
    const string prefix = toBaseNoExt(instanceFile);
    const string ctag   = toCondTag(rr, rv);

    cout << "\n------------------------------------------------------------\n";
    cout << "[MIP] " << prefix << "  " << ctag
         << "  (timeLimit=" << cfg.timeLimit << "s/solve)\n";
    cout << "------------------------------------------------------------\n";

    RCPSP_MIP_Solver solver(instanceFile, rr, rv);

    auto t0     = chrono::steady_clock::now();
    auto front  = solver.solvePareto(cfg);
    double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t0).count();

    cout << "[MIP DONE] " << prefix << "_" << ctag
         << "  |PF|=" << front.size()
         << "  time=" << fixed << setprecision(1) << elapsed << "s\n";

    if (front.empty()) {
        cout << "  [WARN] Pareto front is empty (SolCount=0 at every eps point).\n";
    } else {
        // サマリ表示
        double minMs   = front[0].makespan;
        double minCost = front[0].cost;
        for (const auto &r : front) {
            minMs   = min(minMs,   r.makespan);
            minCost = min(minCost, r.cost);
        }
        cout << "  min_makespan=" << (int)minMs
             << "  min_cost=" << fixed << setprecision(1) << minCost
             << "  |PF|=" << front.size() << "\n";
    }

    writeResults(prefix + "_" + ctag, front, solver);
}

// ============================================================
//  1インスタンス × 8条件
// ============================================================
static void runInstance(const string &instanceFile,
                        const RCPSP_MIP_Solver::Config &cfg,
                        const string &splitMode = "P1")
{
    const string prefix = toBaseNoExt(instanceFile);

    using Cond = RCPSP_Cond;
    // 比較対象は RR<=0.50（6条件）。RR=0.75 は限界条件用に RCPSP_Conditions.h で別枠管理
    // （再有効化する場合は RCPSP_ALL_CONDITIONS() に差し替える）。
    const vector<Cond> conditions = RCPSP_STANDARD_CONDITIONS();

    cout << "\n============================================================\n";
    cout << " MIP Exact Solver  instance=" << instanceFile
         << "  split-mode=" << splitMode << "\n";
    cout << " timeLimit=" << cfg.timeLimit << "s/solve"
         << "  threads=" << cfg.threads
         << "  makespanSlack=" << cfg.makespanSlack << "\n";
    cout << "============================================================\n";

    if (splitMode != "P1") {
        for (const auto &c : conditions) {
            RCPSP_Problem::resetGlobalCostSeries();
            runMIPSplit(instanceFile, c.rr, c.rv, cfg, splitMode);
        }
        return;
    }

    // サマリ表ヘッダ
    cout << "\n" << left  << setw(16) << "Condition"
                 << right << setw(14) << "min_makespan"
                          << setw(14) << "min_cost"
                          << setw(10) << "|PF|"
                          << setw(10) << "time(s)" << "\n";
    cout << string(64, '-') << "\n";

    for (const auto &c : conditions) {
        const string ctag = toCondTag(c.rr, c.rv);
        RCPSP_Problem::resetGlobalCostSeries();

        RCPSP_MIP_Solver solver(instanceFile, c.rr, c.rv);

        auto t0    = chrono::steady_clock::now();
        auto front = solver.solvePareto(cfg);
        double elapsed = chrono::duration<double>(
                chrono::steady_clock::now() - t0).count();

        double minMs   = 1e9;
        double minCost = 1e9;
        for (const auto &r : front) {
            minMs   = min(minMs,   r.makespan);
            minCost = min(minCost, r.cost);
        }

        cout << left  << setw(16) << ctag
             << right << setw(14) << (minMs < 1e8 ? to_string((int)minMs) : "---")
                      << setw(14) << fixed << setprecision(1) << minCost
                      << setw(10) << (int)front.size()
                      << setw(10) << setprecision(1) << elapsed << "\n";

        writeResults(prefix + "_" + ctag, front, solver);
    }

    cout << string(64, '-') << "\n";
}

// ============================================================
//  --split-mode P1|P2|P3 の抽出（既定 P1 = 現行動作を変えない）
// ============================================================
static string parseSplitMode(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "--split-mode" && i + 1 < argc) return string(argv[i + 1]);
        if (a.rfind("--split-mode=", 0) == 0) return a.substr(string("--split-mode=").size());
    }
    return "P1";
}

// ============================================================
//  --time-limit=<秒> の抽出（1ソルブあたりの Gurobi TimeLimit。
//  既定 120.0 秒 = 現行動作を変えない。.miss_memory/036: P3 で 120s に
//  張り付いたまま未証明の点が多発した経緯があり、比較基準線として使う
//  MIP解の最適性証明を得るために CLI から延長できるようにする）
//
//  戻り値: パース成功なら true。省略時は outValue=120.0・true。
//  0以下・数値以外の文字混入・変換不能な文字列は false を返す
//  （呼び出し元でエラー終了させる）。
// ============================================================
static bool parseTimeLimit(int argc, char **argv, double &outValue) {
    string raw;
    bool specified = false;
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "--time-limit" && i + 1 < argc) { raw = argv[i + 1]; specified = true; break; }
        if (a.rfind("--time-limit=", 0) == 0) { raw = a.substr(string("--time-limit=").size()); specified = true; break; }
    }
    if (!specified) { outValue = 120.0; return true; }
    try {
        size_t pos = 0;
        double v = stod(raw, &pos);
        if (pos != raw.size()) return false;  // "600abc" のような末尾ゴミを拒否
        if (!(v > 0.0)) return false;          // 0以下・NaN を拒否（NaN比較は常にfalseなので安全側）
        outValue = v;
        return true;
    } catch (...) {
        return false;  // stod が変換不能（std::invalid_argument/out_of_range）
    }
}

// --split-mode / --time-limit の両方を1パスで取り除く
// （既存の引数の並び・既定動作は変えない。この2フラグはどの位置にあってもよい）
static vector<string> stripKnownFlags(int argc, char **argv) {
    vector<string> out;
    for (int i = 0; i < argc; ++i) {
        string a = argv[i];
        if (a == "--split-mode" || a == "--time-limit") { ++i; continue; }
        if (a.rfind("--split-mode=", 0) == 0) continue;
        if (a.rfind("--time-limit=", 0) == 0) continue;
        out.push_back(a);
    }
    return out;
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        string splitMode = parseSplitMode(argc, argv);
        if (splitMode != "P1" && splitMode != "P2" && splitMode != "P3" && splitMode != "P2-legacy") {
            cerr << "[ERROR] --split-mode must be P1, P2, P3, or P2-legacy (got '" << splitMode << "')\n";
            return 1;
        }
        double timeLimitArg = 120.0;
        if (!parseTimeLimit(argc, argv, timeLimitArg)) {
            cerr << "[ERROR] --time-limit must be a positive number of seconds\n";
            return 1;
        }
        vector<string> args = stripKnownFlags(argc, argv);
        int argc2 = (int)args.size();
        vector<char*> argv2;
        argv2.reserve(argc2);
        for (auto &s : args) argv2.push_back(&s[0]);
        argv = argv2.data();
        argc = argc2;

        // ---- ソルバー設定 ----
        RCPSP_MIP_Solver::Config cfg;
        cfg.timeLimit     = timeLimitArg;  // 既定 120s（2分）。--time-limit=<秒> で上書き可
        cfg.threads       = 4;
        cfg.verbose       = false;
        cfg.makespanSlack   = 40;      // C*_ms + 40 まで ε を探索
        cfg.mipGapTol       = 1e-4;
        cfg.epsBreakpoints  = 30;      // ε を 30 点に等間隔サンプリング（<=0 で従来の1刻み全走査）

        if (argc >= 4) {
            // 単一条件テスト実行: <instanceFile> <rr> <rv(0/1)> [epsBreakpoints] [warmStartSchedPath]
            const string instanceFile = argv[1];
            double rr = stod(argv[2]);
            bool   rv = stoi(argv[3]) != 0;
            if (argc >= 5) cfg.epsBreakpoints = stoi(argv[4]);
            cout << "\n[SINGLE COND] " << instanceFile
                 << "  rr=" << rr << " rv=" << rv
                 << " epsBreakpoints=" << cfg.epsBreakpoints
                 << " split-mode=" << splitMode << "\n";

            if (splitMode != "P1") {
                if (argc >= 6) {
                    cout << "[WARM START] not supported for split-mode " << splitMode
                         << "; ignoring warm start argument\n";
                }
                runMIPSplit(instanceFile, rr, rv, cfg, splitMode);
            } else {
                if (argc >= 6) {
                    RCPSP_MIP_Solver probe(instanceFile, rr, rv);
                    vector<int> warm;
                    if (loadWarmStartFromSched(argv[5], probe.getNumJobs(), warm)) {
                        cfg.warmStart = warm;
                        cout << "[WARM START] loaded from " << argv[5] << "\n";
                    } else {
                        cout << "[WARM START] failed to load from " << argv[5]
                             << "; continuing without warm start\n";
                    }
                }
                runMIP(instanceFile, rr, rv, cfg);
            }

        } else if (argc >= 2) {
            // 単一インスタンス実行（8条件）
            const string instanceFile = argv[1];
            cout << "\n[SINGLE] " << instanceFile << "  split-mode=" << splitMode << "\n";
            runInstance(instanceFile, cfg, splitMode);

        } else {
            // j30 全インスタンス一括実行
            const int total = static_cast<int>(J30_INSTANCES.size());
            cout << "\n[BATCH] MIP Exact Solver  " << total << " j30 instances"
                 << "  split-mode=" << splitMode << "\n";

            auto t_all = chrono::steady_clock::now();
            for (int i = 0; i < total; ++i) {
                const string &inst = J30_INSTANCES[i];
                cout << "\n[" << (i+1) << "/" << total << "] " << inst << "\n";
                runInstance(inst, cfg, splitMode);
            }

            double total_elapsed = chrono::duration<double>(
                    chrono::steady_clock::now() - t_all).count();
            cout << "\n[BATCH DONE]  total=" << total_elapsed << "s"
                 << "  (" << fixed << setprecision(2)
                 << total_elapsed / 3600.0 << "h)\n";
        }
        return 0;

    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
