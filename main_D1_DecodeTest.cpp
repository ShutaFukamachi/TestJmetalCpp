// ============================================================
//  main_D1_DecodeTest.cpp  (D1DecodeTest ターゲット)
//
//  診断1（表現可能性テスト）: MIPの中間部の解から抽出した活動リスト（順列）を
//  既存デコーダ（MaxShift / MaxShiftDur）にそのまま与え、max_shift/rhoキーを
//  一様に掃引しながらデコードする。既存デコーダを一切変更せず「呼ぶだけ」。
//
//  問い: その順列で、MIPのその中間点(m_MIP, c_MIP)を実行可能な形で
//        再現・支配できるか？
//    再現/支配できる → 表現力はある。探索が見つけられていないだけ（探索の問題）
//    できない        → 窓（連続区間探索）という表現形式の限界（構造の問題）
//
//  使い方:
//    D1DecodeTest --instance j30.sm/j3034_1.sm --rr 0.5 --rv 0 \
//                  --perm 0,3,1,5,... --encoding maxshift
//                  [--refms 94 --refcost 250000]
// ============================================================
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Solution.h"
#include "problems/RCPSP_Problem_MaxShift.h"
#include "problems/RCPSP_Problem_MaxShiftDur.h"

using namespace std;

static vector<int> parseIntList(const string &s) {
    vector<int> out;
    stringstream ss(s);
    string tok;
    while (getline(ss, tok, ',')) {
        if (!tok.empty()) out.push_back(stoi(tok));
    }
    return out;
}

int main(int argc, char **argv) {
    try {
        string instanceFile, permStr, encoding = "maxshift";
        double rr = 0.0;
        bool rv = false;
        double refMs = -1, refCost = -1;

        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "--instance" && i + 1 < argc) instanceFile = argv[++i];
            else if (a == "--rr" && i + 1 < argc) rr = stod(argv[++i]);
            else if (a == "--rv" && i + 1 < argc) rv = (stoi(argv[++i]) != 0);
            else if (a == "--perm" && i + 1 < argc) permStr = argv[++i];
            else if (a == "--encoding" && i + 1 < argc) encoding = argv[++i];
            else if (a == "--refms" && i + 1 < argc) refMs = stod(argv[++i]);
            else if (a == "--refcost" && i + 1 < argc) refCost = stod(argv[++i]);
        }
        if (instanceFile.empty() || permStr.empty()) {
            cerr << "usage: D1DecodeTest --instance <file> --rr <r> --rv <0/1> "
                    "--perm <csv> --encoding maxshift|maxshiftdur [--refms M --refcost C]\n";
            return 1;
        }

        vector<int> perm = parseIntList(permStr);

        cout << "instance=" << instanceFile << " rr=" << rr << " rv=" << rv
             << " encoding=" << encoding << " n=" << perm.size() << "\n";
        if (refMs > 0) cout << "REFERENCE (MIP) point: ms=" << refMs << " cost=" << refCost << "\n";

        cout << "window_param  achieved_ms  achieved_cost  dominates_ref  matches_ref_ms\n";

        if (encoding == "maxshift") {
            // strategy=4: halfT=T/2、生産で使う最大窓。評価時にvars[n+j]はこのhalfTへclipされる
            RCPSP_Problem_MaxShift prob(instanceFile, 4, rr, rv);
            const int n = prob.getNumJobs();
            const int halfT = prob.getEffectiveHalfT();
            vector<int> sweep = {0, halfT/16, halfT/8, halfT/4, halfT*3/8, halfT/2,
                                  halfT*3/4, halfT};
            for (int w : sweep) {
                Solution sol(&prob);
                auto &vars = sol.getVars();
                for (int j = 0; j < n; ++j) vars[j] = perm[j];
                for (int j = 0; j < n; ++j) vars[n + j] = w;
                prob.evaluate(&sol);
                double ms = sol.getObjective(0), cost = sol.getObjective(1);
                bool dom = (refMs > 0) && (ms <= refMs && cost <= refCost && (ms < refMs || cost < refCost));
                bool matchMs = (refMs > 0) && (std::abs(ms - refMs) < 1e-6);
                cout << w << "  " << ms << "  " << cost << "  "
                     << (dom ? "YES" : "no") << "  " << (matchMs ? "YES" : "no") << "\n";
            }
        } else if (encoding == "maxshiftdur") {
            RCPSP_Problem_MaxShiftDur prob(instanceFile, 4, rr, rv);
            const int n = prob.getNumJobs();
            const int Rmax = RCPSP_Problem_MaxShiftDur::R;
            vector<int> sweep = {0, Rmax/16, Rmax/8, Rmax/4, Rmax*3/8, Rmax/2,
                                  Rmax*3/4, Rmax};
            for (int w : sweep) {
                Solution sol(&prob);
                auto &vars = sol.getVars();
                for (int j = 0; j < n; ++j) vars[j] = perm[j];
                for (int j = 0; j < n; ++j) vars[n + j] = w;
                prob.evaluate(&sol);
                double ms = sol.getObjective(0), cost = sol.getObjective(1);
                bool dom = (refMs > 0) && (ms <= refMs && cost <= refCost && (ms < refMs || cost < refCost));
                bool matchMs = (refMs > 0) && (std::abs(ms - refMs) < 1e-6);
                cout << w << "  " << ms << "  " << cost << "  "
                     << (dom ? "YES" : "no") << "  " << (matchMs ? "YES" : "no") << "\n";
            }
        } else {
            cerr << "unknown --encoding " << encoding << "\n";
            return 1;
        }

        return 0;
    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
