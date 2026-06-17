#include "Solution.h"
#include "Problem.h"
#include <iostream>
#include <sstream>

Solution::Solution(Problem* problem)
    : problem_(problem)
{
    if (problem) {
        vars_.assign(problem->getNumberOfVariables(), 0);
        objectives_.assign(problem->getNumberOfObjectives(), 0.0);
    }
    distanceToSolutionSet_ = std::numeric_limits<double>::max();
}

double Solution::getObjective(int i) const {
    if (i < 0 || i >= (int)objectives_.size()) {
        std::cerr << "Solution::getObjective: index out of range: " << i << "\n";
        std::exit(-1);
    }
    return objectives_[i];
}

void Solution::setObjective(int i, double v) {
    if (i < 0 || i >= (int)objectives_.size()) {
        std::cerr << "Solution::setObjective: index out of range: " << i << "\n";
        std::exit(-1);
    }
    objectives_[i] = v;
}

double Solution::getAggregativeValue() const {
    double s = 0.0;
    for (double o : objectives_) s += o;
    return s;
}

std::string Solution::toString() const {
    std::ostringstream oss;
    for (int v : vars_) oss << v << " ";
    oss << " | ";
    for (double o : objectives_) oss << o << " ";
    return oss.str();
}