#ifndef NSGAII_H
#define NSGAII_H

#include "Algorithm.h"
#include "SolutionSet.h"
#include "Problem.h"
#include "Operator.h"

class NSGAII : public Algorithm {
public:
    explicit NSGAII(Problem *problem);
    ~NSGAII();
    SolutionSet *execute();

private:
    SolutionSet *population = nullptr;          // ← 最終的に呼び出し側へ所有権移譲
    SolutionSet *offspringPopulation = nullptr;
    SolutionSet *unionPopulation = nullptr;

    int evaluations = 0;
    int populationSize = 0;
    int maxEvaluations = 0;

    Operator *crossoverOperator = nullptr;
    Operator *mutationOperator  = nullptr;
    Operator *selectionOperator = nullptr;
};

#endif

