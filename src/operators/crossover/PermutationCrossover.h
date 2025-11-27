#pragma once
#include "Operator.h"
#include "Solution.h"
#include <vector>
#include "RCPSP_Problem.h"

class PermutationCrossover : public Operator {
public:
    double probability;
    PermutationCrossover(double p) : probability(p) {}
    virtual void * execute(void * object);
};

