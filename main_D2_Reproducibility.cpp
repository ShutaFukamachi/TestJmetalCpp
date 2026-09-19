// ============================================================
//  main_D2_Reproducibility.cpp  (D2Repro ターゲット)
//
//  診断1の再検証: 前回の main_D1_DecodeTest.cpp は全ジョブに同一スカラー
//  max_shift しか試しておらず「活動ごとに独立な max_shift ベクトル」を
//  検証していなかった（重大な不備）。本ハーネスは以下を厳密に判定する。
//
//  テストA（自然順序での厳密判定、掃引不要）:
//    デコーダは各活動について窓 [t_mak, t_mak+w_j] 内の最安の実行可能
//    スロットを選ぶ（同コストなら最早）。目標開始時刻 s*_j に置きたければ
//    w_j = s*_j - t_mak とすればよい。したがって活動 j が再現可能な条件は
//    「[t_mak, s*_j) に cost(s*_j) 以下の実行可能スロットが存在しない」こと。
//    これは実際のデコーダの argmin ループを [t_mak, s*_j] に限定して走らせ、
//    勝者が s*_j 自身かどうかを見るだけで厳密に判定できる（掃引不要）。
//
//    活動リストは MIP の開始時刻昇順（同時刻はジョブ番号昇順）に固定し、
//    各活動を s*_j に「強制配置」しながら usage テーブルを進める。
//
//  テストB（順序修復、★本題）:
//    ブロックされた活動 j のブロック要因スロット t' について、MIP の解で
//    その時刻を専有する活動 k を特定し、k を j より前に配置するよう
//    活動リストを入れ替える（先行制約は必ず守る）。テストAを再実行して
//    再現可能率が上がるか確認する。収束するまで反復する。
//
//  すべて既存デコーダのロジック（canPlace/computeJobCostAt と同一の
//  資源・コスト計算式）を「呼ぶだけ」で再実装せず、public アクセサ
//  （getCapacityT/getCapacity/computeJobCostAt/getDurations/getDemand/
//   getSuccessors/get_precedence_matrix）経由で参照する。デコーダ本体・
//  MIP は一切変更しない。
// ============================================================
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

#include "Solution.h"
#include "problems/RCPSP_Problem_MaxShift.h"
#include "problems/RCPSP_Problem_MaxShiftDur.h"

using namespace std;

static vector<int> parseIntList(const string &s) {
    vector<int> out;
    stringstream ss(s);
    string tok;
    while (getline(ss, tok, ',')) {
        if (!tok.empty()) out.push_back(stoi(tok));
    }
    return out;
}

// ---- capacityAt: RCPSP_Problem.cpp 内の static capacityAt() と同一ロジック ----
//      capacityAtTime() は protected のため、public な getCapacityT()/getCapacity()
//      経由で外部から同じ計算を再現する（デコーダ本体は変更しない）。
static int capAt(const RCPSP_Problem_MaxShiftDur &prob, int k, int t) {
    const auto &capT = prob.getCapacityT();
    if (!capT.empty() && k >= 0 && k < (int)capT.size() && t >= 0 && t < (int)capT[k].size())
        return capT[k][t];
    return prob.getCapacity()[k];
}

static bool canPlace(const RCPSP_Problem_MaxShiftDur &prob, int j, int t,
                      const vector<vector<int>> &usage, int T) {
    int d = prob.getDurations()[j];
    if (d <= 0) return true;
    if (t < 0 || t + d > T) return false;
    const auto &demand = prob.getDemand();
    for (int tau = t; tau < t + d; ++tau)
        for (int k = 0; k < prob.getNumResources(); ++k)
            if (usage[k][tau] + demand[j][k] > capAt(prob, k, tau)) return false;
    return true;
}

static void doPlace(const RCPSP_Problem_MaxShiftDur &prob, int j, int t,
                     vector<vector<int>> &usage) {
    int d = prob.getDurations()[j];
    if (d <= 0) return;
    const auto &demand = prob.getDemand();
    for (int tau = t; tau < t + d; ++tau)
        for (int k = 0; k < prob.getNumResources(); ++k)
            usage[k][tau] += demand[j][k];
}

struct JobResult {
    int job = -1;
    int est = 0, t_mak = 0, s_star = 0;
    bool reproducible = false;
    int block_t = -1;
    double block_cost = 0, target_cost = 0, diff = 0;
    int w_needed = -1;
};

// テストA: 与えられた順序 seq で、各ジョブを targetStart[j] に強制配置しながら進む。
static vector<JobResult> runTestA(RCPSP_Problem_MaxShiftDur &prob,
                                   const vector<int> &seq,
                                   const vector<int> &targetStart,
                                   const vector<vector<int>> &preds,
                                   int T) {
    int n = prob.getNumJobs();
    vector<vector<int>> usage(prob.getNumResources(), vector<int>(T, 0));
    vector<int> finish(n, 0);
    vector<JobResult> results(n);

    for (int j : seq) {
        int d = prob.getDurations()[j];
        int est = 0;
        for (int p : preds[j]) est = max(est, finish[p]);

        JobResult r;
        r.job = j; r.est = est; r.s_star = targetStart[j];

        if (d <= 0) {
            r.t_mak = est; r.reproducible = true; r.w_needed = 0;
            finish[j] = est;
            results[j] = r;
            continue;
        }

        int t_mak = est;
        while (t_mak < T && !canPlace(prob, j, t_mak, usage, T)) ++t_mak;
        if (t_mak >= T) t_mak = targetStart[j]; // 安全網（理論上到達しない）
        r.t_mak = t_mak;

        double targetCost = prob.computeJobCostAt(j, targetStart[j], T);
        r.target_cost = targetCost;

        int bestT = t_mak;
        double bestCost = canPlace(prob, j, t_mak, usage, T)
                               ? prob.computeJobCostAt(j, t_mak, T)
                               : std::numeric_limits<double>::infinity();
        for (int t = t_mak + 1; t <= targetStart[j]; ++t) {
            if (!canPlace(prob, j, t, usage, T)) continue;
            double c = prob.computeJobCostAt(j, t, T);
            if (c < bestCost - 1e-9) { bestCost = c; bestT = t; }
        }

        if (bestT == targetStart[j]) {
            r.reproducible = true;
            r.w_needed = targetStart[j] - t_mak;
        } else {
            r.reproducible = false;
            r.block_t = bestT;
            r.block_cost = bestCost;
            r.diff = targetCost - bestCost;
        }

        doPlace(prob, j, targetStart[j], usage);
        finish[j] = targetStart[j] + d;
        results[j] = r;
    }
    return results;
}

int main(int argc, char **argv) {
    try {
        string instanceFile, startsStr;
        double rr = 0.0;
        bool rv = false;
        int strategy = 4;
        int maxRounds = 10;
        double refMs = -1, refCost = -1;

        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "--instance" && i + 1 < argc) instanceFile = argv[++i];
            else if (a == "--rr" && i + 1 < argc) rr = stod(argv[++i]);
            else if (a == "--rv" && i + 1 < argc) rv = (stoi(argv[++i]) != 0);
            else if (a == "--strategy" && i + 1 < argc) strategy = stoi(argv[++i]);
            else if (a == "--starts" && i + 1 < argc) startsStr = argv[++i];
            else if (a == "--maxrounds" && i + 1 < argc) maxRounds = stoi(argv[++i]);
            else if (a == "--refms" && i + 1 < argc) refMs = stod(argv[++i]);
            else if (a == "--refcost" && i + 1 < argc) refCost = stod(argv[++i]);
        }
        if (instanceFile.empty() || startsStr.empty()) {
            cerr << "usage: D2Repro --instance <file> --rr <r> --rv <0/1> "
                    "--starts <csv, index=job#> [--strategy 4] [--maxrounds 10] "
                    "[--refms M --refcost C]\n";
            return 1;
        }

        vector<int> targetStart = parseIntList(startsStr);

        RCPSP_Problem_MaxShiftDur prob(instanceFile, strategy, rr, rv);
        const int n = prob.getNumJobs();
        if ((int)targetStart.size() != n) {
            cerr << "[ERROR] --starts size (" << targetStart.size()
                 << ") != n (" << n << ")\n";
            return 1;
        }

        // ホライゾン T（デコーダと同一計算式: getHorizon() は MaxShift 系列で public）
        const int T = prob.getHorizon();

        // 先行者リスト
        const auto &succ = prob.getSuccessors();
        vector<vector<int>> preds(n);
        for (int j = 0; j < n; ++j)
            for (int s : succ[j])
                if (s >= 0 && s < n) preds[s].push_back(j);

        // 先行制約の推移閉包（テストBの合法性チェック用）
        auto precMat = prob.get_precedence_matrix(); // precMat[i][j]==1: i は j より前

        // ---- 自然順序: 開始時刻昇順（同時刻はジョブ番号昇順） ----
        vector<int> seq(n);
        for (int i = 0; i < n; ++i) seq[i] = i;
        sort(seq.begin(), seq.end(), [&](int a, int b) {
            if (targetStart[a] != targetStart[b]) return targetStart[a] < targetStart[b];
            return a < b;
        });

        // ================= テストA (初回) =================
        vector<JobResult> resA = runTestA(prob, seq, targetStart, preds, T);
        int reproCountA = 0;
        for (auto &r : resA) if (r.reproducible) ++reproCountA;

        cout << "=== TEST A (natural start-time order) ===\n";
        cout << "job  s_star  est  t_mak  reproducible  w_needed  block_t  block_cost  target_cost  diff\n";
        for (auto &r : resA) {
            cout << r.job << " " << r.s_star << " " << r.est << " " << r.t_mak << " "
                 << (r.reproducible ? "YES" : "no") << " " << r.w_needed << " "
                 << r.block_t << " " << r.block_cost << " " << r.target_cost << " " << r.diff << "\n";
        }
        cout << "[SUMMARY_A] reproducible=" << reproCountA << " / " << n
             << " rate=" << (100.0 * reproCountA / n) << "%\n";

        // ================= テストB (順序修復) =================
        vector<int> curSeq = seq;
        vector<JobResult> curRes = resA;
        int round = 0;
        int swapsTotal = 0;
        for (round = 1; round <= maxRounds; ++round) {
            // 現在の順序での位置
            vector<int> pos(n);
            for (int i = 0; i < (int)curSeq.size(); ++i) pos[curSeq[i]] = i;

            bool anySwap = false;
            for (auto &r : curRes) {
                if (r.reproducible || r.block_t < 0) continue;
                int j = r.job;
                int tprime = r.block_t;
                // MIP の解で tprime を専有する活動 k を探す
                for (int k = 0; k < n; ++k) {
                    if (k == j) continue;
                    int dk = prob.getDurations()[k];
                    if (dk <= 0) continue;
                    int sk = targetStart[k];
                    if (!(sk <= tprime && tprime < sk + dk)) continue; // k の区間が t' を覆うか
                    // k は j より前に配置済みか？
                    if (pos[k] < pos[j]) continue; // 既に前にある→スワップ不要
                    // 先行制約: j が k より前に来る必要がある(j→...→k)なら不可
                    if (precMat[j][k]) continue; // 合法でない
                    // k を j の直前へ移動
                    int pk = pos[k], pj = pos[j];
                    int val = curSeq[pk];
                    curSeq.erase(curSeq.begin() + pk);
                    // 削除後の j の位置を再計算
                    int newPj = 0;
                    for (int i = 0; i < (int)curSeq.size(); ++i) if (curSeq[i] == j) { newPj = i; break; }
                    curSeq.insert(curSeq.begin() + newPj, val);
                    anySwap = true;
                    swapsTotal++;
                    cout << "[TESTB round " << round << "] move job " << k
                         << " before job " << j << " (occupies t'=" << tprime << ")\n";
                    break; // このラウンドでは1件処理したら位置情報が古くなるので抜ける
                }
                if (anySwap) break;
            }

            if (!anySwap) {
                cout << "[TESTB round " << round << "] no legal swap found; converged.\n";
                break;
            }
            curRes = runTestA(prob, curSeq, targetStart, preds, T);
        }

        int reproCountB = 0;
        for (auto &r : curRes) if (r.reproducible) ++reproCountB;
        cout << "=== TEST B (after order repair, " << (round - 1) << " rounds, "
             << swapsTotal << " swaps) ===\n";
        for (auto &r : curRes) {
            cout << r.job << " " << r.s_star << " " << r.est << " " << r.t_mak << " "
                 << (r.reproducible ? "YES" : "no") << " " << r.w_needed << " "
                 << r.block_t << " " << r.block_cost << " " << r.target_cost << " " << r.diff << "\n";
        }
        cout << "[SUMMARY_B] reproducible=" << reproCountB << " / " << n
             << " rate=" << (100.0 * reproCountB / n) << "%\n";

        // ================= バリデーション（全ジョブ再現可能なら実デコーダで確認） =================
        if (reproCountB == n && refMs > 0) {
            Solution sol(&prob);
            auto &vars = sol.getVars();
            for (int i = 0; i < n; ++i) vars[i] = curSeq[i];
            double alpha = prob.getAlpha();
            for (auto &r : curRes) {
                int j = r.job;
                int d = prob.getDurations()[j];
                double eff = alpha * d;
                int rho = 0;
                if (eff > 1e-9) {
                    rho = (int)lround((double)r.w_needed / eff * RCPSP_Problem_MaxShiftDur::R);
                    rho = max(0, min((int)RCPSP_Problem_MaxShiftDur::R, rho));
                }
                vars[n + j] = rho;
            }
            prob.evaluate(&sol);
            double ms = sol.getObjective(0), cost = sol.getObjective(1);
            cout << "[VALIDATION] decoded ms=" << ms << " cost=" << cost
                 << "  (target ms=" << refMs << " cost=" << refCost << ")"
                 << "  match=" << ((abs(ms - refMs) < 1e-6 && abs(cost - refCost) < 1e-3) ? "YES" : "no") << "\n";
        }

        return 0;
    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
