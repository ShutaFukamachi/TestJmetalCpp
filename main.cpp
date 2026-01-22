#include <limits>
#include <queue>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <random>
#include <stdexcept>
#include <iostream>

#include "gurobi_c++.h"
#include "core/Problem.h"
#include "core/Algorithm.h"
#include "core/SolutionSet.h"
#include "Solution.h"
#include "Variable.h"
#include "metaheuristics/nsgaII/NSGAII.h"
#include "problems/RCPSP_Problem.h"

#include "operators/crossover/PermutationCrossover.h"
#include "operators/mutation/PermutationMutation.h"
#include "operators/selection/BinaryTournament2.h"

using namespace std;

// =======================
// Pareto coverage C-metric
// C(A,B) = fraction of solutions in B dominated by at least one in A (minimization)
// report 100*C(A,B)
// =======================
struct Obj2 { double f1; double f2; };

static inline bool dominatesMin2(const Obj2 &a, const Obj2 &b) {
    return (a.f1 <= b.f1 && a.f2 <= b.f2) && (a.f1 < b.f1 || a.f2 < b.f2);
}

static inline vector<Obj2> readFun2(const string &path) {
    ifstream fin(path.c_str());
    vector<Obj2> pts;
    if (!fin) return pts;
    double x, y;
    while (fin >> x >> y) pts.push_back({x, y});
    return pts;
}

static inline double coveragePercent(const vector<Obj2> &A, const vector<Obj2> &B) {
    if (B.empty()) return 0.0;
    int dominatedCount = 0;
    for (const auto &b : B) {
        bool dominated = false;
        for (const auto &a : A) {
            if (dominatesMin2(a, b)) { dominated = true; break; }
        }
        if (dominated) dominatedCount++;
    }
    return 100.0 * (double)dominatedCount / (double)B.size();
}

static inline void appendCMetricCSV(const string &csvPath,
                                    const string &instanceFile,
                                    const string &instanceId,
                                    const string &sizeTag,
                                    double cPercent) {
    const bool exists = ifstream(csvPath.c_str()).good();
    ofstream fout(csvPath.c_str(), ios::app);
    if (!exists) {
        fout << "instanceFile,instanceId,sizeTag,C_AUG_LS_over_NOAUG_NOLS_percent\n";
    }
    fout << instanceFile << "," << instanceId << "," << sizeTag << "," << cPercent << "\n";
}

// =======================
// Utility: file ops
// =======================
static inline bool fileExists(const string &p) {
    ifstream f(p.c_str(), ios::binary);
    return (bool)f;
}

static inline void copyFileBinary(const string &src, const string &dst) {
    ifstream in(src.c_str(), ios::binary);
    if (!in) throw runtime_error("Cannot open src: " + src);
    ofstream out(dst.c_str(), ios::binary);
    if (!out) throw runtime_error("Cannot open dst: " + dst);
    out << in.rdbuf();
}

static inline string baseNameNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

static inline string detectSizeTag(const string &instancePath) {
    if (instancePath.find("j30")  != string::npos) return "j30";
    if (instancePath.find("j60")  != string::npos) return "j60";
    if (instancePath.find("j90")  != string::npos) return "j90";
    if (instancePath.find("j120") != string::npos) return "j120";
    return "unknown";
}

// =======================
// Seed from AUGMECON_two_seeds.csv
// CSV format expected:
// instanceFile,instanceId,sizeTag,seedType,nValues,values...
// where values... are numeric dump of schedule_<id>_<seedType>.sol
// =======================
static inline vector<string> splitCSVSimple(const string& line) {
    vector<string> cols;
    string cur;
    for (char c : line) {
        if (c == ',') { cols.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    cols.push_back(cur);
    return cols;
}

static inline bool parseStartTimesFromSolNumericValues(
        const vector<double>& vals,
        vector<int>& startTimesOut) {

    if (vals.size() < 2) return false;
    int idx = 0;
    int N = (int)llround(vals[idx++]); // N
    int R = (int)llround(vals[idx++]); // R
    if (N <= 0 || R < 0) return false;

    if ((int)vals.size() < idx + R) return false; // caps
    idx += R;

    startTimesOut.assign(N, 0);

    for (int i = 0; i < N; ++i) {
        if ((int)vals.size() < idx + 3 + R) return false;
        int id = (int)llround(vals[idx++]);
        int st = (int)llround(vals[idx++]);
        /*int dur =*/ (void)vals[idx++];

        idx += R; // demands

        if (id < 1 || id > N) return false;
        startTimesOut[id - 1] = st;
    }
    return true;
}

static inline bool loadSeedStartTimesFromCSV(
        const string& csvPath,
        const string& instanceId,
        const string& seedType,     // "Cmax_opt" / "Cost_opt"
        vector<int>& startTimesOut) {

    ifstream fin(csvPath.c_str());
    if (!fin) return false;

    string line;
    if (!getline(fin, line)) return false;

    bool firstIsHeader = (line.find("instanceFile") != string::npos &&
                          line.find("seedType")    != string::npos);

    if (!firstIsHeader) {
        fin.clear();
        fin.seekg(0);
    }

    while (getline(fin, line)) {
        if (line.empty()) continue;
        auto cols = splitCSVSimple(line);
        if (cols.size() < 6) continue;

        // 0:instanceFile, 1:instanceId, 2:sizeTag, 3:seedType, 4:nValues, 5.. values
        if (cols[1] != instanceId) continue;
        if (cols[3] != seedType) continue;

        int nValues = atoi(cols[4].c_str());
        if (nValues <= 0) return false;
        if ((int)cols.size() < 5 + nValues) return false;

        vector<double> vals;
        vals.reserve(nValues);
        for (int i = 0; i < nValues; ++i) {
            vals.push_back(atof(cols[5 + i].c_str()));
        }
        return parseStartTimesFromSolNumericValues(vals, startTimesOut);
    }
    return false;
}

// =======================
// NSGA-II utilities (sol->startTimes for compatibility)
// =======================
Solution *buildSolutionFromStartTimes(Problem *problem,
                                      const vector<int> &startTimes,
                                      double initial_val) {
    int nJobs = (int)startTimes.size();

    Solution *sol = new Solution(problem);
    Variable **vars = sol->getDecisionVariables();
    int nVars = sol->getNumberOfVariables();

    vector<int> jobs(nJobs);
    for (int j = 0; j < nJobs; ++j) jobs[j] = j;

    sort(jobs.begin(), jobs.end(),
         [&](int a, int b) {
             if (startTimes[a] != startTimes[b]) return startTimes[a] < startTimes[b];
             return a < b;
         });

    for (int i = 0; i < nJobs; ++i) vars[i]->setValue((double)jobs[i]);

    for (int j = 0; j < nJobs; ++j) {
        int idx = nJobs + j;
        if (idx < nVars) vars[idx]->setValue(initial_val);
    }
    return sol;
}

// =======================
// MAIN
// =======================
int main(int argc, char **argv) {
    // Seed CSV (precomputed by AUGMECON-only project)
    const string seedCSV = "AUGMECON_two_seeds.csv";

    // cost file naming rule (per instance):
    // instance "j30.sm/j301_1.sm" => instanceId "j301_1" => costs_j301_1.csv
    auto costsFileForInstance = [&](const string& instanceFile) -> string {
        string id = baseNameNoExt(instanceFile);
        return "costs_" + id + ".csv";
    };

    // copy per-instance costs_XXX.csv -> costs.csv, and reset RCPSP global cache
    auto ensureCostsForInstance = [&](const string& instanceFile) {
        string src = costsFileForInstance(instanceFile);
        if (!fileExists(src)) {
            throw runtime_error("Missing per-instance cost file: " + src +
                                " (expected next to executable; ex: costs_j301_1.csv)");
        }
        copyFileBinary(src, "costs.csv");
        cout << "[COST] " << src << " -> costs.csv\n";
        // IMPORTANT: prevent reuse of previous cost series cache
        RCPSP_Problem::resetGlobalCostSeries();
    };

    auto runNSGA = [&](const string &instanceFile,
                       const string &tagPrefix,
                       bool useAUGSeed,
                       bool useLocalSearch) {

        cout << "============================================\n";
        cout << "[INFO] NSGA-II run\n";
        cout << "  instance   : " << instanceFile << "\n";
        cout << "  tagPrefix  : " << tagPrefix  << "\n";
        cout << "  AUG seed   : " << (useAUGSeed ? "ON" : "OFF") << "\n";
        cout << "  LocalSearch: " << (useLocalSearch ? "ON" : "OFF") << "\n";
        cout << "============================================\n";

        Problem *problem = new RCPSP_Problem(instanceFile);
        Algorithm *algorithm = new NSGAII(problem);

        int populationSize = 100;
        int maxEvaluations = 200000;
        dynamic_cast<RCPSP_Problem*>(problem)->setMaxEvaluations(maxEvaluations);

        algorithm->setInputParameter("populationSize", &populationSize);
        algorithm->setInputParameter("maxEvaluations", &maxEvaluations);

        int lsFlag = useLocalSearch ? 1 : 0;
        algorithm->setInputParameter("useLocalSearch", &lsFlag);

        int nJobs = problem->getNumberOfVariables() / 2;

        SolutionSet *seedPopulation = nullptr;
        if (useAUGSeed) {
            seedPopulation = new SolutionSet(2);

            // Cmax_opt from CSV
            {
                vector<int> stCmax;
                if (loadSeedStartTimesFromCSV(seedCSV, tagPrefix, "Cmax_opt", stCmax)) {
                    Solution *s = buildSolutionFromStartTimes(problem, stCmax, 0);
                    problem->evaluate(s);
                    seedPopulation->add(s);
                    cout << "[seed] OK Cmax_opt from " << seedCSV << " instance=" << tagPrefix << "\n";
                } else {
                    cout << "[seed] NG Cmax_opt (missing/parse error) instance=" << tagPrefix << "\n";
                }
            }
            // Cost_opt from CSV
            {
                vector<int> stCost;
                if (loadSeedStartTimesFromCSV(seedCSV, tagPrefix, "Cost_opt", stCost)) {
                    Solution *s = buildSolutionFromStartTimes(problem, stCost, 0);
                    problem->evaluate(s);
                    seedPopulation->add(s);
                    cout << "[seed] OK Cost_opt from " << seedCSV << " instance=" << tagPrefix << "\n";
                } else {
                    cout << "[seed] NG Cost_opt (missing/parse error) instance=" << tagPrefix << "\n";
                }
            }

            if (seedPopulation->size() > 0) {
                algorithm->setInputParameter("initialPopulation", seedPopulation);
            } else {
                delete seedPopulation;
                seedPopulation = nullptr;
            }
        }

        // Operators
        double crossoverProbability = 0.9;
        Operator *crossover = new PermutationCrossover(crossoverProbability);

        double mutationProbability = 1.0 / (double)problem->getNumberOfVariables();
        Operator *mutation = new PermutationMutation(mutationProbability, problem);

        map<string, void *> selectionParameters;
        Operator *selection = new BinaryTournament2(selectionParameters);

        algorithm->addOperator("crossover", crossover);
        algorithm->addOperator("mutation", mutation);
        algorithm->addOperator("selection", selection);

        SolutionSet *population = algorithm->execute();

        string mode =
            string(useAUGSeed ? "AUG" : "NOAUG") + "_" +
            string(useLocalSearch ? "LS" : "NOLS");

        ofstream funFile(("FUN_" + tagPrefix + "_" + mode).c_str());
        ofstream varFile(("VAR_" + tagPrefix + "_" + mode).c_str());

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

        delete population;
        delete algorithm;
        delete problem;
        if (seedPopulation) delete seedPopulation;

        cout << "[INFO] NSGA-II done. Outputs: FUN_" << tagPrefix << "_" << mode
             << " / VAR_" << tagPrefix << "_" << mode << "\n\n";
    };

    auto runOneInstanceAllModes = [&](const string &instanceFile) {
        const string sizeTag = detectSizeTag(instanceFile);
        const string prefix  = baseNameNoExt(instanceFile);
        if (sizeTag == "unknown") {
            throw runtime_error("Cannot detect sizeTag from path: " + instanceFile);
        }

        // ここが今回のポイント：インスタンスごとの costs_XXX.csv を使う
        ensureCostsForInstance(instanceFile);

        // 1) baseline: NO AUG seed, NO LS
        runNSGA(instanceFile, prefix, false, false);

        // 2) local search only
        runNSGA(instanceFile, prefix, false, true);

        // 3) seed only (from AUGMECON_two_seeds.csv)
        runNSGA(instanceFile, prefix, true, false);

        // 4) seed + local search
        runNSGA(instanceFile, prefix, true, true);

        // C-metric: C(AUG_LS, NOAUG_NOLS)
        {
            const string funA = "FUN_" + prefix + "_AUG_LS";
            const string funB = "FUN_" + prefix + "_NOAUG_NOLS";
            auto A = readFun2(funA);
            auto B = readFun2(funB);

            double cPercent = coveragePercent(A, B);
            cout << "[C-METRIC] instance=" << prefix
                 << " size=" << sizeTag
                 << " C(AUG_LS, NOAUG_NOLS) = " << cPercent << " [%]"
                 << " (A=" << A.size() << ", B=" << B.size() << ")\n";

            appendCMetricCSV("Cmetric_by_instance.csv", instanceFile, prefix, sizeTag, cPercent);
        }
    };

    try {
        // 1個だけ実行: exe j30.sm/j301_1.sm
        if (argc >= 2) {
            string instanceFile = argv[1];
            runOneInstanceAllModes(instanceFile);
            return 0;
        }

        vector<string> instances = {
            "j30.sm/j3013_1.sm",
             "j30.sm/j3014_1.sm",
             "j30.sm/j3015_1.sm",
             "j30.sm/j3016_1.sm",
             "j30.sm/j3017_1.sm",
             "j30.sm/j3018_1.sm",
             "j30.sm/j3019_1.sm",
             "j30.sm/j3020_1.sm",
             "j30.sm/j3021_1.sm",
             "j30.sm/j3022_1.sm",
             "j30.sm/j3023_1.sm",
             "j30.sm/j3024_1.sm",
             "j30.sm/j3025_1.sm",
             "j30.sm/j3026_1.sm",
             "j30.sm/j3027_1.sm",
             "j30.sm/j3028_1.sm",
             "j30.sm/j3029_1.sm",
             "j30.sm/j3030_1.sm",
             "j30.sm/j3031_1.sm",
             "j30.sm/j3032_1.sm",
             "j30.sm/j3033_1.sm",

             "j30.sm/j3034_1.sm",
             "j30.sm/j3035_1.sm",
             "j30.sm/j3036_1.sm",
             "j30.sm/j3037_1.sm",
             "j30.sm/j3038_1.sm",
             "j30.sm/j3039_1.sm",
             "j30.sm/j3040_1.sm",
             "j30.sm/j3041_1.sm",
             "j30.sm/j3042_1.sm",
             "j30.sm/j3043_1.sm",
             "j30.sm/j3044_1.sm",
             "j30.sm/j3045_1.sm",
             "j30.sm/j3046_1.sm",
             "j30.sm/j3047_1.sm",
             "j30.sm/j3048_1.sm",
             "j60.sm/j601_1.sm",
             "j60.sm/j602_1.sm",
             "j60.sm/j603_1.sm",
             "j60.sm/j604_1.sm",
             "j60.sm/j605_1.sm",
             "j60.sm/j606_1.sm",
             "j60.sm/j607_1.sm",
             "j60.sm/j608_1.sm",
             "j60.sm/j609_1.sm",
             "j60.sm/j6010_1.sm",
             "j60.sm/j6011_1.sm",
             "j60.sm/j6012_1.sm",
             "j60.sm/j6013_1.sm",
             "j60.sm/j6014_1.sm",
             "j60.sm/j6015_1.sm",
             "j60.sm/j6016_1.sm",
             "j60.sm/j6017_1.sm",
             "j60.sm/j6018_1.sm",
             "j60.sm/j6019_1.sm",
             "j60.sm/j6020_1.sm",
             "j60.sm/j6021_1.sm",
             "j60.sm/j6022_1.sm",
             "j60.sm/j6023_1.sm",
             "j60.sm/j6024_1.sm",
             "j60.sm/j6025_1.sm",
             "j60.sm/j6026_1.sm",
             "j60.sm/j6027_1.sm",
             "j60.sm/j6028_1.sm",
             "j60.sm/j6029_1.sm",
             "j60.sm/j6030_1.sm",
             "j60.sm/j6031_1.sm",
             "j60.sm/j6032_1.sm",
             "j60.sm/j6033_1.sm",
             "j60.sm/j6034_1.sm",
             "j60.sm/j6035_1.sm",
             "j60.sm/j6036_1.sm",
             "j60.sm/j6037_1.sm",
             "j60.sm/j6038_1.sm",
             "j60.sm/j6039_1.sm",
             "j60.sm/j6040_1.sm",
             "j60.sm/j6041_1.sm",
             "j60.sm/j6042_1.sm",
             "j60.sm/j6043_1.sm",
             "j60.sm/j6044_1.sm",
             "j60.sm/j6045_1.sm",
             "j60.sm/j6046_1.sm",
             "j60.sm/j6047_1.sm",
             "j60.sm/j6048_1.sm",
             "j90.sm/j901_1.sm",
             "j90.sm/j902_1.sm",
             "j90.sm/j903_1.sm",
             "j90.sm/j904_1.sm",
             "j90.sm/j905_1.sm",
             "j90.sm/j906_1.sm",
             "j90.sm/j907_1.sm",
             "j90.sm/j908_1.sm",
             "j90.sm/j909_1.sm",
             "j90.sm/j9010_1.sm",
             "j90.sm/j9011_1.sm",
             "j90.sm/j9012_1.sm",
             "j90.sm/j9013_1.sm",
             "j90.sm/j9014_1.sm",
             "j90.sm/j9015_1.sm",
             "j90.sm/j9016_1.sm",
             "j90.sm/j9017_1.sm",
             "j90.sm/j9018_1.sm",
             "j90.sm/j9019_1.sm",
             "j90.sm/j9020_1.sm",
             "j90.sm/j9021_1.sm",
             "j90.sm/j9022_1.sm",
             "j90.sm/j9023_1.sm",
             "j90.sm/j9024_1.sm",
             "j90.sm/j9025_1.sm",
             "j90.sm/j9026_1.sm",
             "j90.sm/j9027_1.sm",
             "j90.sm/j9028_1.sm",
             "j90.sm/j9029_1.sm",
             "j90.sm/j9030_1.sm",
             "j90.sm/j9031_1.sm",
             "j90.sm/j9032_1.sm",
             "j90.sm/j9033_1.sm",
             "j90.sm/j9034_1.sm",
             "j90.sm/j9035_1.sm",
             "j90.sm/j9036_1.sm",
             "j90.sm/j9037_1.sm",
             "j90.sm/j9038_1.sm",
             "j90.sm/j9039_1.sm",
             "j90.sm/j9040_1.sm",
             "j90.sm/j9041_1.sm",
             "j90.sm/j9042_1.sm",
             "j90.sm/j9043_1.sm",
             "j90.sm/j9044_1.sm",
             "j90.sm/j9045_1.sm",
             "j90.sm/j9046_1.sm",
             "j90.sm/j9047_1.sm",
             "j90.sm/j9048_1.sm",
            "j120.sm/j1201_1.sm",
             "j120.sm/j1202_1.sm",
             "j120.sm/j1203_1.sm",
             "j120.sm/j1204_1.sm",
             "j120.sm/j1205_1.sm",
             "j120.sm/j1206_1.sm",
             "j120.sm/j1207_1.sm",
             "j120.sm/j1208_1.sm",
             "j120.sm/j1209_1.sm",
             "j120.sm/j12010_1.sm",
             "j120.sm/j12011_1.sm",
             "j120.sm/j12012_1.sm",
             "j120.sm/j12013_1.sm",
             "j120.sm/j12014_1.sm",
             "j120.sm/j12015_1.sm",
             "j120.sm/j12016_1.sm",
             "j120.sm/j12017_1.sm",
             "j120.sm/j12018_1.sm",
             "j120.sm/j12019_1.sm",
             "j120.sm/j12020_1.sm",
             "j120.sm/j12021_1.sm",
             "j120.sm/j12022_1.sm",
             "j120.sm/j12023_1.sm",
             "j120.sm/j12024_1.sm",
             "j120.sm/j12025_1.sm",
             "j120.sm/j12026_1.sm",
             "j120.sm/j12027_1.sm",
             "j120.sm/j12028_1.sm",
             "j120.sm/j12029_1.sm",
             "j120.sm/j12030_1.sm",
             "j120.sm/j12031_1.sm",
             "j120.sm/j12032_1.sm",
             "j120.sm/j12033_1.sm",
             "j120.sm/j12034_1.sm",
             "j120.sm/j12035_1.sm",
             "j120.sm/j12036_1.sm",
             "j120.sm/j12037_1.sm",
             "j120.sm/j12038_1.sm",
             "j120.sm/j12039_1.sm",
             "j120.sm/j12040_1.sm",
             "j120.sm/j12041_1.sm",
             "j120.sm/j12042_1.sm",
             "j120.sm/j12043_1.sm",
             "j120.sm/j12044_1.sm",
             "j120.sm/j12045_1.sm",
             "j120.sm/j12046_1.sm",
             "j120.sm/j12047_1.sm",
             "j120.sm/j12048_1.sm",
             "j120.sm/j12049_1.sm",
             "j120.sm/j12050_1.sm",
             "j120.sm/j12051_1.sm",
             "j120.sm/j12052_1.sm",
             "j120.sm/j12053_1.sm",
             "j120.sm/j12054_1.sm",
             "j120.sm/j12055_1.sm",
             "j120.sm/j12056_1.sm",
             "j120.sm/j12057_1.sm",
             "j120.sm/j12058_1.sm",
             "j120.sm/j12059_1.sm",
             "j120.sm/j12060_1.sm"
        };

        for (const auto &inst : instances) {
            runOneInstanceAllModes(inst);
        }

        cout << "[BATCH] All done.\n";
        return 0;

    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << endl;
        return 1;
    }
}












