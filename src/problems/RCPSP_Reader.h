#pragma once
#include <string>
#include <vector>

struct RCPSP_Instance {
    int nJobs = 0;
    int nRes  = 0;
    std::vector<int> capacity;                    // size nRes
    std::vector<int> duration;                    // size nJobs (1-based in file -> store 0-based index)
    std::vector<std::vector<int>> demand;         // [job][res]
    std::vector<std::vector<int>> successors;     // [job] -> list of successor indices (0-based)
};

RCPSP_Instance readPSPLIB_SM(const std::string &filename);

