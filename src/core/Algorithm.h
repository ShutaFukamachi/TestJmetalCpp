// Algorithm.h
#ifndef __ALGORITHM__
#define __ALGORITHM__

#include <string>
#include "SolutionSet.h"
#include "Operator.h"
#include "Problem.h"
#include <map>

class Algorithm {
public:
    Algorithm(Problem *problem);
    virtual ~Algorithm();
    virtual SolutionSet * execute() = 0;

    void addOperator(std::string name, Operator *operator_);
    Operator * getOperator(std::string name);

    void setInputParameter(std::string name, void *value);
    void * getInputParameter(std::string name);
    void * getInputParameter(std::string name, void *defaultValue);

    void setOutputParameter(std::string name, void *value) { outputParameters_[name] = value; }
    void * getOutputParameter(std::string name) { return outputParameters_[name]; }

    Problem * getProblem() { return problem_; }

protected:
    Problem *problem_;
    std::map<std::string, Operator *> operators_;
    std::map<std::string, void *> inputParameters_;
    std::map<std::string, void *> outputParameters_;
};

#endif
