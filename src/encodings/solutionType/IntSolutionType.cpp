/*
 * InsertionMutation.h
 *
 *  Created on: 9 Feb 2019
 *      Author: Emad Alharbi
 *      University of York,UK
 */

#include <IntSolutionType.h>
#include <cstddef>
#include "RCPSP_Problem.h" // RCPSPの具体的な問題定義クラス

/**
 * Constructor
 * @param problem
 */
IntSolutionType::IntSolutionType(Problem *problem)
        : SolutionType(problem) {


}


/**
 * Creates the variables of the solution
 * @param decisionVariables
 */
Variable **IntSolutionType::createVariables() {
    // RCPSP_Problemクラスにキャストして、問題固有の情報にアクセスする
    const RCPSP_Problem *rcpspProblem = dynamic_cast<const RCPSP_Problem *>(problem_);
    if (!rcpspProblem) {
        cout << "Error: Problem is not of type RCPSP_Problem" << endl;
        exit(-1);
    }
    int i;

    Variable **variables = new Variable *[problem_->getNumberOfVariables()]; //malloc(sizeof(Int) * problem->getNumberOfVariables());
    if (variables == NULL) {
        cout << "Error grave: Impossible to reserve memory for variable type" << endl;
        exit(-1);
    }

    for (i = 0; i < problem_->getNumberOfVariables(); i++) {
        variables[i] = new Int(problem_->getLowerLimit(i), problem_->getUpperLimit(i));
    }
    // ランダムに実行可能なジョブ順序（topological sort) を生成して、最初の半分の変数に設定する
    vector<int> seq = rcpspProblem->random_topological_sort(rand());
    for (i = 0; i < problem_->getNumberOfVariables() / 2; i++) {
        variables[i]->setValue(seq[i]);
    }
    return variables;
} // createVariables
