#include <IntNoneRepeatedSolutionType.h>
#include <cstddef>
#include <algorithm>
#include <numeric>
#include <vector>
#include <iostream>
#include <random>

IntNoneRepeatedSolutionType::IntNoneRepeatedSolutionType(Problem *problem)
    : SolutionType(problem) {}

/**
 * Creates non-repeated integer permutation variables
 */
Variable **IntNoneRepeatedSolutionType::createVariables() {
    int n = problem_->getNumberOfVariables();


    Variable **variables = new Variable*[n];
    if (variables == nullptr) {
        std::cerr << "Error: failed to allocate memory for variables" << std::endl;
        exit(-1);
    }

    // --- 値配列を生成 [0, 1, 2, ..., n-1] ---
    std::vector<int> values(n);
    std::iota(values.begin(), values.end(), 0);

    // --- シャッフルしてランダム順列を生成 ---
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(values.begin(), values.end(), g);
    // --- 各位置に値を設定 ---
    for (int i = 0; i < n; i++) {
        double lower = problem_->getLowerLimit(i);
        double upper = problem_->getUpperLimit(i);
        Variable *val = new Int(lower, upper);
        val->setValue(values[i]);
        variables[i] = val;
    }

    return variables;
}
