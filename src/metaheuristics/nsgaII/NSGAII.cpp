#include "NSGAII.h"
#include "Ranking.h"
#include "CrowdingDistanceComparator.h"
#include <iostream>
#include <stdexcept>


#include "problems/RCPSP_Problem.h"

NSGAII::NSGAII(Problem *problem)
    : Algorithm(problem),
      population(nullptr),
      offspringPopulation(nullptr),
      unionPopulation(nullptr),
      evaluations(0),
      populationSize(0) {}

NSGAII::~NSGAII() {

}

SolutionSet *NSGAII::execute() {
    populationSize    = *(int *)getInputParameter("populationSize");
    maxEvaluations    = *(int *)getInputParameter("maxEvaluations");
    crossoverOperator = getOperator("crossover");
    mutationOperator  = getOperator("mutation");
    selectionOperator = getOperator("selection");

    // 追加: 初期個体のポインタを取得（渡されていなければ nullptr）
    SolutionSet *seedPopulation = nullptr;
    {
        void *ptr = getInputParameter("initialPopulation");
        if (ptr != nullptr) {
            seedPopulation = static_cast<SolutionSet *>(ptr);
        }
    }


    // 初期個体生成

    population = new SolutionSet(populationSize);
    int filled = 0;

    // Optional: toggle local search by input parameter "useLocalSearch" (int: 0/1).
    bool useLocalSearch = true;
    {
        void *pLS = getInputParameter("useLocalSearch");
        if (pLS != nullptr) {
            int v = *static_cast<int *>(pLS);
            useLocalSearch = (v != 0);
        }
    }


    if (seedPopulation != nullptr) {
        int seedSize = seedPopulation->size();
        for (int i = 0; i < seedSize && filled < populationSize; ++i) {
            Solution *orig = seedPopulation->get(i);
            Solution *copy = new Solution(orig);
            problem_->evaluate(copy);
            population->add(copy);
            ++filled;
        }
        std::cout << "[NSGAII] Seeded " << filled << " solutions from initialPopulation\n";
    }

    //ランダムトポロジカルソートで生成
    for (; filled < populationSize; ++filled) {
        Solution *sol = nullptr;

        if (auto rcpsp = dynamic_cast<RCPSP_Problem*>(problem_)) {
            sol = rcpsp->createRandomTopoSolution();  // 先ほど作った関数
        } else {
            sol = new Solution(problem_);
        }

        problem_->evaluate(sol);
        population->add(sol);
    }

    evaluations = population->size();


    // メインループ

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

            // ここで RCPSP 用の局所探索を追加
            if (useLocalSearch) {
                if (auto rcpsp = dynamic_cast<RCPSP_Problem*>(problem_)) {
                    // -1 or 0 なら「改善できなくなるまで」
                    rcpsp->localSearchOnActivityOrder(c1, -1);
                    rcpsp->localSearchOnActivityOrder(c2, -1);

                    rcpsp->localSearchOnSchedObj(c1, -1);
                    rcpsp->localSearchOnSchedObj(c2, -1);
                }
            }




            offspringPopulation->add(c1);
            if (offspringPopulation->size() < populationSize)
                offspringPopulation->add(c2);
            else
                delete c2;

            evaluations += 2;
        }

        // 親 + 子 = unionPopulation
        unionPopulation = new SolutionSet(population->size() + offspringPopulation->size());
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
                for (int k = 0; k < front->size(); ++k)
                    nextGen->add(new Solution(front->get(k)));
                remain -= front->size();
            } else {
                ranking.crowdingDistanceAssignment(front, problem_->getNumberOfObjectives());
                front->sort(new CrowdingDistanceComparator());
                for (int k = 0; k < remain; ++k)
                    nextGen->add(new Solution(front->get(k)));
                remain = 0;
            }
            ++idx;
        }

        population = nextGen;
        // offspringPopulation / unionPopulation のポインタは上書きしているだけ
    }
    setOutputParameter("evaluations", &evaluations);
    return population;
}



