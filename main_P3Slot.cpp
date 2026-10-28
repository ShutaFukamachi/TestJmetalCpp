// ============================================================
//  main_P3Slot.cpp  —  P3（任意中断）の時刻別実行 MIP（2026-10-01 ゼミ用、指摘8）
//
//  旧 P3（活動を単位活動の鎖に分解、.miss_memory/036）は変数が P1 の約5倍に膨らみ、
//  timeLimit=120s では最適性どころか P1 より悪い暫定解しか出なかった。
//  ここでは「各時刻にその活動を実行するか」を直接変数にする:
//
//    y[j][t] ∈ {0,1} : 活動 j を時刻 t に 1 単位実行する
//    f[j][t] ∈ {0,1} : 活動 j が時刻 t の開始時点で完了している（t=0..E）
//
//    Σ_t y[j][t] = d_j
//    d_j f[j][t] <= Σ_{τ<t} y[j][τ]              （完了していなければ f=0）
//    f[j][t] <= f[j][t+1]                        （完了は取り消せない）
//    y[s][t] <= f[i][t]   （i→s の先行関係：s を実行する時点で i は完了済み）
//    Σ_j r_jk y[j][t] <= R_k(t)
//    ε 制約：t >= ε の y は作らない（その時刻以降に作業しない＝メイクスパン <= ε）
//    目的  ：Σ c_j(t) y[j][t]、c_j(t) = Σ_k cost_k(t) r_jk（resourceCost と同じクランプ規則）
//
//  前線の作り方は P1（RCPSP_MIP_Solver）と同じ ε 制約法：
//    lb = 最小メイクスパン、ub = 最小コスト解を同コストのまま工期最小化した値、
//    [lb, ub] を 30 点に等分し、各 ε でコスト最小 → 同コストのまま工期を締める。
//
//  使い方: P3Slot <instance.sm> <rr> <rv> [epsBreakpoints=30] [--cost-dir=costs_ext] [--time-limit=120]
//  出力  : results/EXT_COST/FUN_MIP_P3SLOT/<prefix>/{FUN,GAP}_MIP_P3SLOT_<prefix>_<cond>.*
//          （拡張コスト表専用の実験ツール。公式 results/FUN_MIP/ には決して書かない）
// ============================================================
#include "problems/RCPSP_MIP_Solver.h"
#include "util/KeepAwake.h"
#include "gurobi_c++.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace std;
namespace fs = std::filesystem;

struct Data {
    int n = 0, R = 0, T = 0;
    vector<int> d;
    vector<vector<int>> dem, succ, cap;   // cap[k][t]
    vector<vector<double>> slotCost;      // slotCost[j][t]
    vector<int> es;                       // 先行関係だけから決まる最早開始
    vector<vector<char>> can;             // can[j][t] : 容量的に 1 単位実行できる
};

struct Sol { bool feas = false, opt = false; double gap = 1, cost = 1e18, sec = 0; int ms = 0; };

// 初期解（--warm=<P1 の SCHED_MIP ファイル>）。P1 の解は中断しないだけの P3 の実行可能解なので、
// 各 ε で「条件を満たす P1 の最良解」を Gurobi の初期解として渡す（P3 が P1 より悪くならない）。
struct Warm { int ms; double cost; vector<int> st; };
static vector<Warm> g_warm;

static void loadWarm(const string &path, int n) {
    ifstream in(path); string line; vector<string> L;
    while (getline(in, line)) if (!line.empty()) L.push_back(line);
    for (auto &l : L) {
        istringstream is(l); vector<double> v; double x;
        while (is >> x) v.push_back(x);
        if ((int)v.size() != n + 2) continue;
        Warm w; w.ms = (int)v[0]; w.cost = v[1];
        for (int j = 0; j < n; ++j) w.st.push_back((int)v[2 + j]);
        g_warm.push_back(w);
    }
}

// mode 0（コスト最小, ε）: ms<=ε の中で最小コスト。mode 1: costUB 以下の中で最小 ms。
static const Warm *pickWarm(int eps, int mode, double costUB) {
    const Warm *best = nullptr;
    for (auto &w : g_warm) {
        if (w.ms > eps) continue;
        if (mode == 0) { if (!best || w.cost < best->cost) best = &w; }
        else if (w.cost <= costUB) { if (!best || w.ms < best->ms) best = &w; }
    }
    return best;
}

static Data loadData(const string &inst, double rr, bool rv, const string &costDir) {
    RCPSP_MIP_Solver P(inst, rr, rv, costDir);
    Data D;
    D.n = P.getNumJobs(); D.R = P.getNumResources(); D.T = P.getHorizon();
    D.d = P.getDurations(); D.dem = P.getDemand(); D.succ = P.getSuccessors();
    D.cap.assign(D.R, vector<int>(D.T));
    for (int k = 0; k < D.R; ++k)
        for (int t = 0; t < D.T; ++t) D.cap[k][t] = P.getCapacityAtTime(k, t);
    // コスト表（resourceCost と同じ：t >= COST_T は最終値にクランプ）
    string base = fs::path(inst).stem().string();
    ifstream fin(costDir + "/costs_" + base + ".csv");
    if (!fin) throw runtime_error("cost table not found: " + costDir + "/costs_" + base + ".csv");
    int Rc, Tc; fin >> Rc >> Tc;
    vector<vector<double>> c(Rc, vector<double>(Tc));
    for (int k = 0; k < Rc; ++k) for (int t = 0; t < Tc; ++t) fin >> c[k][t];
    D.slotCost.assign(D.n, vector<double>(D.T, 0.0));
    for (int j = 0; j < D.n; ++j)
        for (int t = 0; t < D.T; ++t) {
            int tt = min(t, Tc - 1); double s = 0;
            for (int k = 0; k < D.R; ++k) s += c[min(k, Rc - 1)][tt] * D.dem[j][k];
            D.slotCost[j][t] = s;
        }
    D.es.assign(D.n, 0);
    for (int i = 0; i < D.n; ++i)          // PSPLIB は番号順がトポロジカル順
        for (int s : D.succ[i]) if (s >= 0 && s < D.n) D.es[s] = max(D.es[s], D.es[i] + D.d[i]);
    D.can.assign(D.n, vector<char>(D.T, 0));
    for (int j = 0; j < D.n; ++j)
        for (int t = 0; t < D.T; ++t) {
            bool ok = true;
            for (int k = 0; k < D.R; ++k) if (D.dem[j][k] > D.cap[k][t]) { ok = false; break; }
            D.can[j][t] = ok;
        }
    return D;
}

// mode 0: コスト最小（ε 制約）  mode 1: メイクスパン最小（ε 制約, 任意でコスト上限）
static Sol solve(GRBEnv &env, const Data &D, int eps, int mode, double costUB, double tl) {
    Sol S;
    auto t0 = chrono::steady_clock::now();
    GRBModel m(env);
    m.set(GRB_DoubleParam_TimeLimit, tl);
    m.set(GRB_IntParam_Threads, 4);
    m.set(GRB_DoubleParam_MIPGap, 1e-4);
    m.set(GRB_IntParam_OutputFlag, 0);
    const int E = min(eps, D.T);
    vector<vector<GRBVar>> y(D.n), f(D.n);
    vector<vector<int>> ytime(D.n);
    GRBLinExpr obj = 0;
    for (int j = 0; j < D.n; ++j) {
        if (D.d[j] <= 0) continue;
        for (int t = D.es[j]; t < E; ++t) {
            if (!D.can[j][t]) continue;
            y[j].push_back(m.addVar(0, 1, 0, GRB_BINARY)); ytime[j].push_back(t);
            obj += D.slotCost[j][t] * y[j].back();
        }
        if ((int)y[j].size() < D.d[j]) return S;   // ε 内に置けない → 実行不能
        f[j].resize(E + 1);
        for (int t = 0; t <= E; ++t) f[j][t] = m.addVar(0, 1, 0, GRB_BINARY);
    }
    for (int j = 0; j < D.n; ++j) {
        if (D.d[j] <= 0) continue;
        GRBLinExpr sum = 0;
        for (auto &v : y[j]) sum += v;
        m.addConstr(sum == D.d[j]);
        // d_j f[j][t] <= Σ_{τ<t} y[j][τ]
        GRBLinExpr cum = 0; size_t p = 0;
        for (int t = 0; t <= E; ++t) {
            while (p < ytime[j].size() && ytime[j][p] < t) { cum += y[j][p]; ++p; }
            m.addConstr(D.d[j] * f[j][t] <= cum);
            if (t < E) m.addConstr(f[j][t] <= f[j][t + 1]);
        }
        m.addConstr(f[j][E] == 1);
    }
    for (int i = 0; i < D.n; ++i) {
        if (D.d[i] <= 0) continue;
        for (int s : D.succ[i]) {
            if (s < 0 || s >= D.n || D.d[s] <= 0) continue;
            for (size_t p = 0; p < y[s].size(); ++p) m.addConstr(y[s][p] <= f[i][ytime[s][p]]);
        }
    }
    for (int k = 0; k < D.R; ++k)
        for (int t = 0; t < E; ++t) {
            GRBLinExpr use = 0; bool any = false;
            for (int j = 0; j < D.n; ++j) {
                if (D.d[j] <= 0 || D.dem[j][k] == 0) continue;
                auto it = lower_bound(ytime[j].begin(), ytime[j].end(), t);
                if (it != ytime[j].end() && *it == t) { use += D.dem[j][k] * y[j][it - ytime[j].begin()]; any = true; }
            }
            if (any) m.addConstr(use <= D.cap[k][t]);
        }
    GRBVar C;
    if (mode == 0) {
        m.setObjective(obj, GRB_MINIMIZE);
    } else {
        C = m.addVar(0, E, 1.0, GRB_CONTINUOUS);
        for (int j = 0; j < D.n; ++j)
            for (size_t p = 0; p < y[j].size(); ++p) m.addConstr(C >= (ytime[j][p] + 1) * y[j][p]);
        if (costUB < 1e17) m.addConstr(obj <= costUB);
        m.setObjective(GRBLinExpr(C), GRB_MINIMIZE);
    }
    if (const Warm *w = pickWarm(E, mode, costUB)) {
        for (int j = 0; j < D.n; ++j) {
            if (D.d[j] <= 0) continue;
            int s0 = w->st[j], s1 = s0 + D.d[j];
            for (size_t p = 0; p < y[j].size(); ++p) y[j][p].set(GRB_DoubleAttr_Start, (ytime[j][p] >= s0 && ytime[j][p] < s1) ? 1.0 : 0.0);
            for (int t = 0; t <= E; ++t) f[j][t].set(GRB_DoubleAttr_Start, t >= s1 ? 1.0 : 0.0);
        }
        if (mode == 1) C.set(GRB_DoubleAttr_Start, (double)w->ms);
    }
    m.optimize();
    int st = m.get(GRB_IntAttr_Status);
    S.sec = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    if (m.get(GRB_IntAttr_SolCount) == 0) return S;
    S.feas = true; S.opt = (st == GRB_OPTIMAL);
    S.gap = S.opt ? 0.0 : m.get(GRB_DoubleAttr_MIPGap);
    double cost = 0; int ms = 0;
    for (int j = 0; j < D.n; ++j)
        for (size_t p = 0; p < y[j].size(); ++p)
            if (y[j][p].get(GRB_DoubleAttr_X) > 0.5) { cost += D.slotCost[j][ytime[j][p]]; ms = max(ms, ytime[j][p] + 1); }
    S.cost = cost; S.ms = ms;
    return S;
}

int main(int argc, char **argv) {
    enableKeepAwake();
    setvbuf(stdout, nullptr, _IONBF, 0); cout << unitbuf;
    vector<string> pos; string costDir = "costs_ext"; double tl = 120.0; string warmPath;
    string outTag;   // --tag=xxx でファイル名の末尾に _xxx を付ける（既存の結果を上書きしないため、2026-10-28）
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a.rfind("--cost-dir=", 0) == 0) costDir = a.substr(11);
        else if (a.rfind("--time-limit=", 0) == 0) tl = stod(a.substr(13));
        else if (a.rfind("--warm=", 0) == 0) warmPath = a.substr(7);
        else if (a.rfind("--tag=", 0) == 0) outTag = "_" + a.substr(6);
        else pos.push_back(a);
    }
    if (pos.size() < 3) { cerr << "usage: P3Slot <inst.sm> <rr> <rv> [eps=30] [--cost-dir=] [--time-limit=]\n"; return 1; }
    if (costDir == "costs") { cerr << "[FATAL] P3Slot is an extended-cost experiment tool; use --cost-dir=costs_ext\n"; return 1; }
    const string inst = pos[0]; double rr = stod(pos[1]); bool rv = stoi(pos[2]) != 0;
    int nb = pos.size() >= 4 ? stoi(pos[3]) : 30;
    const string prefix = fs::path(inst).stem().string();
    char ctag[32]; snprintf(ctag, sizeof ctag, "RR%03d_RV%d", (int)lround(rr * 100), rv ? 1 : 0);
    Data D = loadData(inst, rr, rv, costDir);
    if (!warmPath.empty()) { loadWarm(warmPath, D.n); cout << "[P3Slot] warm-start pool: " << g_warm.size() << " P1 solutions from " << warmPath << "\n"; }
    cout << "[P3Slot] " << prefix << " " << ctag << " T=" << D.T << " timeLimit=" << tl << "s cost-dir=" << costDir << "\n";
    GRBEnv env(true); env.set(GRB_IntParam_OutputFlag, 0); env.start();

    Sol lbS = solve(env, D, D.T, 1, 1e18, tl);
    if (!lbS.feas) { cout << "  min-makespan: infeasible/no solution within time limit\n"; return 2; }
    cout << "  min-makespan: ms=" << lbS.ms << (lbS.opt ? " (optimal)" : " (NOT proven)") << " " << lbS.sec << "s\n";
    Sol mc = solve(env, D, D.T, 0, 0, tl);
    cout << "  min-cost: cost=" << fixed << setprecision(2) << mc.cost << " ms=" << mc.ms << (mc.opt ? " (optimal)" : " (NOT proven)") << "\n";
    int ub = mc.ms;
    Sol tg = solve(env, D, D.T, 1, mc.cost * (1 + 1e-6), tl);
    if (tg.feas && tg.ms < ub) ub = tg.ms;
    int lb = lbS.ms;
    vector<int> eps;
    if (ub <= lb) eps.push_back(lb);
    else for (int i = 0; i < nb; ++i) {
        int e = (int)lround(lb + (double)(ub - lb) * i / (nb - 1));
        if (eps.empty() || eps.back() != e) eps.push_back(e);
    }
    cout << "  eps=[" << lb << "," << ub << "] " << eps.size() << " points\n";
    const string dir = "results/EXT_COST/FUN_MIP_P3SLOT/" + prefix + "/";
    if (dir.rfind("results/EXT_COST/", 0) != 0) return 3;
    fs::create_directories(dir);
    ofstream fun(dir + "FUN_MIP_P3SLOT_" + prefix + "_" + ctag + outTag + ".txt");
    ofstream gap(dir + "GAP_MIP_P3SLOT_" + prefix + "_" + ctag + outTag + ".csv");
    gap << "makespan,cost,gap,optimal,solve_seconds\n"; gap << fixed;
    double prev = 1e18;
    for (int e : eps) {
        Sol r = solve(env, D, e, 0, 0, tl);
        cout << "  eps=" << e << " ... ";
        if (!r.feas) { cout << "no solution\n"; continue; }
        cout << "ms=" << r.ms << " cost=" << r.cost << (r.opt ? "" : " (gap=" + to_string(r.gap) + ")");
        if (r.cost < prev - 1e-4) {
            Sol t = solve(env, D, e, 1, r.cost * (1 + 1e-6), tl);
            int ms = r.ms;
            if (t.feas && t.ms < ms) { cout << " -> tighten " << ms << "->" << t.ms; ms = t.ms; }
            fun << ms << " " << (long long)llround(r.cost) << "\n";
            gap << ms << "," << setprecision(4) << r.cost << "," << r.gap << "," << (r.opt ? 1 : 0) << "," << setprecision(2) << r.sec << "\n";
            prev = r.cost;
        }
        cout << "\n";
    }
    cout << "[P3Slot DONE] " << prefix << " " << ctag << "\n";
    return 0;
}
