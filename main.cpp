#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <vector>
#include <algorithm>

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

// =========================================================
// AUGMECON の .sol から startTimes を読む関数
// =========================================================
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
        // ここでは警告だけにして続行
    }

    // capacity を読み飛ばす
    vector<int> caps(R);
    for (int k = 0; k < R; ++k) fin >> caps[k];

    startTimes.assign(N, 0);

    for (int i = 0; i < N; ++i) {
        int id, st, dur;
        if (!(fin >> id >> st >> dur)) {
            cerr << "[main] Failed to read job line " << i << " from " << filename << endl;
            return false;
        }
        // demand は読み飛ばし
        for (int k = 0; k < R; ++k) {
            int tmp;
            fin >> tmp;
        }

        if (id < 1 || id > N) {
            cerr << "[main] Invalid job id " << id << " in " << filename << endl;
            return false;
        }
        startTimes[id - 1] = st;  // 0-index に格納
    }

    return true;
}

// =========================================================
// startTimes から NSGA-II 用 Solution を組み立て
// =========================================================
Solution *buildSolutionFromStartTimes(Problem *problem,
                                      const std::vector<int> &startTimes) {
    int nJobs = (int)startTimes.size();

    Solution *sol = new Solution(problem);
    Variable **vars = sol->getDecisionVariables();
    int nVars = sol->getNumberOfVariables();

    // 開始時刻＋jobId でソートして permutation を作る
    std::vector<int> jobs(nJobs);
    for (int j = 0; j < nJobs; ++j) jobs[j] = j;

    std::sort(jobs.begin(), jobs.end(),
              [&](int a, int b) {
                  if (startTimes[a] != startTimes[b])
                      return startTimes[a] < startTimes[b];
                  return a < b;
              });

    // 先頭 nJobs に permutation を書き込む
    for (int i = 0; i < nJobs; ++i) {
        vars[i]->setValue((double)jobs[i]);
    }

    // 後半のビット（schedObj）は全部 0 にしておく
    for (int j = 0; j < nJobs; ++j) {
        int idx = nJobs + j;
        if (idx < nVars) {
            vars[idx]->setValue(0.0);
        }
    }

    return sol;
}

// =========================================================
// main
// =========================================================
int main(int argc, char **argv) {

    string instanceFile = "j3033_1.sm";
    if (argc >= 2) {
        instanceFile = argv[1];
    }

    cout << "[INFO] NSGA-II start" << endl;
    cout << "       problem file   : " << instanceFile << endl;


    Problem *problem = new RCPSP_Problem(instanceFile);

    Algorithm *algorithm = new NSGAII(problem);

    int populationSize = 100;
    int maxEvaluations = 2000;

    // RCPSP_Problem 固有の maxEvaluations セッタはこれまで通り
    dynamic_cast<RCPSP_Problem*>(problem)->setMaxEvaluations(maxEvaluations);

    algorithm->setInputParameter("populationSize", &populationSize);
    algorithm->setInputParameter("maxEvaluations", &maxEvaluations);


    //    Problem の numberOfVariables から計算する
    int nJobs = problem->getNumberOfVariables() / 2;


    // AUGMECON の 2 解を初期個体として注入

    SolutionSet *seedPopulation = new SolutionSet(2);

    // Cmax 最適解
    {
        std::vector<int> stCmax;
        if (loadStartTimesFromSol("schedule_Cmax_opt_HCmax.sol",
                                  stCmax,
                                  nJobs)) {
            Solution *s = buildSolutionFromStartTimes(problem, stCmax);
            problem->evaluate(s);
            seedPopulation->add(s);
            std::cout << "[main] Seeded Cmax-opt solution." << std::endl;
        }
    }

    // Cost 最適解
    {
        std::vector<int> stCost;
        if (loadStartTimesFromSol("schedule_Cost_opt.sol",
                                  stCost,
                                  nJobs)) {
            Solution *s = buildSolutionFromStartTimes(problem, stCost);
            problem->evaluate(s);
            seedPopulation->add(s);
            std::cout << "[main] Seeded Cost-opt solution." << std::endl;
        }
    }

    // NSGAII 側が initialPopulation を見るようにしてある前提
    algorithm->setInputParameter("initialPopulation", seedPopulation);

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

    cout << "[INFO] NSGA-II finished successfully" << endl;
    cout << "       Total evaluations : " << evaluations << endl;
    cout << "[INFO] Results written to FUN / VAR files" << endl;

    delete population;
    delete algorithm;
    delete problem;

    return 0;
}


