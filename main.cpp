#include <iostream>
#include <string>
#include <map>
#include <fstream>
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

    dynamic_cast<RCPSP_Problem*>(problem)->setMaxEvaluations(maxEvaluations);

    algorithm->setInputParameter("populationSize", &populationSize);
    algorithm->setInputParameter("maxEvaluations", &maxEvaluations);

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


    //delete static_cast<CrowdingDistanceComparator *>(
     //   selectionParameters["comparator"]);

    //delete selection;
    //delete mutation;
    //delete crossover;
    delete algorithm;
    delete problem;

    return 0;
}
