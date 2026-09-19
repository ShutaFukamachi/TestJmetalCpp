#include "RCPSP_MIP_Transform.h"

// ============================================================
//  P3: 単位分割
// ============================================================
RCPSP_Instance buildUnitSplitInstance(const RCPSP_Instance &orig, UnitSplitMapping &mapping) {
    int nOrig = orig.nJobs;
    mapping.firstUnit.assign(nOrig, -1);
    mapping.lastUnit.assign(nOrig, -1);
    mapping.unitToOrig.clear();

    RCPSP_Instance out;
    out.nRes       = orig.nRes;
    out.capacity   = orig.capacity;
    out.capacity_t = orig.capacity_t;   // 元のまま維持（容量プロファイルは変換しない）

    for (int j = 0; j < nOrig; ++j) {
        int d = orig.duration[j];
        if (d <= 0) {
            // ダミー（開始・終了、所要時間0）はそのまま1ジョブ
            int newId = (int)out.duration.size();
            out.duration.push_back(0);
            out.demand.push_back(orig.demand[j]);
            mapping.firstUnit[j] = newId;
            mapping.lastUnit[j]  = newId;
            mapping.unitToOrig.push_back(j);
        } else {
            int firstId = (int)out.duration.size();
            for (int u = 0; u < d; ++u) {
                out.duration.push_back(1);
                out.demand.push_back(orig.demand[j]);   // 単位活動の資源需要は元と同じ
                mapping.unitToOrig.push_back(j);
            }
            int lastId = (int)out.duration.size() - 1;
            mapping.firstUnit[j] = firstId;
            mapping.lastUnit[j]  = lastId;
        }
    }
    out.nJobs = (int)out.duration.size();

    out.successors.assign(out.nJobs, {});
    // 鎖状の先行制約: unit_i -> unit_{i+1}（同一元ジョブ内）
    for (int j = 0; j < nOrig; ++j) {
        int f = mapping.firstUnit[j];
        int l = mapping.lastUnit[j];
        for (int u = f; u < l; ++u) {
            out.successors[u].push_back(u + 1);
        }
    }
    // 元の先行制約: lastUnit(j) -> firstUnit(succ)
    for (int j = 0; j < nOrig; ++j) {
        int l = mapping.lastUnit[j];
        for (int s : orig.successors[j]) {
            if (s < 0 || s >= nOrig) continue;
            out.successors[l].push_back(mapping.firstUnit[s]);
        }
    }

    return out;
}

// ============================================================
//  P2: 時間軸圧縮
// ============================================================
TimeCompression buildTimeCompression(const std::vector<std::vector<int>> &capacity_t) {
    TimeCompression tc;
    if (capacity_t.empty() || capacity_t[0].empty()) return tc;

    int nRes = (int)capacity_t.size();
    int T    = (int)capacity_t[0].size();
    tc.T_original = T;
    tc.originalToCompressed.assign(T, -1);
    tc.compressedToOriginal.reserve(T);

    for (int t = 0; t < T; ++t) {
        bool allZero = true;
        for (int k = 0; k < nRes; ++k) {
            if (capacity_t[k][t] != 0) { allZero = false; break; }
        }
        if (!allZero) {
            tc.originalToCompressed[t] = (int)tc.compressedToOriginal.size();
            tc.compressedToOriginal.push_back(t);
        } else {
            ++tc.numRemoved;
        }
    }
    tc.T_compressed = (int)tc.compressedToOriginal.size();
    return tc;
}

RCPSP_Instance buildCompressedInstance(const RCPSP_Instance &orig, const TimeCompression &tc) {
    RCPSP_Instance out = orig;  // duration/demand/successors/capacity/nJobs/nRes は元のまま
    out.capacity_t.assign(orig.nRes, std::vector<int>(tc.T_compressed, 0));
    for (int k = 0; k < orig.nRes; ++k) {
        for (int tp = 0; tp < tc.T_compressed; ++tp) {
            out.capacity_t[k][tp] = orig.capacity_t[k][tc.compressedToOriginal[tp]];
        }
    }
    return out;
}

std::vector<std::vector<double>> buildRemappedCostTable(
        const RCPSP_MIP_Solver &solver,
        const TimeCompression  &tc,
        int                     horizonForLookup) {
    int nRes = solver.getNumResources();
    std::vector<std::vector<double>> table(nRes, std::vector<double>(tc.T_compressed, 0.0));
    for (int tp = 0; tp < tc.T_compressed; ++tp) {
        int tOrig = tc.compressedToOriginal[tp];
        for (int k = 0; k < nRes; ++k) {
            table[k][tp] = solver.getResourceCostAt(k, tOrig, horizonForLookup);
        }
    }
    return table;
}
