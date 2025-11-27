#ifndef __RANKING__
#define __RANKING__

#include "SolutionSet.h"
#include "Comparator.h"
#include "DominanceComparator.h"
#include "OverallConstraintViolationComparator.h"
#include <iostream>
using namespace std;

class Ranking {
public:
    Ranking(SolutionSet *solutionSet);
    virtual ~Ranking();
    SolutionSet *getSubfront(int rank);
    int getNumberOfSubfronts();


    void crowdingDistanceAssignment(SolutionSet *front, int numberOfObjectives);

private:
    int numberOfSubfronts_;
    SolutionSet **ranking_;
    SolutionSet *solutionSet_;
    Comparator *dominance_;
    Comparator *constraint_;
};

#endif

