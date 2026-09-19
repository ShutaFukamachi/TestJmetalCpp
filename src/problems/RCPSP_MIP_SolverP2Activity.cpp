#include "RCPSP_MIP_SolverP2Activity.h"

#include "gurobi_c++.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

namespace {

// ============================================================
//  活動ごとの実行可能時刻集合 A_j とその補助データ
// ============================================================
struct ActivitySets {
    std::vector<std::vector<int>> Aj;       // Aj[j] = 実行可能な実時刻の昇順リスト
    std::vector<std::vector<int>> posInAj;  // posInAj[j][t] = t の Aj[j] 内index（無ければ -1）
    std::vector<int>              iHi;      // 活動jの変数添字の上限（有効範囲 [0, iHi[j]]）
    bool feasiblePrecheck = true;
    int  infeasibleJob    = -1;
};

// A_j を構築する。d_j>0 の活動は容量条件でフィルタし、d_j==0（ダミー）は
// 資源を消費しないため全時刻を実行可能とする（A_j = [0..T-1] の恒等写像）。
ActivitySets buildActivitySets(const RCPSP_MIP_Solver &prob) {
    ActivitySets as;
    const int n    = prob.getNumJobs();
    const int nRes = prob.getNumResources();
    const int T    = prob.getHorizon();
    const auto &dur    = prob.getDurations();
    const auto &demand = prob.getDemand();

    as.Aj.resize(n);
    as.posInAj.assign(n, std::vector<int>(T, -1));
    as.iHi.assign(n, -1);

    for (int j = 0; j < n; ++j) {
        int d = dur[j];
        auto &Aj = as.Aj[j];

        if (d > 0) {
            for (int t = 0; t < T; ++t) {
                bool ok = true;
                for (int k = 0; k < nRes; ++k) {
                    if (demand[j][k] <= 0) continue;
                    if (prob.getCapacityAtTime(k, t) < demand[j][k]) { ok = false; break; }
                }
                if (ok) {
                    as.posInAj[j][t] = (int)Aj.size();
                    Aj.push_back(t);
                }
            }
        } else {
            // ダミー（所要時間0）: 資源を消費しないため全時刻が実行可能
            Aj.resize(T);
            for (int t = 0; t < T; ++t) { Aj[t] = t; as.posInAj[j][t] = t; }
        }

        int m = (int)Aj.size();
        if (d > 0 && m < d) {
            // このジョブは A_j 上に d_j 個の連続要素を確保できない = どこにも置けない
            as.feasiblePrecheck = false;
            if (as.infeasibleJob < 0) as.infeasibleJob = j;
            as.iHi[j] = -1;
            continue;  // 後続ジョブの A_j も一応計算しレポートに使えるようにする
        }
        as.iHi[j] = (d > 0) ? (m - d) : (m - 1);
    }
    return as;
}

// 実開始時刻 / 実完了時刻（precedence 用、+1 は「完了直後」の意味）
inline int realStart (const ActivitySets &as, int j, int i)          { return as.Aj[j][i]; }
inline int realFinish(const ActivitySets &as, int j, int i, int d)   {
    return (d > 0) ? (as.Aj[j][i + d - 1] + 1) : as.Aj[j][i];
}

struct ModelDataP2 {
    GRBModel model;
    ActivitySets as;
    std::vector<std::vector<GRBVar>> xvars;  // xvars[j][i]
    explicit ModelDataP2(GRBEnv &env) : model(env) {}
};

std::unique_ptr<ModelDataP2> buildModelP2Activitywise(
        const RCPSP_MIP_Solver &prob,
        GRBEnv &env,
        const RCPSP_MIP_Solver::Config &cfg)
{
    const int n    = prob.getNumJobs();
    const int nRes = prob.getNumResources();
    const int T    = prob.getHorizon();
    const auto &dur    = prob.getDurations();
    const auto &demand = prob.getDemand();
    const auto &succs  = prob.getSuccessors();

    auto md = std::make_unique<ModelDataP2>(env);
    md->as = buildActivitySets(prob);
    if (!md->as.feasiblePrecheck) return md;  // 呼び出し元で feasiblePrecheck を確認すること

    GRBModel &model = md->model;
    model.set(GRB_DoubleParam_TimeLimit, cfg.timeLimit);
    model.set(GRB_IntParam_Threads,      cfg.threads);
    model.set(GRB_DoubleParam_MIPGap,    cfg.mipGapTol);
    if (!cfg.verbose) model.set(GRB_IntParam_OutputFlag, 0);

    const auto &as = md->as;
    md->xvars.resize(n);

    // ---- 変数生成: x[j][i] = 1 iff 活動 j が A_j の i 番目の要素から開始 ----
    for (int j = 0; j < n; ++j) {
        int nI = as.iHi[j] + 1;
        md->xvars[j].resize(nI);
        for (int i = 0; i < nI; ++i) {
            std::string name = "x_" + std::to_string(j) + "_" + std::to_string(i);
            md->xvars[j][i] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY, name);
        }
    }
    model.update();

    // ---- 制約(1): 各活動 1 回だけ開始 ----
    for (int j = 0; j < n; ++j) {
        GRBLinExpr sum;
        for (auto &v : md->xvars[j]) sum += v;
        model.addConstr(sum == 1.0, "once_" + std::to_string(j));
    }

    // ---- 制約(2): 先行制約（実時刻ベース。a は定数なので線形） ----
    for (int j = 0; j < n; ++j) {
        int dj = dur[j];
        for (int s : succs[j]) {
            if (s < 0 || s >= n) continue;
            GRBLinExpr finish_j, start_s;
            for (int i = 0; i <= as.iHi[j]; ++i)
                finish_j += (double)realFinish(as, j, i, dj) * md->xvars[j][i];
            for (int i2 = 0; i2 <= as.iHi[s]; ++i2)
                start_s += (double)realStart(as, s, i2) * md->xvars[s][i2];
            model.addConstr(finish_j <= start_s,
                "prec_" + std::to_string(j) + "_" + std::to_string(s));
        }
    }

    // ---- 制約(3): 資源制約 ----
    // A_j の構築自体が「demand[j][k] > capacityAtTime(k,t) となる t を除外」しているため、
    // 旧 P1 の「容量0スロットで UB=0 を明示的に張る」workaround（.miss_memory/016）は不要
    // （そもそも該当する x[j][i] 自体が存在しない）。
    for (int t = 0; t < T; ++t) {
        for (int k = 0; k < nRes; ++k) {
            int cap = prob.getCapacityAtTime(k, t);
            GRBLinExpr usage;
            bool any = false;
            for (int j = 0; j < n; ++j) {
                int d = dur[j];
                if (d <= 0 || demand[j][k] <= 0) continue;
                int pos = as.posInAj[j][t];
                if (pos < 0) continue;  // j はそもそも t を占有し得ない
                int iLo = std::max(0, pos - d + 1);
                int iHiJ = std::min(as.iHi[j], pos);
                for (int i = iLo; i <= iHiJ; ++i) {
                    usage += (double)demand[j][k] * md->xvars[j][i];
                    any = true;
                }
            }
            if (any) model.addConstr(usage <= (double)cap,
                "res_" + std::to_string(k) + "_" + std::to_string(t));
        }
    }

    model.update();
    return md;
}

// 活動 j が index i から開始した場合のコスト（Σ_{u=0}^{d-1} 資源合計コスト）。
// 元の時間軸・正本コスト表をそのまま使う（時間圧縮していないため override 不要）。
double costOfChoice(const RCPSP_MIP_Solver &prob, const ActivitySets &as,
                     int j, int i, int d, int horizon)
{
    if (d <= 0) return 0.0;
    double c = 0.0;
    for (int u = 0; u < d; ++u) {
        c += prob.getSlotCost(j, as.Aj[j][i + u], horizon);
    }
    return c;
}

RealSchedule extractResultP2(const RCPSP_MIP_Solver &prob, const ModelDataP2 &md, int horizon) {
    RealSchedule rs;
    const int n = prob.getNumJobs();
    const auto &dur = prob.getDurations();

    bool hasSol = (md.model.get(GRB_IntAttr_SolCount) > 0);
    if (!hasSol) return rs;  // feasible=false のまま

    rs.feasible = true;
    int status = md.model.get(GRB_IntAttr_Status);
    rs.optimal  = (status == GRB_OPTIMAL);
    rs.gap      = rs.optimal ? 0.0 : md.model.get(GRB_DoubleAttr_MIPGap);

    rs.execTimes.assign(n, {});
    double totalCost = 0.0;
    const auto &as = md.as;

    for (int j = 0; j < n; ++j) {
        int d = dur[j];
        int chosen = 0;
        for (int i = 0; i < (int)md.xvars[j].size(); ++i) {
            if (md.xvars[j][i].get(GRB_DoubleAttr_X) > 0.5) { chosen = i; break; }
        }
        if (d > 0) {
            for (int u = 0; u < d; ++u) {
                int t = as.Aj[j][chosen + u];
                rs.execTimes[j].push_back(t);
                totalCost += prob.getSlotCost(j, t, horizon);
            }
        } else {
            rs.execTimes[j].push_back(as.Aj[j][chosen]);
        }
    }
    rs.makespan = rs.execTimes[n - 1].empty() ? 1e9 : (double)rs.execTimes[n - 1][0];
    rs.cost     = totalCost;
    return rs;
}

RealSchedule solveMakespanP2(const RCPSP_MIP_Solver &prob, const RCPSP_MIP_Solver::Config &cfg) {
    const int n = prob.getNumJobs();
    const int T = prob.getHorizon();
    if (n <= 0) return {};

    try {
        GRBEnv env(true);
        if (!cfg.verbose) env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        auto md = buildModelP2Activitywise(prob, env, cfg);
        if (!md->as.feasiblePrecheck) { RealSchedule rs; rs.feasible = false; return rs; }

        GRBLinExpr obj;
        for (int i = 0; i <= md->as.iHi[n - 1]; ++i)
            obj += (double)realStart(md->as, n - 1, i) * md->xvars[n - 1][i];
        md->model.setObjective(obj, GRB_MINIMIZE);

        auto t0 = std::chrono::steady_clock::now();
        md->model.optimize();
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        RealSchedule rs = extractResultP2(prob, *md, T);
        rs.solveSeconds = elapsed;
        return rs;
    } catch (GRBException &e) {
        std::cerr << "[P2-activity solveMakespan] GRBException " << e.getErrorCode()
                  << ": " << e.getMessage() << "\n";
        RealSchedule rs; rs.feasible = false; return rs;
    }
}

RealSchedule solveCostP2(const RCPSP_MIP_Solver &prob, int makespanBound,
                          const RCPSP_MIP_Solver::Config &cfg) {
    const int n = prob.getNumJobs();
    const int T = prob.getHorizon();
    if (n <= 0) return {};

    try {
        GRBEnv env(true);
        if (!cfg.verbose) env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        auto md = buildModelP2Activitywise(prob, env, cfg);
        if (!md->as.feasiblePrecheck) { RealSchedule rs; rs.feasible = false; return rs; }
        const auto &dur = prob.getDurations();

        GRBLinExpr ms_expr;
        for (int i = 0; i <= md->as.iHi[n - 1]; ++i)
            ms_expr += (double)realStart(md->as, n - 1, i) * md->xvars[n - 1][i];
        md->model.addConstr(ms_expr <= (double)makespanBound, "eps_ms");

        GRBLinExpr cost_obj;
        for (int j = 0; j < n; ++j) {
            int d = dur[j];
            if (d <= 0) continue;
            for (int i = 0; i <= md->as.iHi[j]; ++i) {
                double c = costOfChoice(prob, md->as, j, i, d, T);
                if (c != 0.0) cost_obj += c * md->xvars[j][i];
            }
        }
        md->model.setObjective(cost_obj, GRB_MINIMIZE);
        md->model.update();

        auto t0 = std::chrono::steady_clock::now();
        md->model.optimize();
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        RealSchedule rs = extractResultP2(prob, *md, T);
        rs.solveSeconds = elapsed;
        return rs;
    } catch (GRBException &e) {
        std::cerr << "[P2-activity solveCost] GRBException " << e.getErrorCode()
                  << ": " << e.getMessage() << "\n";
        RealSchedule rs; rs.feasible = false; return rs;
    }
}

// Stage 2（辞書式タイトニング）: RCPSP_MIP_Solver.cpp の solveMakespanBounded と同じ役割。
RealSchedule solveMakespanBoundedP2(const RCPSP_MIP_Solver &prob, int makespanBound, double costUB,
                                     const RCPSP_MIP_Solver::Config &cfg) {
    const int n = prob.getNumJobs();
    const int T = prob.getHorizon();
    if (n <= 0) return {};

    try {
        GRBEnv env(true);
        if (!cfg.verbose) env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        auto md = buildModelP2Activitywise(prob, env, cfg);
        if (!md->as.feasiblePrecheck) { RealSchedule rs; rs.feasible = false; return rs; }
        const auto &dur = prob.getDurations();

        GRBLinExpr ms_expr;
        for (int i = 0; i <= md->as.iHi[n - 1]; ++i)
            ms_expr += (double)realStart(md->as, n - 1, i) * md->xvars[n - 1][i];
        md->model.addConstr(ms_expr <= (double)makespanBound, "eps_ms");

        GRBLinExpr cost_expr;
        for (int j = 0; j < n; ++j) {
            int d = dur[j];
            if (d <= 0) continue;
            for (int i = 0; i <= md->as.iHi[j]; ++i) {
                double c = costOfChoice(prob, md->as, j, i, d, T);
                if (c != 0.0) cost_expr += c * md->xvars[j][i];
            }
        }
        md->model.addConstr(cost_expr <= costUB, "stage2_cost_ub");

        md->model.setObjective(ms_expr, GRB_MINIMIZE);
        md->model.update();

        auto t0 = std::chrono::steady_clock::now();
        md->model.optimize();
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        RealSchedule rs = extractResultP2(prob, *md, T);
        rs.solveSeconds = elapsed;
        return rs;
    } catch (GRBException &e) {
        std::cerr << "[P2-activity solveMakespanBounded] GRBException " << e.getErrorCode()
                  << ": " << e.getMessage() << "\n";
        RealSchedule rs; rs.feasible = false; return rs;
    }
}

// RCPSP_MIP_Solver.cpp の buildEpsPoints と同一ロジック（独立実装のため重複定義。
// 挙動を変えたい場合は両方を同時に直すこと）。
std::vector<int> buildEpsPointsP2(int lb, int ub, int breakpoints) {
    std::vector<int> eps;
    if (ub <= lb) { eps.push_back(lb); return eps; }
    if (breakpoints <= 0) {
        eps.reserve(ub - lb + 1);
        for (int e = lb; e <= ub; ++e) eps.push_back(e);
        return eps;
    }
    if (breakpoints == 1) { eps.push_back(lb); return eps; }
    if (ub - lb + 1 <= breakpoints) {
        eps.reserve(ub - lb + 1);
        for (int e = lb; e <= ub; ++e) eps.push_back(e);
        return eps;
    }
    eps.reserve(breakpoints);
    for (int i = 0; i < breakpoints; ++i) {
        double frac = (double)i / (double)(breakpoints - 1);
        int e = lb + (int)std::round(frac * (double)(ub - lb));
        if (eps.empty() || eps.back() != e) eps.push_back(e);
    }
    if (eps.back() != ub) eps.push_back(ub);
    return eps;
}

} // anonymous namespace

// ============================================================
//  公開API
// ============================================================
P2ActivityScaleReport computeP2ActivityScale(const RCPSP_MIP_Solver &prob) {
    P2ActivityScaleReport rep;
    ActivitySets as = buildActivitySets(prob);
    const int n = prob.getNumJobs();

    rep.feasiblePrecheck = as.feasiblePrecheck;
    rep.infeasibleJob    = as.infeasibleJob;
    rep.mLen.resize(n);
    rep.numVars.resize(n);
    for (int j = 0; j < n; ++j) {
        rep.mLen[j] = (int)as.Aj[j].size();
        int nv = (as.iHi[j] >= 0) ? (as.iHi[j] + 1) : 0;
        rep.numVars[j] = nv;
        rep.totalBinaryVars += nv;
    }
    return rep;
}

std::vector<RealSchedule> solveParetoP2ActivityWise(
        const RCPSP_MIP_Solver &prob,
        const RCPSP_MIP_Solver::Config &cfg)
{
    std::vector<RealSchedule> front;

    std::cout << "  [P2-activity] Solving min-makespan...\n";
    RealSchedule ms_res = solveMakespanP2(prob, cfg);
    if (!ms_res.feasible) {
        std::cerr << "  [P2-activity] Infeasible (makespan solve) — "
                     "少なくとも1つの活動が十分な実行可能スロットを持たない可能性があります\n";
        return front;
    }
    int lb_ms = (int)std::round(ms_res.makespan);

    std::cout << "  [P2-activity] Solving min-cost (no makespan constraint)...\n";
    RealSchedule cost_res = solveCostP2(prob, prob.getHorizon(), cfg);

    int ms_at_min_cost = lb_ms + cfg.makespanSlack;
    if (cost_res.feasible) {
        ms_at_min_cost = (int)std::round(cost_res.makespan);
        std::cout << "  [P2-activity] min-cost solution: ms=" << ms_at_min_cost
                  << "  cost=" << cost_res.cost << "\n";

        // min-cost 値を保ったまま makespan を最小化して ε 走査の上端を締める。
        // コスト表は t>=COST_T でクランプされ平坦になるため、締めないと
        // ub_ms がホライゾン端まで縮退し ε 点の大半が同一解に潰れる。
        {
            double tol = 1e-6 * std::max(1.0, std::fabs(cost_res.cost));
            RealSchedule tight = solveMakespanBoundedP2(prob, prob.getHorizon(),
                                                        cost_res.cost + tol, cfg);
            if (tight.feasible && tight.makespan < cost_res.makespan - 1e-6) {
                std::cout << "  [P2-activity] tighten ms@minCost " << ms_at_min_cost
                          << "->" << (int)std::round(tight.makespan) << "\n";
                ms_at_min_cost = (int)std::round(tight.makespan);
            }
        }
    } else {
        std::cout << "  [P2-activity] min-cost solve failed; using slack fallback\n";
    }

    int ub_ms = std::max(ms_at_min_cost, lb_ms + cfg.makespanSlack);
    std::vector<int> epsPoints = buildEpsPointsP2(lb_ms, ub_ms, cfg.epsBreakpoints);

    std::cout << "  [P2-activity] C*_ms=" << lb_ms
              << "  ms@minCost=" << ms_at_min_cost
              << "  searching eps=[" << lb_ms << ", " << ub_ms << "]"
              << "  (" << epsPoints.size() << " points"
              << (cfg.epsBreakpoints > 0 ? ", breakpoints=" + std::to_string(cfg.epsBreakpoints) : ", full scan")
              << ")\n";

    double prev_cost = 1e18;
    for (int eps : epsPoints) {
        std::cout << "  [P2-activity] eps=" << eps << " ... " << std::flush;

        RealSchedule r1 = solveCostP2(prob, eps, cfg);
        if (!r1.feasible) { std::cout << "infeasible\n"; continue; }

        std::cout << "ms=" << (int)r1.makespan << "  cost=" << r1.cost
                  << (r1.optimal ? "" : " (gap=" + std::to_string(r1.gap) + ")");

        if (r1.cost < prev_cost - 1e-4) {
            double tol = 1e-6 * std::max(1.0, std::fabs(r1.cost));
            RealSchedule r2 = solveMakespanBoundedP2(prob, eps, r1.cost + tol, cfg);

            RealSchedule r = r1;
            if (r2.feasible && r2.makespan < r1.makespan - 1e-6) {
                std::cout << "  -> tighten ms " << (int)r1.makespan << "->" << (int)r2.makespan;
                r = r2;
            }
            front.push_back(r);
            prev_cost = r1.cost;
        }
        std::cout << "\n";
    }

    std::cout << "  [P2-activity] Pareto front size = " << front.size() << "\n";
    return front;
}
