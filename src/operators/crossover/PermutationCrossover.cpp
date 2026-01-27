#include "PermutationCrossover.h"
#include <algorithm>
#include <random>
#include <vector>


static void repairToPermutation(std::vector<int> &p, int nJobs) {
    std::vector<int> count(nJobs, 0);


    for (int i = 0; i < nJobs; ++i) {
        int v = p[i];
        if (v < 0 || v >= nJobs) {
            p[i] = -1;
        } else {
            count[v]++;
        }
    }


    std::vector<int> missing;
    missing.reserve(nJobs);
    for (int j = 0; j < nJobs; ++j) {
        if (count[j] == 0) {
            missing.push_back(j);
        }
    }

    int missIdx = 0;


    for (int i = 0; i < nJobs; ++i) {
        int v = p[i];
        if (v == -1) {

            p[i] = missing[missIdx++];
        } else if (count[v] > 1) {

            count[v]--;
            p[i] = missing[missIdx++];
        }
    }


    int pos0 = -1, posLast = -1;
    for (int i = 0; i < nJobs; ++i) {
        if (p[i] == 0)        pos0   = i;
        if (p[i] == nJobs-1)  posLast = i;
    }
    if (pos0 != -1 && pos0 != 0) {
        std::swap(p[0], p[pos0]);
    }
    if (posLast != -1 && posLast != nJobs-1) {
        std::swap(p[nJobs-1], p[posLast]);
    }
}


//PermutationCrossover::PermutationCrossover(double p) : probability(p) {}

void * PermutationCrossover::execute(void * object) {
    void ** parents = (void **)object;
    Solution * p1 = (Solution *)parents[0];
    Solution * p2 = (Solution *)parents[1];

    const int nVars = p1->getNumberOfVariables();
    const int nJobs = nVars / 2;  // 前半: ALR, 後半: schedObj

//    if (nJobs <= 3) {
//        Solution **offs = new Solution*[2];
//        offs[0] = new Solution(*p1);
//        offs[1] = new Solution(*p2);
//        return (void*)offs;
//    }

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution<> prob01(0.0, 1.0);

    // crossoverしない
    if (prob01(gen) > probability) {
        Solution **offs = new Solution*[2];
        offs[0] = new Solution(*p1);
        offs[1] = new Solution(*p2);
        return (void*)offs;
    }

    Variable **v1 = p1->getDecisionVariables();
    Variable **v2 = p2->getDecisionVariables();


    std::vector<int> a(nVars), b(nVars);
    for (int i = 0; i < nVars; ++i) {
        a[i] = v1[i]->getValue();
        b[i] = v2[i]->getValue();
    }

    repairToPermutation(a, nJobs);
    repairToPermutation(b, nJobs);

    std::uniform_int_distribution<> dis(1, nJobs - 2);
    int c1 = dis(gen), c2 = dis(gen);
    while (c1 == c2) c2 = dis(gen);
    if (c1 > c2) std::swap(c1, c2);


    auto ox_make = [&](const std::vector<int>& P, const std::vector<int>& Q) {
        std::vector<int> child(nVars, -1);
        std::vector<bool> used(nJobs, false);

        // 0からc1-1までをPからコピー
        for (int i = 0; i < c1; ++i) {
            child[i] = P[i];
            child[i + nJobs] = P[i + nJobs];
            used[P[i]] = true;
        }
        // c1からc2-1までをQからコピー(重複しないように)
        int j = 0;
        for (int i = c1; i < c2; ++i) {
            while (used[Q[j]]) ++j;
            child[i] = Q[j];
            child[i + nJobs] = Q[j + nJobs];
            used[Q[j]] = true;
            ++j;
        }
        // c2から最後までをPからコピー(重複しないように)
        j = c1;
        for (int i = c2; i < nJobs; ++i) {
            while (used[P[j]]) ++j;
            child[i] = P[j];
            child[i + nJobs] = P[j + nJobs];
            used[P[j]] = true;
            ++j;
        }

//        std::copy(P.begin() + c1, P.begin() + c2 + 1, child.begin() + c1);
//
//
//        for (int i = 0; i < nJobs; ++i) {
//            int v = child[i];
//            if (v >= 0 && v < nJobs) used[v] = true;
//        }
//
//        int posStart = (c2 + 1) % nJobs;
//        int pos = posStart;
//
//        for (int k = 0; k < nJobs; ++k) {
//            int q = Q[(c2 + 1 + k) % nJobs];
//            if (q == 0 || q == nJobs - 1) continue;
//            if (q < 0 || q >= nJobs) continue;
//            if (used[q]) continue;
//
//
//            int steps = 0;
//            while ((child[pos] != -1 || pos == 0 || pos == nJobs - 1) &&
//                   steps < nJobs) {
//                pos = (pos + 1) % nJobs;
//                ++steps;
//            }
//            if (steps >= nJobs) {
//
//                break;
//            }
//            child[pos] = q;
//            used[q] = true;
//        }
//
//
//        std::vector<int> remaining;
//        for (int j = 0; j < nJobs; ++j) {
//            if (!used[j]) remaining.push_back(j);
//        }
//        int idxRem = 0;
//        for (int i = 0; i < nJobs; ++i) {
//            if (child[i] == -1) {
//                if (idxRem < (int)remaining.size()) {
//                    child[i] = remaining[idxRem++];
//                } else {
//                    child[i] = 0; // 念のため
//                }
//            }
//        }
//
//
//        int pos0 = -1, posLast = -1;
//        for (int i = 0; i < nJobs; ++i) {
//            if (child[i] == 0)        pos0   = i;
//            if (child[i] == nJobs-1)  posLast = i;
//        }
//        if (pos0 != -1 && pos0 != 0) {
//            std::swap(child[0], child[pos0]);
//        }
//        if (posLast != -1 && posLast != nJobs-1) {
//            std::swap(child[nJobs-1], child[posLast]);
//        }

        return child;
    };

    auto cA = ox_make(a, b);
    auto cB = ox_make(b, a);


    Solution **offspring = new Solution*[2];
    offspring[0] = new Solution(*p1);
    offspring[1] = new Solution(*p2);


    for (int i = 0; i < nVars; ++i) {
        offspring[0]->getDecisionVariables()[i]->setValue(cA[i]);
        offspring[1]->getDecisionVariables()[i]->setValue(cB[i]);
    }
    // ↓ ox_makeで同時に行うようにしたので不要
//
//    std::bernoulli_distribution coin(0.5);
//    for (int j = 0; j < nJobs; ++j) {
//        int idx  = nJobs + j;
//        int bit1 = v1[idx]->getValue();
//        int bit2 = v2[idx]->getValue();
//        offspring[0]->getDecisionVariables()[idx]->setValue(coin(gen) ? bit1 : bit2);
//        offspring[1]->getDecisionVariables()[idx]->setValue(coin(gen) ? bit1 : bit2);
//    }

    return (void*)offspring;
}




