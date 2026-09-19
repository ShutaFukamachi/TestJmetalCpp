#include "RCPSP_MIP_Solver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "gurobi_c++.h"

// ============================================================
//  コンストラクタ
// ============================================================
RCPSP_MIP_Solver::RCPSP_MIP_Solver(const std::string &filename,
                                     double rr,
                                     bool   rv)
    : RCPSP_Problem(filename, /*strategy=*/1, rr, rv)
{}

int RCPSP_MIP_Solver::getHorizon() const {
    int T = 0;
    for (int d : instance.duration) T += d;
    if (!instance.capacity_t.empty() && !instance.capacity_t[0].empty()) {
        int tvT = static_cast<int>(instance.capacity_t[0].size());
        if (tvT > T) T = tvT;
    }
    return (T > 0) ? T : 1;
}

int RCPSP_MIP_Solver::getCapacityAtTime(int k, int t) const {
    return capacityAtTime(k, t);
}

// ============================================================
//  内部ヘルパー: GRBModel を構築して返す
//
//  呼び出し元が目的関数・ε 制約を追加して optimize() を呼ぶ想定。
//  x[j][t] のインデックス: tStart_[j]..tEnd_[j]
//
//  戻り値:
//    model    : 構築済みモデル（制約 (1)(2)(3) 設定済み）
//    xvars    : xvars[j][t - tStart_[j]] = x[j][t]
//    tStart   : ジョブ j の開始時刻下限 (EST)
//    tEnd     : ジョブ j の開始時刻上限 (LST)
// ============================================================
namespace {

struct ModelData {
    GRBModel                             model;
    std::vector<int>                     tStart;
    std::vector<int>                     tEnd;
    std::vector<std::vector<GRBVar>>     xvars;   // xvars[j][t - tStart[j]]

    explicit ModelData(GRBEnv &env) : model(env) {}
};

// 単純 EST: 先行ジョブの最大 (EST + duration)
std::vector<int> computeEST(
        const std::vector<std::vector<int>> &successors,
        const std::vector<int>              &duration,
        int n)
{
    // 先行リスト構築
    std::vector<std::vector<int>> preds(n);
    for (int i = 0; i < n; ++i)
        for (int s : successors[i])
            if (s >= 0 && s < n) preds[s].push_back(i);

    std::vector<int> est(n, 0);
    // トポロジカル順で EST を伝播
    std::vector<int> indeg(n, 0);
    for (int i = 0; i < n; ++i)
        for (int s : successors[i])
            if (s >= 0 && s < n) ++indeg[s];

    std::vector<int> queue;
    for (int j = 0; j < n; ++j)
        if (indeg[j] == 0) queue.push_back(j);

    for (int qi = 0; qi < (int)queue.size(); ++qi) {
        int j = queue[qi];
        for (int s : successors[j]) {
            if (s < 0 || s >= n) continue;
            est[s] = std::max(est[s], est[j] + duration[j]);
            if (--indeg[s] == 0) queue.push_back(s);
        }
    }
    return est;
}

// 単純 LST: 後続ジョブの最小 (LST - duration) — T を上限に後ろから計算
std::vector<int> computeLST(
        const std::vector<std::vector<int>> &successors,
        const std::vector<int>              &duration,
        int n, int T)
{
    std::vector<std::vector<int>> preds(n);
    for (int i = 0; i < n; ++i)
        for (int s : successors[i])
            if (s >= 0 && s < n) preds[s].push_back(i);

    std::vector<int> lft(n, T); // Latest finish time
    for (int j = n - 1; j >= 0; --j) {
        if (successors[j].empty()) lft[j] = T;
    }

    // 逆トポロジカル順で LST を伝播
    std::vector<int> indeg(n, 0);
    for (int i = 0; i < n; ++i)
        for (int s : successors[i])
            if (s >= 0 && s < n) ++indeg[s];

    std::vector<int> queue;
    for (int j = 0; j < n; ++j)
        if (successors[j].empty() || indeg[j] == 0) {}

    // 簡易的に全ジョブの LST = T - d_j（タイトでない上限）
    std::vector<int> lst(n);
    for (int j = 0; j < n; ++j)
        lst[j] = T - duration[j];

    return lst;
}

std::unique_ptr<ModelData> buildModel(
        const RCPSP_MIP_Solver &prob,
        GRBEnv                 &env,
        const RCPSP_MIP_Solver::Config &cfg)
{
    const int n    = prob.getNumJobs();
    const int nRes = prob.getNumResources();
    const int T    = prob.getHorizon();
    const auto &dur     = prob.getDurations();
    const auto &demand  = prob.getDemand();
    const auto &succs   = prob.getSuccessors();

    // EST / LST の計算（変数の範囲を絞る）
    std::vector<int> est = computeEST(succs, dur, n);
    std::vector<int> lst = computeLST(succs, dur, n, T);

    auto md = std::make_unique<ModelData>(env);
    GRBModel &model = md->model;

    model.set(GRB_DoubleParam_TimeLimit,  cfg.timeLimit);
    model.set(GRB_IntParam_Threads,       cfg.threads);
    model.set(GRB_DoubleParam_MIPGap,     cfg.mipGapTol);
    if (!cfg.verbose)
        model.set(GRB_IntParam_OutputFlag, 0);

    md->tStart.resize(n);
    md->tEnd  .resize(n);
    md->xvars .resize(n);

    // ---- 変数生成: x[j][t] = 1 iff ジョブ j が時刻 t に開始 ----
    for (int j = 0; j < n; ++j) {
        int d  = dur[j];
        int ts = est[j];
        int te = std::max(ts, lst[j]);  // LST: te + d <= T
        if (d > 0 && te + d > T) te = T - d;
        if (te < ts) te = ts;  // 必ず 1 変数以上

        md->tStart[j] = ts;
        md->tEnd  [j] = te;

        int nT = te - ts + 1;
        md->xvars[j].resize(nT);
        for (int ti = 0; ti < nT; ++ti) {
            int t = ts + ti;
            std::string name = "x_" + std::to_string(j) + "_" + std::to_string(t);
            md->xvars[j][ti] = model.addVar(
                0.0, 1.0, 0.0, GRB_BINARY, name);
        }
    }
    model.update();

    // ---- warm start（任意）: 各ジョブの与えられた開始時刻に Start=1 を設定 ----
    if (!cfg.warmStart.empty() && (int)cfg.warmStart.size() == n) {
        for (int j = 0; j < n; ++j) {
            int wstart = cfg.warmStart[j];
            int ts_j = md->tStart[j];
            for (int ti = 0; ti < (int)md->xvars[j].size(); ++ti) {
                double v = ((ts_j + ti) == wstart) ? 1.0 : 0.0;
                md->xvars[j][ti].set(GRB_DoubleAttr_Start, v);
            }
        }
    }

    // ---- 制約 (1): 各ジョブ 1 回だけ開始 ----
    for (int j = 0; j < n; ++j) {
        GRBLinExpr sum;
        for (auto &v : md->xvars[j]) sum += v;
        model.addConstr(sum == 1.0, "once_" + std::to_string(j));
    }

    // ---- 制約 (2): 先行制約 ----
    // start_i + d_i <= start_j  ⟺  Σ_t t*x[i][t] + d_i <= Σ_t t*x[j][t]
    for (int i = 0; i < n; ++i) {
        for (int s : succs[i]) {
            if (s < 0 || s >= n) continue;
            int di = dur[i];
            int ts_i = md->tStart[i], te_i = md->tEnd[i];
            int ts_s = md->tStart[s], te_s = md->tEnd[s];

            GRBLinExpr start_i, start_s;
            for (int ti = 0; ti <= te_i - ts_i; ++ti)
                start_i += (double)(ts_i + ti) * md->xvars[i][ti];
            for (int ti = 0; ti <= te_s - ts_s; ++ti)
                start_s += (double)(ts_s + ti) * md->xvars[s][ti];

            model.addConstr(start_i + (double)di <= start_s,
                "prec_" + std::to_string(i) + "_" + std::to_string(s));
        }
    }

    // ---- 制約 (3): 資源制約 ----
    // Σ_j r_{jk} * Σ_{τ: τ<=t<τ+d_j} x[j][τ] <= R_k(t)
    for (int t = 0; t < T; ++t) {
        for (int k = 0; k < nRes; ++k) {
            int cap = prob.getCapacityAtTime(k, t);
            if (cap <= 0) {
                // 容量 0 の時刻: この資源を使うジョブは t をまたいで実行不可。
                // t をカバーする開始時刻変数 x[j][τ] (τ <= t < τ+d_j) を禁止する。
                // （旧実装は制約自体をスキップしており、容量 0 スロットを
                //   突っ切る実行不可能スケジュールを許していた）
                for (int j = 0; j < n; ++j) {
                    int d = dur[j];
                    if (d <= 0 || demand[j][k] <= 0) continue;
                    int ts_j = md->tStart[j];
                    int te_j = md->tEnd[j];
                    int tau_lo = std::max(ts_j, t - d + 1);
                    int tau_hi = std::min(te_j, t);
                    for (int tau = tau_lo; tau <= tau_hi; ++tau)
                        md->xvars[j][tau - ts_j].set(GRB_DoubleAttr_UB, 0.0);
                }
                continue;
            }

            GRBLinExpr usage;
            bool anyTerm = false;
            for (int j = 0; j < n; ++j) {
                int d = dur[j];
                if (d <= 0 || demand[j][k] <= 0) continue;
                int ts_j = md->tStart[j];
                int te_j = md->tEnd[j];
                // x[j][τ] が時刻 t をカバー: τ <= t < τ + d_j → τ ∈ [t-d+1, t]
                int tau_lo = std::max(ts_j, t - d + 1);
                int tau_hi = std::min(te_j, t);
                for (int tau = tau_lo; tau <= tau_hi; ++tau) {
                    usage += (double)demand[j][k] * md->xvars[j][tau - ts_j];
                    anyTerm = true;
                }
            }
            if (anyTerm)
                model.addConstr(usage <= (double)cap,
                    "res_" + std::to_string(k) + "_" + std::to_string(t));
        }
    }

    model.update();
    return md;
}

// ソルブ結果を Result に変換
RCPSP_MIP_Solver::Result extractResult(
        const RCPSP_MIP_Solver &prob,
        ModelData              &md,
        double                  objVal,
        bool                    isMinMakespan)
{
    RCPSP_MIP_Solver::Result res;
    const int n = prob.getNumJobs();
    const int T = prob.getHorizon();

    int status = md.model.get(GRB_IntAttr_Status);
    bool hasSol = (md.model.get(GRB_IntAttr_SolCount) > 0);

    if (!hasSol) return res;

    res.feasible = true;
    res.optimal  = (status == GRB_OPTIMAL);
    res.gap      = (status == GRB_OPTIMAL) ? 0.0
                   : md.model.get(GRB_DoubleAttr_MIPGap);

    // 開始時刻を抽出
    res.startTimes.assign(n, 0);
    for (int j = 0; j < n; ++j) {
        int ts = md.tStart[j];
        for (int ti = 0; ti < (int)md.xvars[j].size(); ++ti) {
            if (md.xvars[j][ti].get(GRB_DoubleAttr_X) > 0.5) {
                res.startTimes[j] = ts + ti;
                break;
            }
        }
    }

    // makespan = last dummy の開始時刻
    res.makespan = (double)res.startTimes[n - 1];

    // cost 計算（d>0 のジョブのみ）
    res.cost = 0.0;
    const auto &dur = prob.getDurations();
    for (int j = 0; j < n; ++j) {
        if (dur[j] > 0)
            res.cost += prob.computeJobCostAt(j, res.startTimes[j], T);
    }

    return res;
}

// Stage 2（辞書式タイトニング）: 「makespan <= makespanBound かつ cost <= costUB」の
// もとで makespan を最小化する。Stage 1 の解（cost=C*, makespan<=makespanBound）は
// 常にこの制約集合に含まれるため、実行可能なら結果の makespan は Stage 1 以下になる。
RCPSP_MIP_Solver::Result solveMakespanBounded(
        const RCPSP_MIP_Solver &prob,
        int                     makespanBound,
        double                  costUB,
        const RCPSP_MIP_Solver::Config &cfg)
{
    const int n = prob.getNumJobs();
    const int T = prob.getHorizon();
    if (n <= 0) return {};

    try {
        GRBEnv env(true);
        if (!cfg.verbose) env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        auto md = buildModel(prob, env, cfg);
        const auto &dur = prob.getDurations();

        // ε 制約: makespan ≤ makespanBound
        int ts_last = md->tStart[n - 1];
        GRBLinExpr ms_expr;
        for (int ti = 0; ti < (int)md->xvars[n - 1].size(); ++ti)
            ms_expr += (double)(ts_last + ti) * md->xvars[n - 1][ti];
        md->model.addConstr(ms_expr <= (double)makespanBound, "eps_ms");

        // Stage 2 制約: cost ≤ costUB（Stage 1 の最小コストに微小許容値を加えた値）
        GRBLinExpr cost_expr;
        for (int j = 0; j < n; ++j) {
            if (dur[j] <= 0) continue;
            int ts_j = md->tStart[j];
            for (int ti = 0; ti < (int)md->xvars[j].size(); ++ti) {
                int t = ts_j + ti;
                double c = prob.computeJobCostAt(j, t, T);
                if (c != 0.0) cost_expr += c * md->xvars[j][ti];
            }
        }
        md->model.addConstr(cost_expr <= costUB, "stage2_cost_ub");

        md->model.setObjective(ms_expr, GRB_MINIMIZE);
        md->model.update();

        auto t0 = std::chrono::steady_clock::now();
        md->model.optimize();
        double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();

        double objVal = 1e9;
        if (md->model.get(GRB_IntAttr_SolCount) > 0)
            objVal = md->model.get(GRB_DoubleAttr_ObjVal);

        RCPSP_MIP_Solver::Result res = extractResult(prob, *md, objVal, true);
        res.solveSeconds = elapsed;
        return res;

    } catch (GRBException &e) {
        std::cerr << "[RCPSP_MIP_Solver::solveMakespanBounded] GRBException "
                  << e.getErrorCode() << ": " << e.getMessage() << "\n";
        return {};
    }
}

// ε 探索点を生成する: [lb, ub] を breakpoints 個に等間隔サンプリング（両端を必ず含む）。
// breakpoints <= 0 の場合は 1 刻み全走査（従来挙動）。
std::vector<int> buildEpsPoints(int lb, int ub, int breakpoints) {
    std::vector<int> eps;
    if (ub <= lb) {
        eps.push_back(lb);
        return eps;
    }
    if (breakpoints <= 0) {
        eps.reserve(ub - lb + 1);
        for (int e = lb; e <= ub; ++e) eps.push_back(e);
        return eps;
    }
    if (breakpoints == 1) {
        eps.push_back(lb);
        return eps;
    }
    // 範囲がブレイクポイント数より狭ければ 1 刻み全走査にフォールバック
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
    if (eps.back() != ub) eps.push_back(ub);  // 上端を必ず含める
    return eps;
}

} // anonymous namespace

// ============================================================
//  solveMakespan
// ============================================================
RCPSP_MIP_Solver::Result
RCPSP_MIP_Solver::solveMakespan(const Config &cfg) const
{
    const int n = getNumJobs();
    if (n <= 0) return {};

    try {
        GRBEnv env(true);
        if (!cfg.verbose) env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        auto md = buildModel(*this, env, cfg);

        // 目的: makespan = start[n-1] = Σ_t t * x[n-1][t]
        int ts_last = md->tStart[n - 1];
        GRBLinExpr obj;
        for (int ti = 0; ti < (int)md->xvars[n - 1].size(); ++ti)
            obj += (double)(ts_last + ti) * md->xvars[n - 1][ti];
        md->model.setObjective(obj, GRB_MINIMIZE);

        auto t0 = std::chrono::steady_clock::now();
        md->model.optimize();
        double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();

        double objVal = 1e9;
        if (md->model.get(GRB_IntAttr_SolCount) > 0)
            objVal = md->model.get(GRB_DoubleAttr_ObjVal);

        Result res = extractResult(*this, *md, objVal, true);
        res.solveSeconds = elapsed;
        return res;

    } catch (GRBException &e) {
        std::cerr << "[RCPSP_MIP_Solver::solveMakespan] GRBException "
                  << e.getErrorCode() << ": " << e.getMessage() << "\n";
        return {};
    }
}

// ============================================================
//  solveCost
// ============================================================
RCPSP_MIP_Solver::Result
RCPSP_MIP_Solver::solveCost(int makespanBound, const Config &cfg) const
{
    const int n = getNumJobs();
    const int T = getHorizon();
    if (n <= 0) return {};

    try {
        GRBEnv env(true);
        if (!cfg.verbose) env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        auto md = buildModel(*this, env, cfg);
        const auto &dur = getDurations();

        // ε 制約: makespan ≤ makespanBound
        int ts_last = md->tStart[n - 1];
        GRBLinExpr ms_expr;
        for (int ti = 0; ti < (int)md->xvars[n - 1].size(); ++ti)
            ms_expr += (double)(ts_last + ti) * md->xvars[n - 1][ti];
        md->model.addConstr(ms_expr <= (double)makespanBound, "eps_ms");

        // 目的: cost = Σ_j Σ_t c_{jt} * x[j][t]
        GRBLinExpr cost_obj;
        for (int j = 0; j < n; ++j) {
            if (dur[j] <= 0) continue;
            int ts_j = md->tStart[j];
            for (int ti = 0; ti < (int)md->xvars[j].size(); ++ti) {
                int t = ts_j + ti;
                double c = computeJobCostAt(j, t, T);
                if (c != 0.0) cost_obj += c * md->xvars[j][ti];
            }
        }
        md->model.setObjective(cost_obj, GRB_MINIMIZE);
        md->model.update();

        auto t0 = std::chrono::steady_clock::now();
        md->model.optimize();
        double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();

        double objVal = 1e9;
        if (md->model.get(GRB_IntAttr_SolCount) > 0)
            objVal = md->model.get(GRB_DoubleAttr_ObjVal);

        Result res = extractResult(*this, *md, objVal, false);
        res.solveSeconds = elapsed;
        return res;

    } catch (GRBException &e) {
        std::cerr << "[RCPSP_MIP_Solver::solveCost] GRBException "
                  << e.getErrorCode() << ": " << e.getMessage() << "\n";
        return {};
    }
}

// ============================================================
//  solvePareto（ε 制約法）
//
//  アルゴリズム:
//    1. makespan 最小化 → C*_ms  (下限)
//    2. コスト最小化（makespan 無制約）→ ms_at_min_cost  (上限)
//    3. ε ∈ [C*_ms, ub_ms] を cfg.epsBreakpoints 点に等間隔サンプリング
//       （両端を必ず含む。<=0 なら 1 刻み全走査）しながら cost 最小化 (makespan ≤ ε)
//    4. cost が改善したタイミングで Pareto 点を記録
//    5. サンプリング点を使い切ったら停止（早期打ち切りなし）
//
//  ※ cfg.makespanSlack は上限のバッファとして使用:
//     ub_ms = max(ms_at_min_cost, C*_ms + makespanSlack)
//  ※ 1刻み全走査は高RR/RV条件で ε 範囲が数百に達し、1条件で数百回のソルブ
//     ×最大timeLimit秒かかり数時間規模になる（.miss_memory 参照）。
//     ブレイクポイント方式で解の質を維持しつつ計算量を O(breakpoints) に抑える。
// ============================================================
std::vector<RCPSP_MIP_Solver::Result>
RCPSP_MIP_Solver::solvePareto(const Config &cfg) const
{
    std::vector<Result> front;

    // ---- Step 1: makespan 最小化 ----
    std::cout << "  [MIP] Solving min-makespan...\n";
    Result ms_res = solveMakespan(cfg);
    if (!ms_res.feasible) {
        std::cerr << "  [MIP] Infeasible (makespan solve)\n";
        return front;
    }
    int lb_ms = (int)std::round(ms_res.makespan);

    // ---- Step 2: コスト最小化（makespan 無制約）→ 上限を確定 ----
    std::cout << "  [MIP] Solving min-cost (no makespan constraint)...\n";
    Result cost_res = solveCost(getHorizon(), cfg);  // 上限 = 地平線全体

    int ms_at_min_cost = lb_ms + cfg.makespanSlack;  // フォールバック
    if (cost_res.feasible) {
        ms_at_min_cost = (int)std::round(cost_res.makespan);
        std::cout << "  [MIP] min-cost solution: ms=" << ms_at_min_cost
                  << "  cost=" << cost_res.cost << "\n";
    } else {
        std::cout << "  [MIP] min-cost solve failed; using slack fallback\n";
    }

    // ε の探索上限 = ms_at_min_cost（makespanSlack でバッファ）
    int ub_ms = std::max(ms_at_min_cost, lb_ms + cfg.makespanSlack);

    std::vector<int> epsPoints = buildEpsPoints(lb_ms, ub_ms, cfg.epsBreakpoints);

    std::cout << "  [MIP] C*_ms=" << lb_ms
              << "  ms@minCost=" << ms_at_min_cost
              << "  searching ε=[" << lb_ms << ", " << ub_ms << "]"
              << "  (" << epsPoints.size() << " points"
              << (cfg.epsBreakpoints > 0 ? ", breakpoints=" + std::to_string(cfg.epsBreakpoints) : ", full scan")
              << ")\n";

    double prev_cost = 1e18;

    // ---- Step 3: ε をサンプリングしながら Stage1(cost最小化)→Stage2(makespanタイトニング) ----
    for (int eps : epsPoints) {
        std::cout << "  [MIP] ε=" << eps << " ... " << std::flush;

        Result r1 = solveCost(eps, cfg);  // Stage 1: makespan<=eps のもとで cost 最小化

        if (!r1.feasible) {
            std::cout << "infeasible\n";
            continue;
        }

        std::cout << "ms=" << (int)r1.makespan
                  << "  cost=" << r1.cost
                  << (r1.optimal ? "" : " (gap=" + std::to_string(r1.gap) + ")");

        if (r1.cost < prev_cost - 1e-4) {
            // Stage 2: 同コスト（C*+tol）のもとで makespan を詰め、弱パレート解を排除
            double tol = 1e-6 * std::max(1.0, std::fabs(r1.cost));
            Result r2 = solveMakespanBounded(*this, eps, r1.cost + tol, cfg);

            Result r = r1;
            r.makespanBeforeTighten = r1.makespan;
            if (r2.feasible && r2.makespan < r1.makespan - 1e-6) {
                std::cout << "  -> tighten ms " << (int)r1.makespan
                          << "->" << (int)r2.makespan;
                r = r2;
                r.makespanBeforeTighten = r1.makespan;
            }

            front.push_back(r);
            prev_cost = r1.cost;
        }
        std::cout << "\n";
    }

    std::cout << "  [MIP] Pareto front size = " << front.size() << "\n";
    return front;
}
