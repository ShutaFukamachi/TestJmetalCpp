#include <limits>

#include "gurobi_c++.h"
#include <queue>

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


void runAUGMECON(const std::string &instanceFile) {
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

    const double TIME_LIMIT_SEC = 300.0;
    const double GAP_TARGET     = 0.01;

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
                                    "Cmax", "Cmax_opt",
                                    TIME_LIMIT_SEC, GAP_TARGET,
                                    UB1, LB1, GAP1, status1, solCnt1);
    double Cmin = res1.first;
    double CostAtCmin = res1.second;

    std::pair<double,double> res2 =
        solveModelSingleObj_AUG(inst, costs, env, H_cost,
                                "Cost", "Cost_opt",
                                TIME_LIMIT_SEC, GAP_TARGET,
                                UB2, LB2, GAP2, status2, solCnt2);
    double CmaxAtCostMin = res2.first;
    double CostMin = res2.second;



    // auto [Cmin, CostAtCmin] =
    //     solveModelSingleObj_AUG(inst, costs, env, H_cmax,
    //                             "Cmax", "Cmax_opt",
    //                             TIME_LIMIT_SEC, GAP_TARGET,
    //                             UB1, LB1, GAP1, status1, solCnt1);
    //
    // auto [CmaxAtCostMin, CostMin] =
    //     solveModelSingleObj_AUG(inst, costs, env, H_cost,
    //                             "Cost", "Cost_opt",
    //                             TIME_LIMIT_SEC, GAP_TARGET,
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

    std::string instanceFile = "j30.sm/j3033_1.sm";
    if (argc >= 2) instanceFile = argv[1];

    cout << "============================================\n";
    cout << "[INFO] Integrated AUGMECON + NSGA-II start\n";
    cout << "       problem file   : " << instanceFile << endl;
    cout << "============================================\n\n";

    try {
        runAUGMECON(instanceFile);
    } catch (const std::exception &e) {
        std::cerr << "[main] AUGMECON stage failed: " << e.what() << std::endl;
        return 1;
    }

    cout << "\n[INFO] AUGMECON stage finished. Now start NSGA-II.\n\n";

    Problem *problem = new RCPSP_Problem(instanceFile);
    Algorithm *algorithm = new NSGAII(problem);

    int populationSize = 100;
    int maxEvaluations = 200000;

    dynamic_cast<RCPSP_Problem*>(problem)->setMaxEvaluations(maxEvaluations);

    algorithm->setInputParameter("populationSize", &populationSize);
    algorithm->setInputParameter("maxEvaluations", &maxEvaluations);

    int nJobs = problem->getNumberOfVariables() / 2;


    // Optional seed 2 solutions from AUG schedules


    // SolutionSet *seedPopulation = new SolutionSet(2);
    //
    // // Cmax-opt
    // {
    //     std::vector<int> stCmax;
    //     if (loadStartTimesFromSol("schedule_Cmax_opt.sol", stCmax, nJobs)) {
    //         Solution *s = buildSolutionFromStartTimes(problem, stCmax, 0);
    //         problem->evaluate(s);
    //         seedPopulation->add(s);
    //         std::cout << "[main] Seeded Cmax-opt solution." << std::endl;
    //     } else {
    //         std::cout << "[main] Cmax-opt solution NOT seeded (read error)." << std::endl;
    //     }
    // }
    //
    // // Cost-opt
    // {
    //     std::vector<int> stCost;
    //     if (loadStartTimesFromSol("schedule_Cost_opt.sol", stCost, nJobs)) {
    //         Solution *s = buildSolutionFromStartTimes(problem, stCost, 1);
    //         problem->evaluate(s);
    //         seedPopulation->add(s);
    //         std::cout << "[main] Seeded Cost-opt solution." << std::endl;
    //     } else {
    //         std::cout << "[main] Cost-opt solution NOT seeded (read error)." << std::endl;
    //     }
    // }
    //
    // if (seedPopulation->size() > 0) {
    //     std::cout << "\n[DEBUG] Seed solutions (from AUGMECON)\n";
    //     for (int i = 0; i < seedPopulation->size(); ++i) {
    //         Solution* s = seedPopulation->get(i);
    //         std::cout << "  [SEED " << i << "] f1=" << s->getObjective(0)
    //                   << " f2=" << s->getObjective(1) << std::endl;
    //     }
    //     std::cout << std::endl;
    //     algorithm->setInputParameter("initialPopulation", seedPopulation);
    // } else {
    //     std::cout << "[WARN] No seed solutions were added; start from random population.\n";
    // }


    cout << "       populationSize : " << populationSize << endl;
    cout << "       maxEvaluations : " << maxEvaluations << endl;

    double crossoverProbability = 0.9;
    Operator *crossover = new PermutationCrossover(crossoverProbability);

    double mutationProbability =
        1.0 / static_cast<double>(problem->getNumberOfVariables());
    Operator *mutation = new PermutationMutation(mutationProbability, problem);

    map<string, void *> selectionParameters;
    selectionParameters["comparator"] = new CrowdingDistanceComparator();
    Operator *selection = new BinaryTournament2(selectionParameters);

    algorithm->addOperator("crossover", crossover);
    algorithm->addOperator("mutation", mutation);
    algorithm->addOperator("selection", selection);

    SolutionSet *population = algorithm->execute();

    Ranking ranking(population);
    SolutionSet *front0 = ranking.getSubfront(0);

    std::ofstream funFile("FUN");
    std::ofstream varFile("VAR");

    int nVar = problem->getNumberOfVariables();

    for (int i = 0; i < front0->size(); ++i) {
        Solution *sol = front0->get(i);

        funFile << sol->getObjective(0) << " "
                << sol->getObjective(1) << "\n";

        Variable **vars = sol->getDecisionVariables();
        for (int j = 0; j < nVar; ++j) {
            varFile << vars[j]->getValue();
            if (j + 1 < nVar) varFile << " ";
        }
        varFile << "\n";
    }

    funFile.close();
    varFile.close();

    int evaluations = 0;
    void *evalPtr = algorithm->getOutputParameter("evaluations");
    if (evalPtr != nullptr) {
        evaluations = *static_cast<int *>(evalPtr);
    }

    cout << "\n[INFO] NSGA-II finished successfully" << endl;
    cout << "       Total evaluations : " << evaluations << endl;
    cout << "[INFO] Results written to FUN / VAR files" << endl;

    delete population;
    delete algorithm;
    delete problem;

    return 0;
}




