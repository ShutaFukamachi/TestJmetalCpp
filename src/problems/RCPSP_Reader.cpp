#include "RCPSP_Reader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cctype>

static bool parseLastInt(const std::string &s, int &out) {
    std::istringstream iss(s);
    std::string token;
    bool found = false;
    int val = 0;
    while (iss >> token) {
        size_t i = 0;
        while (i < token.size() && !(std::isdigit((unsigned char)token[i]) || token[i]=='-')) ++i;
        if (i < token.size()) {
            bool neg = false;
            if (token[i] == '-') { neg = true; ++i; }
            long long v = 0;
            bool anydigit = false;
            while (i < token.size() && std::isdigit((unsigned char)token[i])) {
                anydigit = true;
                v = v*10 + (token[i]-'0');
                ++i;
            }
            if (anydigit) {
                if (neg) v = -v;
                val = static_cast<int>(v);
                found = true;
            }
        }
    }
    if (found) { out = val; return true; }
    return false;
}

RCPSP_Instance readPSPLIB_SM(const std::string &filename) {
    std::ifstream in(filename);
    if (!in) throw std::runtime_error("Could not open file: " + filename);

    RCPSP_Instance inst;
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }

    // header: jobs / horizon / renewable
    for (auto &l : lines) {
        if (l.find("jobs (incl. supersource/sink") != std::string::npos) {
            int v;
            if (parseLastInt(l, v)) inst.nJobs = v;
        } else if (l.find("- renewable") != std::string::npos) {
            int v;
            if (parseLastInt(l, v)) inst.nRes = v;
        }
    }

    if (inst.nJobs <= 0) throw std::runtime_error("nJobs not found in header.");
    if (inst.nRes < 0) inst.nRes = 0;

    // init containers
    inst.duration.assign(inst.nJobs, 0);
    inst.demand.assign(inst.nJobs, std::vector<int>(inst.nRes, 0));
    inst.successors.assign(inst.nJobs, std::vector<int>());

    // parse PRECEDENCE RELATIONS
    bool inPre = false;
    for (size_t idx=0; idx<lines.size(); ++idx) {
        auto &l = lines[idx];
        if (!inPre) {
            if (l.find("PRECEDENCE RELATIONS") != std::string::npos) { inPre = true; ++idx; }
            else continue;
        } else {
            // read until blank line or REQUESTS/DURATIONS or RESOURCEAVAILABILITIES
            if (lines[idx].find("REQUESTS/DURATIONS") != std::string::npos || lines[idx].find("************************************************************************") != std::string::npos) break;
            std::istringstream iss(lines[idx]);
            int id, nmodes, nsucc;
            if (!(iss >> id >> nmodes >> nsucc)) {
                // try token parse
                std::vector<int> tok;
                std::istringstream iss2(lines[idx]);
                int x;
                while (iss2 >> x) tok.push_back(x);
                if (tok.size() >= 3) {
                    id = tok[0]; nmodes = tok[1]; nsucc = tok[2];
                    for (size_t k = 0; k < nsucc && 3 + k < tok.size(); ++k) {
                        inst.successors[id-1].push_back(tok[3+k]-1);
                    }
                    continue;
                } else continue;
            }
            for (int k=0;k<nsucc;k++){
                int s; iss >> s;
                if (!iss) break;
                inst.successors[id-1].push_back(s-1); // store 0-based
            }
        }
    }

    // parse REQUESTS/DURATIONS
    bool inReq = false;
    for (size_t idx=0; idx<lines.size(); ++idx) {
        auto &l = lines[idx];
        if (!inReq) {
            if (l.find("REQUESTS/DURATIONS") != std::string::npos) { inReq = true; ++idx; }
            else continue;
        } else {
            if (lines[idx].find("RESOURCEAVAILABILITIES") != std::string::npos || lines[idx].find("************************************************************************") != std::string::npos) break;
            // skip separator lines
            bool hasDigit=false;
            for(char c: lines[idx]) if (std::isdigit((unsigned char)c)) { hasDigit=true; break; }
            if (!hasDigit) continue;
            std::istringstream iss(lines[idx]);
            int id, mode, dur;
            if (!(iss >> id >> mode >> dur)) {
                std::vector<int> tok;
                std::istringstream iss2(lines[idx]);
                int x; while (iss2 >> x) tok.push_back(x);
                if (tok.size()>=3) { id=tok[0]; mode=tok[1]; dur=tok[2]; }
                else continue;
            }
            inst.duration[id-1] = dur;
            for (int r=0;r<inst.nRes;r++){
                int d;
                if (!(iss >> d)) {
                    // demands may continue on next lines – try to read subsequent lines until fill
                    std::string more;
                    std::streampos curpos = in.tellg(); // not used but safe
                    size_t look = idx+1;
                    bool found = false;
                    while (!found && look < lines.size()) {
                        std::istringstream issm(lines[look]);
                        if (!(issm >> d)) { ++look; continue; }
                        found = true;
                        // push back read value into current demand (but careful: this strategy is rarely needed for .sm)
                    }
                    if (!found) d = 0;
                }
                if (r < (int)inst.demand[id-1].size()) inst.demand[id-1][r] = d;
            }
        }
    }

    // parse RESOURCEAVAILABILITIES
    bool inRes = false;
    for (size_t idx=0; idx<lines.size(); ++idx) {
        auto &l = lines[idx];
        if (!inRes) {
            if (l.find("RESOURCEAVAILABILITIES") != std::string::npos) { inRes = true; ++idx; }
            else continue;
        } else {
            // next non-empty numeric tokens include capacities
            std::vector<int> caps;
            size_t look = idx;
            while (caps.size() < (size_t)inst.nRes && look < lines.size()) {
                std::istringstream iss(lines[look]);
                int v;
                while (iss >> v) caps.push_back(v);
                ++look;
            }
            if (caps.size() >= (size_t)inst.nRes) {
                inst.capacity = std::vector<int>(caps.begin(), caps.begin()+inst.nRes);
                break;
            }
        }
    }

    // final checks
    if (inst.duration.size() != (size_t)inst.nJobs) inst.duration.assign(inst.nJobs,0);
    if (inst.demand.size() != (size_t)inst.nJobs) inst.demand.assign(inst.nJobs, std::vector<int>(inst.nRes,0));
    if (inst.successors.size() != (size_t)inst.nJobs) inst.successors.assign(inst.nJobs, std::vector<int>());

    return inst;
}


