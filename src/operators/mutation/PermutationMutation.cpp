#include "PermutationMutation.h"
#include "RCPSP_Problem.h"
#include <random>

//PermutationMutation::PermutationMutation(double p) : probability(p) {}

void * PermutationMutation::execute(void * object) {
    Solution * s = (Solution *)object;
    const int nVars = s->getNumberOfVariables();
    const int nJobs = nVars / 2;

    if (nJobs <= 3) return s;

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution<> prob01(0.0, 1.0);

    Variable **vars = s->getDecisionVariables();


    std::uniform_int_distribution<> pos(1, nJobs - 2); // ダミー0, nJobs-1を避ける

    if (prob01(gen) < probability) {
        int i = pos(gen), j = pos(gen);
        while (j == i) j = pos(gen);
        if (i > j) std::swap(i, j);

        int vi = vars[i]->getValue();
        int vj = vars[j]->getValue();

        bool canSwap = true;
        for (int k = i; k <= j; ++k) {
            int vk = vars[k]->getValue();
            if (mat[vi][vk] == 1 || mat[vk][vj] == 1) {
                canSwap = false;
                break;
            }
        }
        if (canSwap) {
            vars[i]->setValue(vj);
            vars[j]->setValue(vi);
        }
    }


    if (prob01(gen) < probability) {
        std::uniform_int_distribution<> bitPos(nJobs, nVars - 1);
        int idx = bitPos(gen);
        int v   = vars[idx]->getValue();
        vars[idx]->setValue(v ? 0 : 1);
    }

    return s;
}


