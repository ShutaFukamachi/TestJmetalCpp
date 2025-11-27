#include "NSGAII.h"
#include "Ranking.h"
#include "CrowdingDistanceComparator.h"
#include <iostream>
#include <stdexcept>

NSGAII::NSGAII(Problem *problem)
    : Algorithm(problem),
      population(nullptr),
      offspringPopulation(nullptr),
      unionPopulation(nullptr),
      evaluations(0),
      populationSize(0) {}


NSGAII::~NSGAII() {

    //if (offspringPopulation) { delete offspringPopulation; offspringPopulation = nullptr; }
    //if (unionPopulation)     { delete unionPopulation;     unionPopulation = nullptr; }
}

SolutionSet *NSGAII::execute() {

    populationSize    = *(int *)getInputParameter("populationSize");
    maxEvaluations    = *(int *)getInputParameter("maxEvaluations");
    crossoverOperator = getOperator("crossover");
    mutationOperator  = getOperator("mutation");
    selectionOperator = getOperator("selection");


    population = new SolutionSet(populationSize);
    for (int i = 0; i < populationSize; i++) {
        Solution *sol = new Solution(problem_);
        problem_->evaluate(sol);
        population->add(sol);
        cout << sol->getObjective(0) << " " << sol->getObjective(1) << endl;
    }
    evaluations = populationSize;


    while (evaluations < maxEvaluations) {
        std::cout << "evaluations = " << evaluations << std::endl;

        offspringPopulation = new SolutionSet(populationSize);

        for (int i = 0; i < populationSize && evaluations < maxEvaluations; i += 2) {
            Solution *p1 = (Solution *)selectionOperator->execute(population);
            Solution *p2 = (Solution *)selectionOperator->execute(population);


            void **parents = new void*[2];
            parents[0] = p1;
            parents[1] = p2;


            void **offs = (void **)crossoverOperator->execute(parents);

            delete [] parents;

            Solution *c1 = (Solution *)offs[0];
            Solution *c2 = (Solution *)offs[1];


            delete [] offs;

            mutationOperator->execute(c1);
            mutationOperator->execute(c2);

            problem_->evaluate(c1);
            problem_->evaluate(c2);

            offspringPopulation->add(c1);
            if (offspringPopulation->size() < populationSize)
                offspringPopulation->add(c2);
            else
                delete c2;

            evaluations += 2;
        }



        unionPopulation = new SolutionSet(population->size() + offspringPopulation->size());
       // unionPopulation = population->join(offspringPopulation);population->size() + offspringPopulation->size());
        for (int i = 0; i < population->size(); ++i) {
            unionPopulation->add(new Solution(population->get(i)));
        }


        for (int i = 0; i < offspringPopulation->size(); ++i) {
            unionPopulation->add(new Solution(offspringPopulation->get(i)));
        }
        Ranking ranking(unionPopulation);

        SolutionSet *nextGen = new SolutionSet(populationSize);
        int remain = populationSize;
        int idx = 0;

        while (remain > 0 && idx < ranking.getNumberOfSubfronts()) {
            SolutionSet *front = ranking.getSubfront(idx);
            if (front->size() <= remain) {
                for (int k = 0; k < front->size(); ++k) nextGen->add(new Solution(front->get(k)));
                remain -= front->size();
            } else {
                ranking.crowdingDistanceAssignment(front, problem_->getNumberOfObjectives());
                front->sort(new CrowdingDistanceComparator());
                for (int k = 0; k < remain; ++k) nextGen->add(new Solution(front->get(k)));
                remain = 0;
            }
            ++idx;
        }


        //delete population;
        //delete offspringPopulation; offspringPopulation = nullptr;
        //delete unionPopulation;
        unionPopulation     = nullptr;

        population = nextGen;
    }

    setOutputParameter("evaluations", &evaluations);
    return population;
}



