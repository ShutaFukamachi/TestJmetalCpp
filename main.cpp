#include <limits>

#include "gurobi_c++.h"
#include <queue>
#include <filesystem>
#include <cstdio>
#include "core/Problem.h"
#include "core/Algorithm.h"
#include "core/SolutionSet.h"
#include "util/Ranking.h"
#include "Solution.h"
#include "Variable.h"
#include "metaheuristics/nsgaII/NSGAII.h"
#include "problems/RCPSP_Problem.h"

#include "operators/crossover/PermutationCrossover.h"
#include "operators/mutation/PermutationMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/comparators/CrowdingDistanceComparator.h"

using namespace std;


// =======================
// Pareto coverage C-metric
// C(A,B) = fraction of solutions in B that are dominated by at least one solution in A
// (minimization assumed for all objectives)
// We will report it as percentage: 100*C(A,B)
// =======================
struct Obj2 {
    double f1;
    double f2;
};

static inline bool dominatesMin2(const Obj2 &a, const Obj2 &b) {
    // a dominates b if a is no worse in both and strictly better in at least one
    return (a.f1 <= b.f1 && a.f2 <= b.f2) && (a.f1 < b.f1 || a.f2 < b.f2);
}

static inline std::vector<Obj2> readFun2(const std::string &path) {
    std::ifstream fin(path);
    std::vector<Obj2> pts;
    if (!fin) return pts;

    double x, y;
    while (fin >> x >> y) {
        pts.push_back({x, y});
    }
    return pts;
}

static inline double coveragePercent(const std::vector<Obj2> &A, const std::vector<Obj2> &B) {
    if (B.empty()) return 0.0;
    int dominatedCount = 0;
    for (const auto &b : B) {
        bool dominated = false;
        for (const auto &a : A) {
            if (dominatesMin2(a, b)) { dominated = true; break; }
        }
        if (dominated) dominatedCount++;
    }
    return 100.0 * (static_cast<double>(dominatedCount) / static_cast<double>(B.size()));
}

static inline void appendCMetricCSV(const std::string &csvPath,
                                   const std::string &instancePrefix,
                                   const std::string &sizeTag,
                                   double c_noaug_over_aug) {
    // CSV columns: instance,sizeTag,C(NO_AUG_NOLS over AUG_LS)[%]
    const bool exists = std::ifstream(csvPath).good();
    std::ofstream fout(csvPath, std::ios::app);
    if (!exists) {
        fout << "instance,sizeTag,C_NOAUG_NOLS_over_AUG_LS_percent\n";
    }
    fout << instancePrefix << "," << sizeTag << "," << c_noaug_over_aug << "\n";
}


struct ActivityAUG {
    int id = -1;
    int duration = 0;
    vector<int> successors;
    vector<int> demand;
    int ES = 0;
};

struct InstanceAUG {
    int nJobs = 0;
    int horizon = 0;
    int nRes = 0;
    vector<int> capacity;
    vector<ActivityAUG> jobs;
};

// ----- helper: parse last int in a string -----
static bool parseLastInt_AUG(const std::string& s, int& out) {
    std::istringstream iss(s);
    std::string token;
    bool found = false;
    int val = 0;
    while (iss >> token) {
        size_t i = 0;
        while (i < token.size() &&
               !(std::isdigit((unsigned char)token[i]) || token[i] == '-')) ++i;
        if (i < token.size()) {
            bool neg = false;
            if (token[i] == '-') { neg = true; ++i; }
            long long v = 0;
            bool anydigit = false;
            while (i < token.size() && std::isdigit((unsigned char)token[i])) {
                anydigit = true;
                v = v * 10 + (token[i] - '0');
                ++i;
            }
            if (anydigit) {
                if (neg) v = -v;
                val = static_cast<int>(v);
                found = true;
            }
        }
    }
    if (found) { out = val; return true; }
    return false;
}

// ----- PSPLIB SM reader for AUG -----
InstanceAUG readPSPLIB_SM_AUG(const std::string& filename) {
    std::ifstream in(filename);
    if (!in) throw std::runtime_error("Could not open file: " + filename);

    InstanceAUG inst;
    std::string line;

    // header: jobs / horizon / renewable
    in.clear(); in.seekg(0);
    while (std::getline(in, line)) {
        if (line.find("jobs (incl. supersource/sink") != std::string::npos) {
            int v;
            if (!parseLastInt_AUG(line, v))
                throw std::runtime_error("Failed to parse number of jobs.");
            inst.nJobs = v;
        } else if (line.find("horizon") != std::string::npos &&
                   line.find("resource") == std::string::npos) {
            int v;
            if (!parseLastInt_AUG(line, v))
                throw std::runtime_error("Failed to parse horizon.");
            inst.horizon = v;
        } else if (line.find("- renewable") != std::string::npos) {
            int v;
            if (parseLastInt_AUG(line, v)) inst.nRes = v;
        }
    }

    if (inst.nJobs <= 0) throw std::runtime_error("nJobs not found in header.");
    if (inst.nRes < 0) inst.nRes = 0;

    inst.jobs.assign(inst.nJobs, ActivityAUG());
    for (int i = 0; i < inst.nJobs; ++i) {
        inst.jobs[i].id = i;
        inst.jobs[i].demand.assign(inst.nRes, 0);
    }

    // PRECEDENCE RELATIONS
    in.clear(); in.seekg(0);
    while (std::getline(in, line)) {
        if (line.find("PRECEDENCE RELATIONS") != std::string::npos) {
            std::getline(in, line); // header skip
            int read = 0;
            while (read < inst.nJobs && std::getline(in, line)) {
                if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;

                std::istringstream iss(line);
                int id, nmodes, nsucc;
                if (!(iss >> id >> nmodes >> nsucc)) {
                    std::vector<int> tokens;
                    std::istringstream iss2(line);
                    int x;
                    while (iss2 >> x) tokens.push_back(x);
                    if (tokens.size() >= 3) {
                        id = tokens[0]; nmodes = tokens[1]; nsucc = tokens[2];
                        inst.jobs[id-1].successors.clear();
                        for (size_t k = 3;
                             k < tokens.size() &&
                             (int)inst.jobs[id-1].successors.size() < nsucc;
                             ++k) {
                            inst.jobs[id-1].successors.push_back(tokens[k] - 1);
                        }
                        ++read;
                        continue;
                    }
                    throw std::runtime_error("Failed to read PRECEDENCE line: " + line);
                }

                int idx = id - 1;
                inst.jobs[idx].successors.clear();
                for (int k = 0; k < nsucc; ++k) {
                    int s; iss >> s;
                    inst.jobs[idx].successors.push_back(s - 1);
                }
                ++read;
            }
            break;
        }
    }

    // REQUESTS/DURATIONS
    in.clear(); in.seekg(0);
    while (std::getline(in, line)) {
        if (line.find("REQUESTS/DURATIONS") != std::string::npos) {
            if (!std::getline(in, line))
                throw std::runtime_error("Unexpected EOF after REQUESTS/DURATIONS");

            int read = 0;
            while (read < inst.nJobs && std::getline(in, line)) {
                bool hasDigit = false;
                for (char c : line) {
                    if (std::isdigit((unsigned char)c)) { hasDigit = true; break; }
                }
                if (!hasDigit) continue;

                std::istringstream iss(line);
                int id = 0, mode = 0, dur = 0;
                if (!(iss >> id >> mode >> dur)) {
                    std::vector<int> tokens;
                    std::istringstream iss2(line);
                    int x;
                    while (iss2 >> x) tokens.push_back(x);
                    if (tokens.size() < 3)
                        throw std::runtime_error("Bad REQUESTS/DURATIONS line: " + line);
                    id = tokens[0]; mode = tokens[1]; dur = tokens[2];
                }
                int idx = id - 1;
                inst.jobs[idx].duration = dur;

                inst.jobs[idx].demand.assign(inst.nRes, 0);
                for (int r = 0; r < inst.nRes; ++r) {
                    int d;
                    if (!(iss >> d)) {
                        std::string more;
                        while (!(iss >> d)) {
                            if (!std::getline(in, more))
                                throw std::runtime_error("Unexpected EOF while reading demands.");
                            if (more.find_first_not_of(" \t\r\n") == std::string::npos) continue;
                            iss.clear();
                            iss.str(more);
                        }
                    }
                    inst.jobs[idx].demand[r] = d;
                }
                ++read;
            }
            break;
        }
    }

    // RESOURCEAVAILABILITIES
    in.clear(); in.seekg(0);
    while (std::getline(in, line)) {
        if (line.find("RESOURCEAVAILABILITIES") != std::string::npos) {
            inst.capacity.assign(inst.nRes, 0);
            vector<int> caps;
            while ((int)caps.size() < inst.nRes && std::getline(in, line)) {
                if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
                std::istringstream iss(line);
                int v;
                while (iss >> v) caps.push_back(v);
            }
            if ((int)caps.size() < inst.nRes)
                throw std::runtime_error("Failed to read resource capacities.");
            for (int r = 0; r < inst.nRes; ++r) inst.capacity[r] = caps[r];
            break;
        }
    }

    if (inst.horizon <= 0) {
        long long sum = 0;
        for (auto &a : inst.jobs) sum += a.duration;
        inst.horizon = static_cast<int>(sum);
    }

    return inst;
}

// ----- ES calculation -----
void computeEarliestStarts_AUG(InstanceAUG &inst) {
    int N = inst.nJobs;
    vector<int> indeg(N, 0);
    for (int i = 0; i < N; i++)
        for (int s : inst.jobs[i].successors) indeg[s]++;

    queue<int> q;
    for (int i = 0; i < N; i++) if (indeg[i] == 0) q.push(i);

    vector<int> ES(N, 0);
    while (!q.empty()) {
        int u = q.front(); q.pop();
        for (int v : inst.jobs[u].successors) {
            ES[v] = max(ES[v], ES[u] + inst.jobs[u].duration);
            if (--indeg[v] == 0) q.push(v);
        }
    }
    for (int i = 0; i < N; i++) inst.jobs[i].ES = ES[i];
}

// ----- cost
struct RNG_AUG {
    std::mt19937 gen;
    RNG_AUG(): gen((std::random_device())()) {}
    double uniform(double a, double b) {
        std::uniform_real_distribution<> d(a,b);
        return d(gen);
    }
    double normal(double mean, double sd) {
        std::normal_distribution<> d(mean, sd);
        return d(gen);
    }
};

static vector<double> seasonal_sequence_AUG(double Gamma) {
    return {0.0, Gamma, 2*Gamma, 3*Gamma, 2*Gamma, Gamma, 0.0,
            -Gamma, -2*Gamma, -3*Gamma, -2*Gamma, -Gamma};
}

static vector<vector<double>> generateCosts_AUG(int R, int T, RNG_AUG &rng) {
    vector<vector<double>> cost(R, vector<double>(T, 0.0));
    for (int k = 0; k < R; k++) {
        int pattern = (k % 4) + 1;
        double alpha = rng.uniform(100.0, 200.0);
        double beta = 0.0;
        double threshold = (2.0*T > 0) ? (alpha / (2.0 * T)) : 0.0;

        if (pattern == 1 || pattern == 3) {
            beta = (threshold > 0.1) ? rng.uniform(0.1, threshold) : 0.1;
        } else {
            beta = (threshold > 0.1) ? rng.uniform(-threshold, -0.1) : -0.1;
        }

        vector<double> gamma_seq;
        if (pattern == 3 || pattern == 4) {
            double Gamma = rng.uniform(20.0, 30.0);
            gamma_seq = seasonal_sequence_AUG(Gamma);
        } else {
            gamma_seq.assign(12, 0.0);
        }

        for (int t = 0; t < T; t++) {
            double gamma_t = gamma_seq[t % (int)gamma_seq.size()];
            double omega = rng.normal(0.0, 5.0);
            cost[k][t] = alpha + beta * t + gamma_t + omega;
        }
    }
    return cost;
}

static bool loadCostsCSV(const std::string &path,
                         int expectedR,
                         vector<vector<double>> &costs_out) {
    std::ifstream fin(path);
    if (!fin) return false;

    int R = 0, T = 0;
    if (!(fin >> R >> T)) return false;
    if (R != expectedR || R <= 0 || T <= 0) return false;

    vector<vector<double>> costs(R, vector<double>(T, 0.0));
    for (int k = 0; k < R; ++k) {
        for (int t = 0; t < T; ++t) {
            if (!(fin >> costs[k][t])) return false;
        }
    }
    costs_out = std::move(costs);
    return true;
}

static void saveCostsCSV(const std::string &path,
                         const vector<vector<double>> &costs) {
    int R = (int)costs.size();
    int T = (R > 0) ? (int)costs[0].size() : 0;
    std::ofstream fout(path);
    if (!fout) throw std::runtime_error("Cannot open " + path + " for writing");
    fout << R << " " << T << "\n";
    for (int k = 0; k < R; ++k) {
        for (int t = 0; t < T; ++t) {
            fout << costs[k][t];
            if (t + 1 < T) fout << " ";
        }
        fout << "\n";
    }
}

// ----- new time-indexed single-objective MIP -----
pair<double,double> solveModelSingleObj_AUG(
    InstanceAUG &inst,
    vector<vector<double>> &costs,
    GRBEnv &env,
    int H,
    const string &objType,     // "Cmax" or "Cost"
    const string &label,
    double timeLimitSec,
    double gapTarget,
    double &UB, double &LB, double &GAP,
    int &statusOut, int &solCountOut
) {
    int N = inst.nJobs;
    int R = inst.nRes;

    GRBModel model(env);
    model.set(GRB_IntParam_OutputFlag, 0);
    model.set(GRB_DoubleParam_TimeLimit, timeLimitSec);
    model.set(GRB_DoubleParam_MIPGap, gapTarget);
    model.set(GRB_DoubleParam_NodefileStart, 0.5);
    model.set(GRB_IntParam_Threads, 4);

    vector<vector<GRBVar>> y(N);
    vector<int> maxStart(N);

    for (int j = 0; j < N; ++j) {
        int p  = inst.jobs[j].duration;
        int es = inst.jobs[j].ES;
        int ls = H - p;
        if (ls < es) {
            throw std::runtime_error("H is too small for job " + std::to_string(j+1));
        }
        maxStart[j] = ls;
        y[j].resize(ls - es + 1);
        for (int t = es; t <= ls; ++t) {
            y[j][t - es] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY);
        }
    }

    GRBVar Cmax = model.addVar(0.0, (double)H, 0.0, GRB_INTEGER);

    // each job starts once
    for (int j = 0; j < N; ++j) {
        GRBLinExpr sum = 0;
        int es = inst.jobs[j].ES;
        int ls = maxStart[j];
        for (int t = es; t <= ls; ++t) sum += y[j][t - es];
        model.addConstr(sum == 1);
    }

    // S[j] expression
    vector<GRBLinExpr> S(N);
    for (int j = 0; j < N; ++j) {
        int es = inst.jobs[j].ES;
        int ls = maxStart[j];
        GRBLinExpr Sj = 0;
        for (int t = es; t <= ls; ++t) Sj += t * y[j][t - es];
        S[j] = Sj;
    }

    // precedence: S[i] + p_i <= S[j]
    for (int i = 0; i < N; ++i) {
        for (int j : inst.jobs[i].successors) {
            model.addConstr(S[i] + inst.jobs[i].duration <= S[j]);
        }
    }

    // resource constraints
    for (int tau = 0; tau < H; ++tau) {
        for (int k = 0; k < R; ++k) {
            GRBLinExpr usage = 0;
            for (int j = 0; j < N; ++j) {
                int p  = inst.jobs[j].duration;
                int es = inst.jobs[j].ES;
                int ls = maxStart[j];
                int tmin = std::max(es, tau - p + 1);
                int tmax = std::min(ls, tau);
                for (int t = tmin; t <= tmax; ++t) {
                    usage += inst.jobs[j].demand[k] * y[j][t - es];
                }
            }
            model.addConstr(usage <= inst.capacity[k]);
        }
    }

    // Cmax definition
    for (int j = 0; j < N; ++j) {
        model.addConstr(Cmax >= S[j] + inst.jobs[j].duration);
    }

    // cost coefficients
    vector<vector<double>> coeff(N);
    for (int j = 0; j < N; ++j) {
        int p  = inst.jobs[j].duration;
        int es = inst.jobs[j].ES;
        int ls = maxStart[j];
        coeff[j].assign(ls - es + 1, 0.0);
        for (int t = es; t <= ls; ++t) {
            double csum = 0.0;
            for (int tau = t; tau < t + p && tau < H; ++tau) {
                for (int k = 0; k < R; ++k) {
                    csum += inst.jobs[j].demand[k] * costs[k][tau];
                }
            }
            coeff[j][t - es] = csum;
        }
    }

    GRBLinExpr Cost = 0;
    for (int j = 0; j < N; ++j) {
        int es = inst.jobs[j].ES;
        int ls = maxStart[j];
        for (int t = es; t <= ls; ++t) {
            Cost += coeff[j][t - es] * y[j][t - es];
        }
    }

    if (objType == "Cmax") {
        model.setObjective(GRBLinExpr(Cmax), GRB_MINIMIZE);
    } else if (objType == "Cost") {
        model.setObjective(Cost, GRB_MINIMIZE);
    } else {
        throw std::runtime_error("Unknown objType: " + objType);
    }

    model.optimize();

    statusOut   = model.get(GRB_IntAttr_Status);
    solCountOut = model.get(GRB_IntAttr_SolCount);

    double Cval = 1e9, costVal = 1e9;
    UB = 1e9; LB = -1e9; GAP = 1e9;

    if (solCountOut > 0) {
        UB  = model.get(GRB_DoubleAttr_ObjVal);
        LB  = model.get(GRB_DoubleAttr_ObjBound);
        GAP = model.get(GRB_DoubleAttr_MIPGap);

        Cval    = Cmax.get(GRB_DoubleAttr_X);
        costVal = Cost.getValue();

        // write schedule
        vector<int> startTimes(N, -1);
        for (int j = 0; j < N; ++j) {
            int es = inst.jobs[j].ES;
            int ls = maxStart[j];
            for (int t = es; t <= ls; ++t) {
                if (y[j][t - es].get(GRB_DoubleAttr_X) > 0.5) {
                    startTimes[j] = t;
                    break;
                }
            }
        }

        if (!label.empty()) {
            ofstream sol("schedule_" + label + ".sol");
            sol << N << " " << R << "\n";
            for (int k = 0; k < R; ++k) sol << inst.capacity[k] << " ";
            sol << "\n";
            for (int j = 0; j < N; ++j) {
                sol << j+1 << " " << startTimes[j] << " " << inst.jobs[j].duration;
                for (int k = 0; k < R; ++k) sol << " " << inst.jobs[j].demand[k];
                sol << "\n";
            }
        }

        cout << "--- Objective info (" << objType << ", " << label << ") ---\n";
        cout << " status  = " << statusOut   << "\n";
        cout << " solCnt  = " << solCountOut << "\n";
        cout << " UB      = " << UB          << "\n";
        cout << " LB      = " << LB          << "\n";
        cout << " GAP     = " << GAP         << " (relative)\n";
    } else {
        cerr << "[AUG] No feasible solution found (status=" << statusOut
             << ", solCount=" << solCountOut << ")\n";
    }

    return {Cval, costVal};
}


void runAUGMECON(const std::string &instanceFile, double timeLimitSec, double mipGap, const std::string &tagPrefix) {
    cout << "[AUG] Instance file = " << instanceFile << "\n";

    InstanceAUG inst = readPSPLIB_SM_AUG(instanceFile);
    computeEarliestStarts_AUG(inst);

    int sink  = inst.nJobs - 1;
    int cpLen = inst.jobs[sink].ES + inst.jobs[sink].duration;

    cout << "[AUG] Critical path length ≒ " << cpLen << "\n";
    cout << "[AUG] horizon = " << inst.horizon << "\n";

    vector<vector<double>> costs;
    if (loadCostsCSV("costs.csv", inst.nRes, costs)) {
        cout << "[AUG] Reusing existing costs.csv (R=" << inst.nRes
             << ", T=" << (costs.empty() ? 0 : (int)costs[0].size()) << ")\n";
    } else {
        RNG_AUG rng;
        costs = generateCosts_AUG(inst.nRes, inst.horizon, rng);
        saveCostsCSV("costs.csv", costs);
        cout << "[AUG] Generated new random costs and wrote costs.csv (R=" << inst.nRes
             << ", T=" << inst.horizon << ")\n";
    }
    // timeLimitSec / mipGap are provided by caller (per instance)
    int H_cost = inst.horizon;

    int minFeasibleH = 0;
    for (int j = 0; j < inst.nJobs; ++j) {
        minFeasibleH = max(minFeasibleH, inst.jobs[j].ES + inst.jobs[j].duration);
    }
    int H_cmax = max(minFeasibleH, 250);  // tune as needed
    H_cmax = min(H_cmax, inst.horizon);

    cout << "[AUG] H_cmax = " << H_cmax << ", H_cost = " << H_cost << "\n";

    GRBEnv env = GRBEnv(true);
    env.start();

    double UB1, LB1, GAP1, UB2, LB2, GAP2;
    int status1, solCnt1, status2, solCnt2;

    std::pair<double,double> res1 =
        solveModelSingleObj_AUG(inst, costs, env, H_cmax,
                                    "Cmax", (tagPrefix + "_Cmax_opt").c_str(),
                                    timeLimitSec, mipGap,
                                    UB1, LB1, GAP1, status1, solCnt1);
    double Cmin = res1.first;
    double CostAtCmin = res1.second;

    std::pair<double,double> res2 =
        solveModelSingleObj_AUG(inst, costs, env, H_cost,
                                    "Cost", (tagPrefix + "_Cost_opt"),
                                timeLimitSec, mipGap,
                                UB2, LB2, GAP2, status2, solCnt2);
    double CmaxAtCostMin = res2.first;
    double CostMin = res2.second;



    // auto [Cmin, CostAtCmin] =
    //     solveModelSingleObj_AUG(inst, costs, env, H_cmax,
    //                             "Cmax", (tagPrefix + "_Cmax_opt").c_str(),
    //                             timeLimitSec, mipGap,
    //                             UB1, LB1, GAP1, status1, solCnt1);
    //
    // auto [CmaxAtCostMin, CostMin] =
    //     solveModelSingleObj_AUG(inst, costs, env, H_cost,
                                    "Cost", (tagPrefix + "_Cost_opt"),
    //                             timeLimitSec, mipGap,
    //                             UB2, LB2, GAP2, status2, solCnt2);

    cout << "=== [AUG] Best makespan solution (Cmax objective, time-limited) ===\n";
    cout << "Cmax = " << Cmin << ", Cost = " << CostAtCmin << "\n";
    cout << "  -> GAP (Cmax run) = " << GAP1 << "\n";

    cout << "=== [AUG] Best cost solution (Cost objective, time-limited) ===\n";
    cout << "Cmax = " << CmaxAtCostMin << ", Cost = " << CostMin << "\n";
    cout << "  -> GAP (Cost run) = " << GAP2 << "\n";
}


// NSGA-II utilities


bool loadStartTimesFromSol(const std::string &filename,
                           std::vector<int> &startTimes,
                           int expectedJobs) {
    ifstream fin(filename);
    if (!fin) {
        cerr << "[main] Cannot open " << filename << endl;
        return false;
    }

    int N = 0, R = 0;
    if (!(fin >> N >> R)) {
        cerr << "[main] Failed to read N,R from " << filename << endl;
        return false;
    }

    if (expectedJobs > 0 && N != expectedJobs) {
        cerr << "[main] WARNING: N in " << filename
             << " (" << N << ") != expectedJobs (" << expectedJobs << ")\n";
    }

    vector<int> caps(R);
    for (int k = 0; k < R; ++k) fin >> caps[k];

    startTimes.assign(N, 0);

    for (int i = 0; i < N; ++i) {
        int id, st, dur;
        if (!(fin >> id >> st >> dur)) {
            cerr << "[main] Failed to read job line " << i
                 << " from " << filename << endl;
            return false;
        }
        for (int k = 0; k < R; ++k) {
            int tmp; fin >> tmp;
        }
        if (id < 1 || id > N) {
            cerr << "[main] Invalid job id " << id
                 << " in " << filename << endl;
            return false;
        }
        startTimes[id - 1] = st;
    }
    return true;
}

Solution *buildSolutionFromStartTimes(Problem *problem,
                                      const std::vector<int> &startTimes,
                                      double initial_val) {
    int nJobs = (int)startTimes.size();

    Solution *sol = new Solution(problem);
    Variable **vars = sol->getDecisionVariables();
    int nVars = sol->getNumberOfVariables();

    std::vector<int> jobs(nJobs);
    for (int j = 0; j < nJobs; ++j) jobs[j] = j;

    std::sort(jobs.begin(), jobs.end(),
              [&](int a, int b) {
                  if (startTimes[a] != startTimes[b])
                      return startTimes[a] < startTimes[b];
                  return a < b;
              });

    for (int i = 0; i < nJobs; ++i) vars[i]->setValue((double)jobs[i]);

    for (int j = 0; j < nJobs; ++j) {
        int idx = nJobs + j;
        if (idx < nVars) vars[idx]->setValue(initial_val);
    }

    return sol;
}


// Integrated main



int main(int argc, char **argv) {
    // If you pass an instance file as argv[1], we run ONLY that instance in "seed+LS" mode (backward-friendly).
    // If no args, we run the full batch:
    //   j3033_1 (base -> seed -> LS -> seed+LS) -> j601_1 -> j901_1 -> j1201_1
    //
    // Costs handling:
    //   We maintain per-size files: costs_j30.csv, costs_j60.csv, costs_j90.csv, costs_j120.csv
    //   Before each instance run, we copy the corresponding file to costs.csv
    //   After finishing that size, we copy costs.csv back to the per-size file (so updates persist).
    //
    // AUGMECON params per size:
    //   j30 : timelimit 300, MIPGap 0.1
    //   j60 : timelimit 300, MIPGap 0.05
    //   j90 : timelimit 600, MIPGap 0.1
    //   j120: timelimit 600, MIPGap 0.3

    auto fileExists = [](const std::string &p) -> bool {
        std::ifstream f(p);
        return (bool)f;
    };

    auto copyFile = [](const std::string &src, const std::string &dst) {
        std::ifstream in(src, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open src: " + src);
        std::ofstream out(dst, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot open dst: " + dst);
        out << in.rdbuf();
    };

    auto baseNameNoExt = [](const std::string &path) -> std::string {
        std::string s = path;
        // strip dirs
        size_t p = s.find_last_of("/\\");
        if (p != std::string::npos) s = s.substr(p + 1);
        // strip ext
        size_t dot = s.find_last_of('.');
        if (dot != std::string::npos) s = s.substr(0, dot);
        return s;
    };

    auto detectSizeTag = [](const std::string &instancePath) -> std::string {
        // Prefer folder name like "j30.sm/..."
        if (instancePath.find("j30") != std::string::npos)  return "j30";
        if (instancePath.find("j60") != std::string::npos)  return "j60";
        if (instancePath.find("j90") != std::string::npos)  return "j90";
        if (instancePath.find("j120") != std::string::npos) return "j120";
        // fallback
        return "unknown";
    };

    struct AugParams { double timelimit; double mipgap; };
    auto augParamsFor = [](const std::string &sizeTag) -> AugParams {
        if (sizeTag == "j30")  return {300.0, 0.01};
        if (sizeTag == "j60")  return {300.0, 0.1};
        if (sizeTag == "j90")  return {600.0, 0.15};
        if (sizeTag == "j120") return {600.0, 0.30};
        return {300.0, 0.10};
    };

    std::string prevSizeTag = ""; // track size changes to regenerate costs


    auto ensureCostsForSize = [&](const std::string &sizeTag) {
        std::string perSize = "costs_" + sizeTag + ".csv";
        if (fileExists(perSize)) {
            copyFile(perSize, "costs.csv");
            std::cout << "[BATCH] Using " << perSize << " -> costs.csv\n";
        } else {
            // If missing, leave costs.csv as-is; AUGMECON/RCPSP_Problem can generate it.
            std::cout << "[BATCH] " << perSize << " not found. Will generate/keep costs.csv.\n";
        }
    };

    auto persistCostsForSize = [&](const std::string &sizeTag) {
        std::string perSize = "costs_" + sizeTag + ".csv";
        if (fileExists("costs.csv")) {
            copyFile("costs.csv", perSize);
            std::cout << "[BATCH] Saved costs.csv -> " << perSize << "\n";
        }
    };

    auto runNSGA = [&](const std::string &instanceFile,
                       const std::string &tagPrefix,
                       bool useAUGSeed,
                       bool useLocalSearch) {

        std::cout << "============================================\n";
        std::cout << "[INFO] NSGA-II run\n";
        std::cout << "  instance   : " << instanceFile << "\n";
        std::cout << "  tagPrefix  : " << tagPrefix  << "\n";
        std::cout << "  AUG seed   : " << (useAUGSeed ? "ON" : "OFF") << "\n";
        std::cout << "  LocalSearch: " << (useLocalSearch ? "ON" : "OFF") << "\n";
        std::cout << "============================================\n";

        Problem *problem = new RCPSP_Problem(instanceFile);
        Algorithm *algorithm = new NSGAII(problem);

        int populationSize = 100;
        int maxEvaluations = 200000;
        dynamic_cast<RCPSP_Problem*>(problem)->setMaxEvaluations(maxEvaluations);

        algorithm->setInputParameter("populationSize", &populationSize);
        algorithm->setInputParameter("maxEvaluations", &maxEvaluations);

        // Toggle local search inside NSGAII.cpp
        int lsFlag = useLocalSearch ? 1 : 0;
        algorithm->setInputParameter("useLocalSearch", &lsFlag);

        int nJobs = problem->getNumberOfVariables() / 2;

        // Seed 2 solutions from AUG schedules (optional)
        SolutionSet *seedPopulation = nullptr;
        if (useAUGSeed) {
            seedPopulation = new SolutionSet(2);

            // Cmax-opt
            {
                std::vector<int> stCmax;
                std::string fn = "schedule_" + tagPrefix + "_Cmax_opt.sol";
                if (loadStartTimesFromSol(fn, stCmax, nJobs)) {
                    Solution *s = buildSolutionFromStartTimes(problem, stCmax, 0);
                    problem->evaluate(s);
                    seedPopulation->add(s);
                    std::cout << "[main] Seeded Cmax-opt solution from " << fn << "\n";
                } else {
                    std::cout << "[main] Cmax-opt solution NOT seeded (read error): " << fn << "\n";
                }
            }

            // Cost-opt
            {
                std::vector<int> stCost;
                std::string fn = "schedule_" + tagPrefix + "_Cost_opt.sol";
                if (loadStartTimesFromSol(fn, stCost, nJobs)) {
                    Solution *s = buildSolutionFromStartTimes(problem, stCost, 0);
                    problem->evaluate(s);
                    seedPopulation->add(s);
                    std::cout << "[main] Seeded Cost-opt solution from " << fn << "\n";
                } else {
                    std::cout << "[main] Cost-opt solution NOT seeded (read error): " << fn << "\n";
                }
            }

            if (seedPopulation->size() > 0) {
                algorithm->setInputParameter("initialPopulation", seedPopulation);
            } else {
                delete seedPopulation;
                seedPopulation = nullptr;
            }
        }

        // Operators (same as your current main)
        double crossoverProbability = 0.9;
        Operator *crossover = new PermutationCrossover(crossoverProbability);

        double mutationProbability =
            1.0 / static_cast<double>(problem->getNumberOfVariables());
        Operator *mutation = new PermutationMutation(mutationProbability, problem);

        map<string, void *> selectionParameters;
        Operator *selection = new BinaryTournament2(selectionParameters);

        algorithm->addOperator("crossover", crossover);
        algorithm->addOperator("mutation", mutation);
        algorithm->addOperator("selection", selection);

        // Execute
        SolutionSet *population = algorithm->execute();

        // Write outputs with mode-specific names
        std::string mode =
            std::string(useAUGSeed ? "AUG" : "NOAUG") + "_" +
            std::string(useLocalSearch ? "LS" : "NOLS");

        std::ofstream funFile("FUN_" + tagPrefix + "_" + mode);
        std::ofstream varFile("VAR_" + tagPrefix + "_" + mode);

        for (int i = 0; i < population->size(); ++i) {
            Solution *sol = population->get(i);
            funFile << sol->getObjective(0) << " " << sol->getObjective(1) << "\n";

            int nVar = problem->getNumberOfVariables();
            Variable **vars = sol->getDecisionVariables();
            for (int j = 0; j < nVar; ++j) {
                varFile << vars[j]->getValue();
                if (j + 1 < nVar) varFile << " ";
            }
            varFile << "\n";
        }

        funFile.close();
        varFile.close();

        // Cleanup
        delete population;
        delete algorithm;
        delete problem;
        if (seedPopulation) delete seedPopulation;

        std::cout << "[INFO] NSGA-II done. Outputs: FUN_" << tagPrefix << "_" << mode
                  << " / VAR_" << tagPrefix << "_" << mode << "\n\n";
    };

    auto runOneInstanceAllModes = [&](const std::string &instanceFile) {
        const std::string sizeTag = detectSizeTag(instanceFile);
        const std::string prefix  = baseNameNoExt(instanceFile);

// If instance size changed (j30->j60->...), regenerate a fresh costs.csv
if (sizeTag != prevSizeTag) {
    std::cout << "[BATCH] Size changed: " << prevSizeTag << " -> " << sizeTag
              << "  => regenerate costs.csv\n";
    // Remove any old costs.csv so AUG/RCPSP will generate a new one for this size
    auto fileExists = [](const std::string& path) -> bool {
        std::ifstream f(path.c_str());
        return f.good();
    };

    if (fileExists("costs.csv")) {
        std::remove("costs.csv");  // const char* が必要
    }

    std::string perSize = "costs_" + sizeTag + ".csv";
    if (fileExists(perSize)) {
        std::remove(perSize.c_str());
    }
    // Reset RCPSP global cost series cache so it won't reuse the previous instance's table
    RCPSP_Problem::resetGlobalCostSeries();

    prevSizeTag = sizeTag;
}

ensureCostsForSize(sizeTag);

        // 1) baseline: NO AUG seed, NO LS
        runNSGA(instanceFile, prefix, false, false);

        // 2) run AUGMECON once (needed for seed runs)
        AugParams ap = augParamsFor(sizeTag);
        std::cout << "[BATCH] Run AUGMECON for " << prefix
                  << " (timelimit=" << ap.timelimit << ", mipgap=" << ap.mipgap << ")\n";
        runAUGMECON(instanceFile, ap.timelimit, ap.mipgap, prefix);

        // 3) seed only
        runNSGA(instanceFile, prefix, true, false);

        // 4) local search only
        runNSGA(instanceFile, prefix, false, true);

        // 5) seed + local search
        runNSGA(instanceFile, prefix, true, true);


        // C-metric

        {
            const std::string funA = "FUN_" + prefix + "_AUG_LS";
            const std::string funB = "FUN_" + prefix + "_NOAUG_NOLS";
            auto A = readFun2(funA);
            auto B = readFun2(funB);

            double cPercent = coveragePercent(A, B);
            std::cout << "[C-METRIC] instance=" << prefix
                      << " size=" << sizeTag
                      << " C(AUG_LS, NOAUG_NOLS) = " << cPercent << " [%]"
                      << " (A=" << A.size() << ", B=" << B.size() << ")\n";

            appendCMetricCSV("Cmetric_summary.csv", prefix, sizeTag, cPercent);
        }

persistCostsForSize(sizeTag);
    };

    try {
        if (argc >= 2) {
            // Single instance (for quick test): default seed+LS (and run AUGMECON once)
            std::string instanceFile = argv[1];
            std::string sizeTag = detectSizeTag(instanceFile);
            std::string prefix  = baseNameNoExt(instanceFile);

            ensureCostsForSize(sizeTag);

            AugParams ap = augParamsFor(sizeTag);
            runAUGMECON(instanceFile, ap.timelimit, ap.mipgap, prefix);
            runNSGA(instanceFile, prefix, true, true);

            persistCostsForSize(sizeTag);
            return 0;
        }

        // Full batch
        std::vector<std::string> instances = {
            "j30.sm/j301_1.sm",
            // "j30.sm/j302_1.sm",
            // "j30.sm/j303_1.sm",
            // "j30.sm/j304_1.sm",
            // "j30.sm/j305_1.sm",
            // "j30.sm/j306_1.sm",
            // "j30.sm/j307_1.sm",
            // "j30.sm/j308_1.sm",
            // "j30.sm/j309_1.sm",
            // "j30.sm/j3010_1.sm",
            // "j30.sm/j3011_1.sm",
            // "j30.sm/j3012_1.sm",
            // "j30.sm/j3013_1.sm",
            // "j30.sm/j3014_1.sm",
            // "j30.sm/j3015_1.sm",
            // "j30.sm/j3016_1.sm",
            // "j30.sm/j3017_1.sm",
            // "j30.sm/j3018_1.sm",
            // "j30.sm/j3019_1.sm",
            // "j30.sm/j3020_1.sm",
            // "j30.sm/j3021_1.sm",
            // "j30.sm/j3022_1.sm",
            // "j30.sm/j3023_1.sm",
            // "j30.sm/j3024_1.sm",
            // "j30.sm/j3025_1.sm",
            // "j30.sm/j3026_1.sm",
            // "j30.sm/j3027_1.sm",
            // "j30.sm/j3028_1.sm",
            // "j30.sm/j3029_1.sm",
            // "j30.sm/j3030_1.sm",
            // "j30.sm/j3031_1.sm",
            // "j30.sm/j3032_1.sm",
            // "j30.sm/j3033_1.sm",
            // "j30.sm/j3034_1.sm",
            // "j30.sm/j3035_1.sm",
            // "j30.sm/j3036_1.sm",
            // "j30.sm/j3037_1.sm",
            // "j30.sm/j3038_1.sm",
            // "j30.sm/j3039_1.sm",
            // "j30.sm/j3040_1.sm",
            // "j30.sm/j3041_1.sm",
            // "j30.sm/j3042_1.sm",
            // "j30.sm/j3043_1.sm",
            // "j30.sm/j3044_1.sm",
            // "j30.sm/j3045_1.sm",
            // "j30.sm/j3046_1.sm",
            // "j30.sm/j3047_1.sm",
            // "j30.sm/j3048_1.sm",
            // "j60.sm/j602_1.sm",
            // "j60.sm/j603_1.sm",
            // "j60.sm/j604_1.sm",
            // "j60.sm/j605_1.sm",
            // "j60.sm/j606_1.sm",
            // "j60.sm/j607_1.sm",
            // "j60.sm/j608_1.sm",
            // "j60.sm/j609_1.sm",
            // "j60.sm/j6010_1.sm",
            // "j60.sm/j6011_1.sm",
            // "j60.sm/j6012_1.sm",
            // "j60.sm/j6013_1.sm",
            // "j60.sm/j6014_1.sm",
            // "j60.sm/j6015_1.sm",
            // "j60.sm/j6016_1.sm",
            // "j60.sm/j6017_1.sm",
            // "j60.sm/j6018_1.sm",
            // "j60.sm/j6019_1.sm",
            // "j60.sm/j6020_1.sm",
            // "j60.sm/j6021_1.sm",
            // "j60.sm/j6022_1.sm",
            // "j60.sm/j6023_1.sm",
            // "j60.sm/j6024_1.sm",
            // "j60.sm/j6025_1.sm",
            // "j60.sm/j6026_1.sm",
            // "j60.sm/j6027_1.sm",
            // "j60.sm/j6028_1.sm",
            // "j60.sm/j6029_1.sm",
            // "j60.sm/j6030_1.sm",
            // "j60.sm/j6031_1.sm",
            // "j60.sm/j6032_1.sm",
            // "j60.sm/j6033_1.sm",
            // "j60.sm/j6034_1.sm",
            // "j60.sm/j6035_1.sm",
            // "j60.sm/j6036_1.sm",
            // "j60.sm/j6037_1.sm",
            // "j60.sm/j6038_1.sm",
            // "j60.sm/j6039_1.sm",
            // "j60.sm/j6040_1.sm",
            // "j60.sm/j6041_1.sm",
            // "j60.sm/j6042_1.sm",
            // "j60.sm/j6043_1.sm",
            // "j60.sm/j6044_1.sm",
            // "j60.sm/j6045_1.sm",
            // "j60.sm/j6046_1.sm",
            // "j60.sm/j6047_1.sm",
            // "j60.sm/j6048_1.sm",
            // "j90.sm/j901_1.sm",
            // "j90.sm/j9010_1.sm",
            // "j90.sm/j9020_1.sm",
            // "j90.sm/j9030_1.sm",
            // "j90.sm/j9040_1.sm",
            // "j120.sm/j1201_1.sm",
            // "j120.sm/j12010_1.sm",
            // "j120.sm/j12020_1.sm",
            // "j120.sm/j12030_1.sm",
            // "j120.sm/j12040_1.sm",
            // "j120.sm/j12050_1.sm",
            // "j120.sm/j12060_1.sm",
        };

        for (const auto &inst : instances) {
            runOneInstanceAllModes(inst);
        }

        std::cout << "[BATCH] All done.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }
}








