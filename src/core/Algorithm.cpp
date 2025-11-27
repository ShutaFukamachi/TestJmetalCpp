
#include "Algorithm.h"

Algorithm::Algorithm(Problem *problem) : problem_(problem) {}

Algorithm::~Algorithm() {

}

void Algorithm::addOperator(std::string name, Operator *op) { operators_[name] = op; }
Operator * Algorithm::getOperator(std::string name) { return operators_[name]; }

void Algorithm::setInputParameter(std::string name, void *value) { inputParameters_[name] = value; }
void * Algorithm::getInputParameter(std::string name) { return inputParameters_[name]; }
void * Algorithm::getInputParameter(std::string name, void *defVal) {
    if (inputParameters_.find(name) == inputParameters_.end()) inputParameters_[name] = defVal;
    return inputParameters_[name];
}

