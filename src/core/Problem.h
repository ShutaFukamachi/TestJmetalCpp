#pragma once
#ifndef __PROBLEM__
#define __PROBLEM__

#include <string>
#include <vector>
#include <iostream>
#include "Solution.h"

class Solution;

class Problem {
protected:
    int    numberOfVariables_   = 0;
    int    numberOfObjectives_  = 0;
    int    numberOfConstraints_ = 0;
    std::string problemName_;
    std::vector<double> lowerLimit_;
    std::vector<double> upperLimit_;

public:
    Problem() = default;
    virtual ~Problem() = 0;

    int    getNumberOfVariables()  const { return numberOfVariables_; }
    void   setNumberOfVariables(int n)   { numberOfVariables_ = n; }
    int    getNumberOfObjectives() const { return numberOfObjectives_; }
    void   setNumberOfObjectives(int n)  { numberOfObjectives_ = n; }
    int    getNumberOfConstraints()const { return numberOfConstraints_; }

    double getLowerLimit(int i) const {
        if (i < 0 || i >= (int)lowerLimit_.size()) return 0.0;
        return lowerLimit_[i];
    }
    double getUpperLimit(int i) const {
        if (i < 0 || i >= (int)upperLimit_.size()) return 0.0;
        return upperLimit_[i];
    }

    virtual void evaluate(Solution* solution) = 0;
    virtual void evaluateConstraints(Solution* solution) {}
    virtual bool evaluateStopConstraints(Solution* solution) { return false; }

    std::string getName() const { return problemName_; }
};

#endif