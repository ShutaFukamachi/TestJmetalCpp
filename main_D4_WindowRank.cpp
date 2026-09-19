// ============================================================
//  main_D4_WindowRank.cpp  (D4WindowRank ターゲット)
//
//  ゼミ指摘1・2に答えるための計測ツール。
//  MIP のパレート解（results/FUN_MIP/<inst>/SCHED_MIP_*.txt、読み取り専用）を
//  入力に、提案手法デコーダ（RCPSP_Problem_MaxShiftDur）が各活動をその開始
//  時刻に置けるかを「窓幅」と「コスト順位」の2軸で計測し、CSV に落とす。
//
//  すべて既存デコーダのロジック（canPlace/computeJobCostAt と同一の資源・
//  コスト計算式、getAlpha/getBeta と同一の窓幅計算式）を public アクセサ
//  経由で「呼ぶだけ」で再実装せず参照する。main_D2_Reproducibility.cpp の
//  runTestA をベースにしている（同じ強制配置・自然順序の考え方）。
//  デコーダ本体（RCPSP_Problem_MaxShiftDur）・MIP（RCPSP_MIP_Solver）は
//  一切変更しない。
//
//  読み取り専用: results/FUN_MIP/, results/FUN_ENC/, costs/costs_*.csv
//  書き込み先:   results/ANALYSIS_D4/ のみ
// ============================================================
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "Solution.h"
#include "problems/RCPSP_Problem_MaxShift.h"
#include "problems/RCPSP_Problem_MaxShiftDur.h"
#include "problems/RCPSP_Conditions.h"

using namespace std;
namespace fs = std::filesystem;

// ============================================================
//  デコーダ本体と同一ロジックの再現（main_D2_Reproducibility.cpp と同一実装。
//  capacityAtTime()/canPlace は protected のため、public な
//  getCapacityT()/getCapacity()/getDurations()/getDemand() 経由で再現する）
// ============================================================
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

// ============================================================
//  SCHED_MIP_*.txt パーサ（main_MIP_RCPSP.cpp::writeResults と同一形式。
//  RR=0かつRV=0のときは capacity_t が空のため T_cap=0・以降の行なし。
//  空白/改行非依存で >> により逐次読みするので、そのケースも自然に処理できる）
// ============================================================
struct SchedMipData {
    int nJobs = 0, nRes = 0;
    vector<int> duration;
    vector<vector<int>> demand;
    vector<int> capacity;
    int T_cap = 0;
    vector<vector<int>> capacity_t;
    int frontCount = 0;
    vector<double> frontMakespan, frontCost;
    vector<vector<int>> frontStarts;  // [frontIdx][job]
};

static bool parseSchedMip(const string &path, SchedMipData &out) {
    ifstream in(path);
    if (!in) return false;

    if (!(in >> out.nJobs >> out.nRes)) return false;

    out.duration.resize(out.nJobs);
    for (auto &d : out.duration) if (!(in >> d)) return false;

    out.demand.assign(out.nJobs, vector<int>(out.nRes));
    for (int j = 0; j < out.nJobs; ++j)
        for (int k = 0; k < out.nRes; ++k)
            if (!(in >> out.demand[j][k])) return false;

    out.capacity.resize(out.nRes);
    for (auto &c : out.capacity) if (!(in >> c)) return false;

    if (!(in >> out.T_cap)) return false;
    out.capacity_t.assign(out.nRes, vector<int>(out.T_cap));
    for (int k = 0; k < out.nRes; ++k)
        for (int t = 0; t < out.T_cap; ++t)
            if (!(in >> out.capacity_t[k][t])) return false;

    if (!(in >> out.frontCount)) return false;
    out.frontMakespan.resize(out.frontCount);
    out.frontCost.resize(out.frontCount);
    out.frontStarts.assign(out.frontCount, vector<int>(out.nJobs));
    for (int i = 0; i < out.frontCount; ++i) {
        if (!(in >> out.frontMakespan[i] >> out.frontCost[i])) return false;
        for (int j = 0; j < out.nJobs; ++j)
            if (!(in >> out.frontStarts[i][j])) return false;
    }
    return true;
}

// ============================================================
//  ジョブごとの計測結果
// ============================================================
struct D4JobResult {
    int    job = -1, duration = 0, est = 0, t_mak = 0, s_star = 0;
    int    w_needed = 0;
    int    n_feasible_in_span = 0, n_cheaper = 0, n_tie_earlier = 0, rank = 0;
    double target_cost = 0.0, best_cost = 0.0, cost_diff = 0.0;
    bool   reproducible = false;
    double alpha = 0.0, beta = 0.0, eff = 0.0, rho_needed = 0.0;
};

// ============================================================
//  計測本体: main_D2_Reproducibility.cpp::runTestA をベースに、
//  厳密な rank（n_cheaper/n_tie_earlier）と窓幅充足診断
//  （eff/rho_needed）を追加したもの。
//
//  活動リストは s* 昇順（同時刻はジョブ番号昇順）に固定し、各活動を
//  必ず s*_j に強制配置しながら usage テーブルを進める。
// ============================================================
static vector<D4JobResult> runWindowRank(
        RCPSP_Problem_MaxShiftDur &prob,
        const vector<int> &seq,
        const vector<int> &targetStart,
        const vector<vector<int>> &preds,
        int T, double alpha, double beta)
{
    int n = prob.getNumJobs();
    vector<vector<int>> usage(prob.getNumResources(), vector<int>(T, 0));
    vector<int> finish(n, 0);
    vector<D4JobResult> results(n);

    for (int j : seq) {
        int d = prob.getDurations()[j];
        int est = 0;
        for (int p : preds[j]) est = max(est, finish[p]);

        D4JobResult r;
        r.job = j; r.duration = d; r.est = est; r.s_star = targetStart[j];
        r.alpha = alpha; r.beta = beta;

        if (d <= 0) {
            // ダミー活動（所要時間0）: 窓の概念が適用されないため自明に再現可能
            r.t_mak = est; r.w_needed = 0;
            r.n_feasible_in_span = 1; r.n_cheaper = 0; r.n_tie_earlier = 0; r.rank = 1;
            r.reproducible = true;
            r.target_cost = 0.0; r.best_cost = 0.0; r.cost_diff = 0.0;
            r.eff = 0.0; r.rho_needed = 0.0;
            finish[j] = est;
            results[j] = r;
            continue;
        }

        // ---- t_mak: est 以降で canPlace が真になる最早時刻 ----
        int t_mak = est;
        while (t_mak < T && !canPlace(prob, j, t_mak, usage, T)) ++t_mak;
        if (t_mak >= T) t_mak = targetStart[j];  // 安全網（理論上到達しない、D2Reproと同一）
        r.t_mak = t_mak;
        r.w_needed = targetStart[j] - t_mak;

        double targetCost = prob.computeJobCostAt(j, targetStart[j], T);
        r.target_cost = targetCost;

        // ---- S = [t_mak, s*_j] 上を1回走査して rank と argmin 勝者を同時に求める ----
        int nFeasible = 0, nCheaper = 0, nTieEarlier = 0;
        bool haveBest = false;
        double bestCost = std::numeric_limits<double>::infinity();

        for (int t = t_mak; t <= targetStart[j]; ++t) {
            if (!canPlace(prob, j, t, usage, T)) continue;
            ++nFeasible;
            double c = prob.computeJobCostAt(j, t, T);

            if (c < targetCost - 1e-9) {
                ++nCheaper;
            } else if (t < targetStart[j] && std::fabs(c - targetCost) <= 1e-9) {
                ++nTieEarlier;
            }

            // デコーダの argmin タイブレーク（同コストなら最早＝厳密未満のみ更新）を再現
            if (!haveBest || c < bestCost - 1e-9) { bestCost = c; haveBest = true; }
        }
        r.n_feasible_in_span = nFeasible;
        r.n_cheaper          = nCheaper;
        r.n_tie_earlier       = nTieEarlier;
        r.rank                = 1 + nCheaper + nTieEarlier;
        r.reproducible         = (r.rank == 1);
        r.best_cost            = haveBest ? bestCost : targetCost;
        r.cost_diff             = targetCost - r.best_cost;

        // ---- 窓幅充足診断: RCPSP_Problem_MaxShiftDur.cpp の窓幅計算式そのもの ----
        //   eff_j = max(alpha * d_j, beta * T);  win_j = round(rho_j * eff_j)
        double eff = std::max(alpha * d, beta * (double)T);
        r.eff = eff;
        r.rho_needed = (eff > 1e-9)
                ? ((double)r.w_needed / eff)
                : (r.w_needed > 0 ? std::numeric_limits<double>::infinity() : 0.0);

        doPlace(prob, j, targetStart[j], usage);
        finish[j] = targetStart[j] + d;
        results[j] = r;
    }
    return results;
}

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

static string toCondTag(double rr, bool rv) {
    int rrInt = static_cast<int>(round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

// 正本コスト表 costs/costs_<prefix>.csv のヘッダ（R T）だけを読む（読み取り専用）。
// .miss_memory/024: コスト表の horizon（T_cost）がスケジュール horizon（T）より
// 短い既知の未解決課題があり、t >= T_cost はコスト表末尾の値にクランプされる
// （RCPSP_Problem.cpp::resourceCost の `if (t >= COST_T) t = COST_T - 1;`）。
// D4 の rank/cost_diff はこの区間で「本当に同コストだから」ではなく「クランプされて
// いるから」タイになりうるため、影響範囲を診断的に報告する（CSVスキーマは変更しない）。
static int readCostTableHorizon(const string &prefix) {
    ifstream in("costs/costs_" + prefix + ".csv");
    if (!in) return -1;
    int r = 0, t = 0;
    if (!(in >> r >> t)) return -1;
    return t;
}

static double percentile(vector<double> v, double p) {
    if (v.empty()) return 0.0;
    sort(v.begin(), v.end());
    double idx = p * (double)(v.size() - 1);
    size_t lo = (size_t)floor(idx), hi = (size_t)ceil(idx);
    if (lo == hi) return v[lo];
    double frac = idx - (double)lo;
    return v[lo] * (1.0 - frac) + v[hi] * frac;
}

// ============================================================
//  1インスタンス × 1条件 の処理
// ============================================================
static bool processCondition(const string &instanceFile, double rr, bool rv,
                              int strategy, double beta)
{
    const string prefix  = toBaseNoExt(instanceFile);
    const string condTag = toCondTag(rr, rv);
    const string schedPath = "results/FUN_MIP/" + prefix + "/SCHED_MIP_" + prefix + "_" + condTag + ".txt";

    cout << "\n------------------------------------------------------------\n";
    cout << "[D4] " << prefix << "  " << condTag
         << "  strategy=" << strategy << "  beta=" << beta << "\n";
    cout << "------------------------------------------------------------\n";

    SchedMipData sched;
    if (!fs::exists(schedPath)) {
        cout << "  [SKIP] SCHED file not found: " << schedPath << "\n";
        return false;
    }
    if (!parseSchedMip(schedPath, sched)) {
        cerr << "  [ERROR] failed to parse " << schedPath << "\n";
        return false;
    }
    if (sched.frontCount <= 0) {
        cout << "  [SKIP] front is empty (frontCount=0): " << schedPath << "\n";
        return false;
    }

    // ---- デコーダ本体を .sm から再構築（001の決定論的シードにより
    //      MIP出力と同一の capacity_t が再現される想定。次で検証する） ----
    RCPSP_Problem_MaxShiftDur prob(instanceFile, strategy, rr, rv);
    prob.setBeta(beta);
    const int n = prob.getNumJobs();
    const int T = prob.getHorizon();
    const double alpha = prob.getAlpha();

    const int costT = readCostTableHorizon(prefix);
    if (costT > 0 && costT < T) {
        cout << "  [WARN] cost table horizon (" << costT << ") < schedule horizon (" << T
             << ") -- known unresolved issue (.miss_memory/024). Costs for t>=" << costT
             << " are clamped to the last table value, so rank/cost_diff in that range"
                " may reflect clamping rather than a genuine cost tie.\n";
    }

    // ---- クロス検証: SCHED_MIP ファイルの instance データと .sm 再構築が一致するか
    //      （.miss_memory/017: 系列間でコスト表・容量プロファイルが食い違うと
    //       比較が無効になる。ここで食い違えば即座に中断し、原因不明のまま
    //       測定を進めない） ----
    bool mismatch = false;
    string reason;
    if (n != sched.nJobs) { mismatch = true; reason = "nJobs"; }
    if (!mismatch) {
        const auto &pd = prob.getDurations();
        for (int j = 0; j < n; ++j) if (pd[j] != sched.duration[j]) { mismatch = true; reason = "duration"; break; }
    }
    if (!mismatch) {
        const auto &dem = prob.getDemand();
        for (int j = 0; j < n && !mismatch; ++j)
            for (int k = 0; k < sched.nRes; ++k)
                if (dem[j][k] != sched.demand[j][k]) { mismatch = true; reason = "demand"; break; }
    }
    if (!mismatch) {
        const auto &cap = prob.getCapacity();
        for (int k = 0; k < sched.nRes; ++k) if (cap[k] != sched.capacity[k]) { mismatch = true; reason = "capacity"; break; }
    }
    if (!mismatch && sched.T_cap > 0) {
        const auto &capT = prob.getCapacityT();
        if ((int)capT.size() != sched.nRes || capT.empty() || (int)capT[0].size() != sched.T_cap) {
            mismatch = true; reason = "capacity_t shape";
        } else {
            for (int k = 0; k < sched.nRes && !mismatch; ++k)
                for (int t = 0; t < sched.T_cap; ++t)
                    if (capT[k][t] != sched.capacity_t[k][t]) { mismatch = true; reason = "capacity_t values"; break; }
        }
    }
    if (mismatch) {
        cerr << "  [ERROR] instance data mismatch between SCHED_MIP file and .sm reconstruction ("
             << reason << ")\n"
             << "          may match the .miss_memory/017 pattern (cross-series cost-table/"
                "capacity-profile mismatch). Skipping this condition.\n";
        return false;
    }

    const auto &succ = prob.getSuccessors();
    vector<vector<int>> preds(n);
    for (int j = 0; j < n; ++j)
        for (int s : succ[j])
            if (s >= 0 && s < n) preds[s].push_back(j);

    fs::create_directories("results/ANALYSIS_D4");
    const string csvPath = "results/ANALYSIS_D4/D4_" + prefix + "_" + condTag + ".csv";
    ofstream csv(csvPath);
    csv << "instance,rr,rv,front_idx,makespan,cost,job,duration,est,t_mak,s_star,"
           "w_needed,n_feasible_in_span,n_cheaper,n_tie_earlier,rank,"
           "target_cost,best_cost,cost_diff,reproducible,"
           "alpha,beta,eff,rho_needed\n";
    csv << fixed;

    // ---- 集計用（ダミー活動 duration<=0 は窓の概念が無いため統計から除外） ----
    vector<int> rankCounts(4, 0);  // [0]=rank1 [1]=rank2 [2]=rank3 [3]=rank>=4
    vector<double> costDiffForRankGe2;
    vector<double> wNeededAll;
    vector<double> rhoNeededAll;
    int rhoOver1Count = 0;
    int fullyReproducibleFronts = 0;
    int costClampedRows = 0;  // s_star >= costT（コスト表クランプの影響を受けうる行）

    for (int fi = 0; fi < sched.frontCount; ++fi) {
        const vector<int> &targetStart = sched.frontStarts[fi];

        vector<int> seq(n);
        for (int i = 0; i < n; ++i) seq[i] = i;
        sort(seq.begin(), seq.end(), [&](int a, int b) {
            if (targetStart[a] != targetStart[b]) return targetStart[a] < targetStart[b];
            return a < b;
        });

        vector<D4JobResult> res = runWindowRank(prob, seq, targetStart, preds, T, alpha, beta);

        bool allRepro = true;
        for (const auto &r : res) {
            csv << prefix << "," << rr << "," << (rv ? 1 : 0) << "," << fi << ","
                << sched.frontMakespan[fi] << "," << setprecision(4) << sched.frontCost[fi] << ","
                << r.job << "," << r.duration << "," << r.est << "," << r.t_mak << "," << r.s_star << ","
                << r.w_needed << "," << r.n_feasible_in_span << "," << r.n_cheaper << "," << r.n_tie_earlier << ","
                << r.rank << "," << setprecision(4) << r.target_cost << "," << r.best_cost << "," << r.cost_diff << ","
                << (r.reproducible ? 1 : 0) << ","
                << setprecision(4) << r.alpha << "," << r.beta << "," << r.eff << "," << r.rho_needed << "\n";

            if (!r.reproducible) allRepro = false;

            if (r.duration > 0) {
                int idx = (r.rank <= 1) ? 0 : (r.rank == 2 ? 1 : (r.rank == 3 ? 2 : 3));
                rankCounts[idx]++;
                if (r.rank >= 2) costDiffForRankGe2.push_back(r.cost_diff);
                wNeededAll.push_back((double)r.w_needed);
                double rn = std::isinf(r.rho_needed) ? 1e6 : r.rho_needed;  // 集計用に有限値へ丸める
                rhoNeededAll.push_back(rn);
                if (r.rho_needed > 1.0) ++rhoOver1Count;
                if (costT > 0 && r.s_star >= costT) ++costClampedRows;
            }
        }
        if (allRepro) ++fullyReproducibleFronts;
    }

    long long totalJobRows = 0;
    for (int c : rankCounts) totalJobRows += c;

    cout << "  [FILE] " << csvPath << "\n";
    cout << "  front points K=" << sched.frontCount << "\n";
    cout << "  --- rank distribution (duration>0 jobs, N=" << totalJobRows << ") ---\n";
    const char *labels[4] = {"rank=1", "rank=2", "rank=3", "rank>=4"};
    for (int i = 0; i < 4; ++i) {
        double pct = totalJobRows > 0 ? 100.0 * rankCounts[i] / (double)totalJobRows : 0.0;
        cout << "    " << labels[i] << ": " << rankCounts[i] << "  (" << fixed << setprecision(1) << pct << "%)\n";
    }

    if (!costDiffForRankGe2.empty()) {
        double sum = 0, mx = -1e18;
        for (double v : costDiffForRankGe2) { sum += v; mx = max(mx, v); }
        double mean = sum / costDiffForRankGe2.size();
        double med = percentile(costDiffForRankGe2, 0.5);
        cout << "  --- cost_diff for rank>=2 (N=" << costDiffForRankGe2.size() << ") ---\n";
        cout << "    mean=" << setprecision(2) << mean << "  median=" << med << "  max=" << mx << "\n";
    } else {
        cout << "  --- cost_diff for rank>=2: N=0 (全ジョブ rank=1) ---\n";
    }

    if (!wNeededAll.empty()) {
        double med = percentile(wNeededAll, 0.5);
        double p90 = percentile(wNeededAll, 0.9);
        double mx = *max_element(wNeededAll.begin(), wNeededAll.end());
        cout << "  --- w_needed distribution ---\n";
        cout << "    median=" << setprecision(1) << med << "  p90=" << p90 << "  max=" << mx << "\n";
    }

    if (!rhoNeededAll.empty()) {
        double med = percentile(rhoNeededAll, 0.5);
        double p90 = percentile(rhoNeededAll, 0.9);
        double mx = *max_element(rhoNeededAll.begin(), rhoNeededAll.end());
        double pct = 100.0 * rhoOver1Count / (double)rhoNeededAll.size();
        cout << "  --- rho_needed distribution ---\n";
        cout << "    median=" << setprecision(3) << med << "  p90=" << p90 << "  max=" << mx << "\n";
        cout << "    rho_needed>1.0: " << rhoOver1Count << " / " << rhoNeededAll.size()
             << "  (" << setprecision(1) << pct << "%)\n";
    }

    cout << "  --- fully reproducible fronts (all jobs rank=1): "
         << fullyReproducibleFronts << " / " << sched.frontCount << " ---\n";

    if (costT > 0 && costT < T && totalJobRows > 0) {
        double pct = 100.0 * costClampedRows / (double)totalJobRows;
        cout << "  --- cost-table-horizon clamping exposure (.miss_memory/024) ---\n";
        cout << "    s_star >= " << costT << " (cost table horizon): "
             << costClampedRows << " / " << totalJobRows
             << "  (" << fixed << setprecision(1) << pct << "%) -- "
                "rank/cost_diff for these rows may be affected by clamping to the last cost value\n";
    }

    return true;
}

// ============================================================
//  main
// ============================================================
int main(int argc, char **argv) {
    try {
        string instanceFile;
        double rr = 0.0;
        bool   rv = false;
        int    strategy = 4;
        double beta = 0.5;
        bool   runAll = false;
        bool   haveRR = false, haveRV = false;

        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "--instance" && i + 1 < argc) instanceFile = argv[++i];
            else if (a == "--rr" && i + 1 < argc) { rr = stod(argv[++i]); haveRR = true; }
            else if (a == "--rv" && i + 1 < argc) { rv = (stoi(argv[++i]) != 0); haveRV = true; }
            else if (a == "--strategy" && i + 1 < argc) strategy = stoi(argv[++i]);
            else if (a == "--beta" && i + 1 < argc) beta = stod(argv[++i]);
            else if (a == "--all") runAll = true;
        }

        if (instanceFile.empty()) {
            cerr << "usage: D4WindowRank --instance <file> --rr <r> --rv <0/1> "
                    "[--strategy 4] [--beta 0.5]\n"
                    "       D4WindowRank --instance <file> --all [--strategy 4] [--beta 0.5]\n";
            return 1;
        }

        if (runAll) {
            const auto &conditions = RCPSP_STANDARD_CONDITIONS();
            int ran = 0, skipped = 0;
            for (const auto &c : conditions) {
                if (processCondition(instanceFile, c.rr, c.rv, strategy, beta)) ++ran;
                else ++skipped;
            }
            cout << "\n[D4 ALL DONE] ran=" << ran << " skipped=" << skipped
                 << " / " << conditions.size() << "\n";
        } else {
            if (!haveRR || !haveRV) {
                cerr << "[ERROR] specify both --rr and --rv, or use --all\n";
                return 1;
            }
            if (!processCondition(instanceFile, rr, rv, strategy, beta)) {
                return 1;
            }
        }

        return 0;
    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
