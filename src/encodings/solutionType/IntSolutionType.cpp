/*
 * InsertionMutation.h
 *
 *  Created on: 9 Feb 2019
 *      Author: Emad Alharbi
 *      University of York,UK
 */

#include <IntSolutionType.h>
#include <cstddef>
#include <random>
#include <numeric>
#include <algorithm>
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
    int nJobs = problem_->getNumberOfVariables() / 2;
    const auto &successors = rcpspProblem->getSuccessors();

    std::vector<int> indeg(nJobs, 0);
    for (int j = 0; j < nJobs; ++j) {
        for (int succ : successors[j]) {
            if (succ >= 0 && succ < nJobs) ++indeg[succ];
        }
    }
    std::vector<int> avail;
    for (int j = 0; j < nJobs; ++j) {
        if (indeg[j] == 0) avail.push_back(j);
    }
    std::mt19937 rng(std::random_device{}());
    std::vector<int> seq;
    seq.reserve(nJobs);
    while (!avail.empty()) {
        std::uniform_int_distribution<int> dist(0, (int)avail.size() - 1);
        int idx = dist(rng);
        int j = avail[idx];
        avail[idx] = avail.back();
        avail.pop_back();
        seq.push_back(j);
        for (int succ : successors[j]) {
            if (succ >= 0 && succ < nJobs) {
                if (--indeg[succ] == 0) avail.push_back(succ);
            }
        }
    }
    if ((int)seq.size() != nJobs) {
        seq.resize(nJobs);
        std::iota(seq.begin(), seq.end(), 0);
    }
    for (i = 0; i < nJobs; i++) {
        variables[i]->setValue(seq[i]);
    }
    return variables;
} // createVariables
