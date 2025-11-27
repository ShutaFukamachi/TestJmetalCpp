#ifndef BINARYTOURNAMENT2_H
#define BINARYTOURNAMENT2_H

#include "Selection.h"
#include "Comparator.h"
#include <map>

class BinaryTournament2 : public Selection {
public:
    explicit BinaryTournament2(std::map<std::string, void *> parameters);
    explicit BinaryTournament2(Comparator *comparator);
    ~BinaryTournament2();

    void *execute(void *object);

private:
    Comparator *comparator_ = nullptr;
};

#endif
