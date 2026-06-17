#pragma once
#ifndef __SOLUTION__
#define __SOLUTION__

#include <string>
#include <vector>
#include <limits>
#include <sstream>

class Problem;

class Solution {
public:
    Solution() = default;
    explicit Solution(Problem* problem);

    // Value copy semantics (vectors handle memory automatically)
    Solution(const Solution&) = default;
    Solution& operator=(const Solution&) = default;
    Solution(Solution&&) = default;
    Solution& operator=(Solution&&) = default;
    ~Solution() = default;

    // Pointer-based copy for backward compatibility with existing code
    // that does: new Solution(ptr)
    explicit Solution(Solution* other) : Solution(*other) {}

    // --- Variable access (replaces Variable**) ---
    int  getVar(int i) const     { return vars_[i]; }
    void setVar(int i, int v)    { vars_[i] = v; }
    std::vector<int>&       getVars()       { return vars_; }
    const std::vector<int>& getVars() const { return vars_; }
    int getNumberOfVariables() const { return (int)vars_.size(); }

    // --- Objectives ---
    double getObjective(int i) const;
    void   setObjective(int i, double v);
    int    getNumberOfObjectives() const { return (int)objectives_.size(); }

    // --- Metadata ---
    double getFitness() const               { return fitness_; }
    void   setFitness(double v)             { fitness_ = v; }
    double getCrowdingDistance() const      { return crowdingDistance_; }
    void   setCrowdingDistance(double v)    { crowdingDistance_ = v; }
    int    getRank() const                  { return rank_; }
    void   setRank(int v)                   { rank_ = v; }
    double getOverallConstraintViolation() const  { return overallConstraintViolation_; }
    void   setOverallConstraintViolation(double v){ overallConstraintViolation_ = v; }
    int    getNumberOfViolatedConstraints() const  { return numberOfViolatedConstraints_; }
    void   setNumberOfViolatedConstraints(int v)   { numberOfViolatedConstraints_ = v; }
    bool   isMarked() const     { return marked_; }
    void   mark()               { marked_ = true; }
    void   unMark()             { marked_ = false; }
    int    getLocation() const  { return location_; }
    void   setLocation(int v)   { location_ = v; }
    double getKDistance() const { return kDistance_; }
    void   setKDistance(double v){ kDistance_ = v; }
    double getDistanceToSolutionSet() const { return distanceToSolutionSet_; }
    void   setDistanceToSolutionSet(double v){ distanceToSolutionSet_ = v; }
    double getAggregativeValue() const;
    std::string toString() const;

    Problem* getProblem() const { return problem_; }

    // RCPSP-specific fields (used by evaluate() and copied normally)
    std::vector<int> startTimes_;
    std::vector<std::vector<int>> execSlots_;

private:
    Problem* problem_ = nullptr;
    std::vector<int>    vars_;
    std::vector<double> objectives_;
    double fitness_                    = 0.0;
    double crowdingDistance_           = 0.0;
    double kDistance_                  = 0.0;
    double distanceToSolutionSet_      = std::numeric_limits<double>::max();
    double overallConstraintViolation_ = 0.0;
    int    numberOfViolatedConstraints_ = 0;
    bool   marked_   = false;
    int    rank_     = 0;
    int    location_ = 0;
};

#endif