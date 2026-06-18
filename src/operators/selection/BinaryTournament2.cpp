#include "BinaryTournament2.h"
#include "SolutionSet.h"
#include "CrowdingDistanceComparator.h"
#include <random>

BinaryTournament2::BinaryTournament2(std::map<std::string, void *> parameters)
    : Selection(parameters), comparator_(nullptr) {}

BinaryTournament2::BinaryTournament2(Comparator *comp)
    : Selection({}), comparator_(comp) {}

BinaryTournament2::~BinaryTournament2() {}

void *BinaryTournament2::execute(void *object) {
    SolutionSet *pop = static_cast<SolutionSet *>(object);
    int n = pop->size();
    if (n == 0) return nullptr;
    if (n == 1) return pop->get(0);

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, n - 1);

    int i1 = dist(gen), i2 = dist(gen);
    while (i1 == i2) i2 = dist(gen);

    Solution *s1 = pop->get(i1);
    Solution *s2 = pop->get(i2);

    if (comparator_) {
        int flag = comparator_->compare(s1, s2);
        if (flag < 0) return s1;
        if (flag > 0) return s2;
    }


    if (s1->getCrowdingDistance() > s2->getCrowdingDistance()) return s1;
    if (s2->getCrowdingDistance() > s1->getCrowdingDistance()) return s2;


    return (dist(gen) & 1) ? s1 : s2;
}
