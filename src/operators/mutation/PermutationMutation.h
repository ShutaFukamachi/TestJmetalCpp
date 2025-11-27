#pragma once
#include "Operator.h"
#include "Solution.h"
#include "RCPSP_Problem.h"

class PermutationMutation : public Operator {
    const vector<vector<int>> mat;
    static vector<vector<int>> get_matrix(Problem *problem) {;
        RCPSP_Problem *rcpsp_problem = dynamic_cast<RCPSP_Problem *>(problem);
        if (!rcpsp_problem) {
            throw std::runtime_error("Problem is not of type RCPSP_Problem");
        }
        return rcpsp_problem->get_precedence_matrix();
    }
public:
    double probability;
    PermutationMutation(double p, Problem *problem): probability(p), mat(get_matrix(problem)) {}
    virtual void * execute(void * object);
};

