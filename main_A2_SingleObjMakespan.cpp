// ============================================================
//  main_A2_SingleObjMakespan.cpp  (A2Makespan ターゲット)
//
//  「工期ギャップの原因切り分け」診断ハーネス（改善ではなく診断が目的）。
//
//  問い: 純粋に makespan だけを最小化する GA なら、MIP の工期に近づけるのか？
//    近づける   → 2目的NSGA-IIの選択圧不足が確定（Phase 2: 良い順列を2目的ランに注入）
//    近づけない → オペレータの限界が確定（工期改善は打ち切り、P2/P3へ）
//
//  設計（新規オペレータは作らない。既存クラスを流用）:
//    - RCPSP_Problem_MaxShift を strategy=1 で構築する。
//      getEffectiveHalfT() は strategy=1 のとき 0 を返す（EST固定・makespan専用、
//      クラス既存のドキュメント通り）ので、evaluate() 内で
//      maxShift[j] = clip(vars[n+j], 0, 0) = 0 に必ずクリップされ、
//      デコードは常に Serial SGS の EST 配置になる（コード変更不要）。
//    - 交叉: MaxShiftCrossover（既存）。変異: MaxShiftMutation（既存、
//      halfT_=0 のため第2段階＝max_shift変異は常に0のまま＝no-op化される）。
//    - 選択: 本ハーネス独自の単純な makespan トーナメント（新規「オペレータ」
//      ではなく、2個体を比較して良い方を残すだけの十数行のインライン処理）。
//    - 世代交代: (μ+λ) エリート打ち切り（親100+子100→ms昇順で上位100を残す）。
//
//  マルチスタート: 各 (instance, condition) を R 回（既定10）独立実行し、
//    best / mean / worst の ms* を記録してノイズ床を把握する（.miss_memory/031
//    の教訓：単一試行の成功を一般化しない）。
//
//  評価回数: 1 run あたり popSize=100, maxEvaluations=100000
//    （2目的ラン1ストラテジー分と同一予算。公平性のため揃える）。
//
//  出力:
//    results/FUN_A2/<prefix>/SCHED_A2_<prefix>_<cond>.txt   (R回中の最良解1本、
//        SCHED_MIP/SCHED_ENC と同一フォーマット。verify_precedence.py 系で検証可能)
//    analysis/gap48/a2_raw_runs.csv  (instance,condition,run_id,best_ms,best_cost,evals)
//
//  使い方:
//    A2Makespan                     # 既定8インスタンス×6条件×R=10
//    A2Makespan --instances j309_1,j3034_1 --R 5
// ============================================================
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

#include "Solution.h"
#include "operators/crossover/MaxShiftCrossover.h"
#include "operators/mutation/MaxShiftMutation.h"
#include "problems/RCPSP_Conditions.h"
#include "problems/RCPSP_Problem_MaxShift.h"

using namespace std;

static mutex g_printMutex;
static void syncPrint(const string &msg) {
    lock_guard<mutex> lk(g_printMutex);
    cout << msg;
}

static string toCondTag(double rr, bool rv) {
    int rrInt = static_cast<int>(round(rr * 100));
    char buf[32];
    snprintf(buf, sizeof(buf), "RR%03d_RV%d", rrInt, rv ? 1 : 0);
    return string(buf);
}

static string toBaseNoExt(const string &path) {
    string s = path;
    size_t p = s.find_last_of("/\\");
    if (p != string::npos) s = s.substr(p + 1);
    size_t dot = s.find_last_of('.');
    if (dot != string::npos) s = s.substr(0, dot);
    return s;
}

struct RunResult {
    double bestMs   = 1e18;
    double bestCost = 1e18;
    int    evalsUsed = 0;
    vector<int> startTimes;  // 最良解のスケジュール（検証・出力用）
    // 最終集団から ms 昇順で重複順列を除いた上位 topKPerms 件（Phase2 シード抽出用）。
    // 各要素: (ms, cost, activity-list[0..n-1])
    vector<std::tuple<double,double,vector<int>>> topPerms;
};

// ============================================================
//  単目的 makespan GA 本体（1 run 分）
// ============================================================
static RunResult runSingleObjMakespanGA(const string &instanceFile,
                                         double rr, bool rv,
                                         int popSize, int maxEvaluations,
                                         unsigned seed,
                                         int topKPerms = 0)
{
    // strategy=1: getEffectiveHalfT()=0 → 全ジョブ max_shift は evaluate() 内で
    // 必ず 0 にクリップされる（EST固定）。クラス既存の仕様通りで新規コード不要。
    RCPSP_Problem_MaxShift prob(instanceFile, /*strategy=*/1, rr, rv);
    prob.resetEvalCounter();

    mt19937 rng(seed);
    uniform_int_distribution<int> pick(0, popSize - 1);

    double crossP = 0.9;
    double mutP   = 1.0 / (double)prob.getNumberOfVariables();
    MaxShiftCrossover crossover(crossP);
    MaxShiftMutation  mutation(mutP, &prob);

    int evalsUsed = 0;
    auto evalOne = [&](Solution *s) {
        prob.evaluate(s);
        ++evalsUsed;
    };

    // ---- 初期集団 ----
    vector<Solution*> pop;
    pop.reserve(popSize);
    for (int i = 0; i < popSize; ++i) {
        Solution *s = prob.createRandomTopoSolution();
        evalOne(s);
        pop.push_back(s);
    }

    auto byMs = [](Solution *a, Solution *b) {
        return a->getObjective(0) < b->getObjective(0);
    };

    // ---- 世代ループ: (μ+λ) エリート打ち切り ----
    while (evalsUsed < maxEvaluations) {
        vector<Solution*> offspring;
        offspring.reserve(popSize);
        while ((int)offspring.size() < popSize && evalsUsed < maxEvaluations) {
            // makespan トーナメント選択（2個体を見て良い方を親に）
            auto tournamentPick = [&]() -> Solution* {
                Solution *a = pop[pick(rng)];
                Solution *b = pop[pick(rng)];
                return byMs(a, b) ? a : b;
            };
            Solution *p1 = tournamentPick();
            Solution *p2 = tournamentPick();

            void *parents[2] = {(void*)p1, (void*)p2};
            Solution **children = (Solution**)crossover.execute((void*)parents);

            for (int c = 0; c < 2 && (int)offspring.size() < popSize; ++c) {
                mutation.execute((void*)children[c]);
                evalOne(children[c]);
                offspring.push_back(children[c]);
            }
            // crossoverが2個返すがoffspringが1個で埋まった場合の残りを破棄
            for (int c = 0; c < 2; ++c) {
                bool used = false;
                for (auto *o : offspring) if (o == children[c]) { used = true; break; }
                if (!used) delete children[c];
            }
            delete[] children;
        }

        // (μ+λ): 親+子をまとめてms昇順ソートし上位popSizeを残す
        vector<Solution*> combined;
        combined.reserve(pop.size() + offspring.size());
        for (auto *s : pop) combined.push_back(s);
        for (auto *s : offspring) combined.push_back(s);
        sort(combined.begin(), combined.end(), byMs);

        vector<Solution*> nextPop(combined.begin(), combined.begin() + popSize);
        for (size_t i = popSize; i < combined.size(); ++i) delete combined[i];
        pop = nextPop;
    }

    sort(pop.begin(), pop.end(), byMs);
    Solution *best = pop[0];

    RunResult result;
    result.bestMs   = best->getObjective(0);
    result.bestCost = best->getObjective(1);
    result.evalsUsed = evalsUsed;
    result.startTimes = prob.computeStartTimes(best);

    if (topKPerms > 0) {
        const int n = prob.getNumJobs();
        vector<vector<int>> seen;
        for (auto *s : pop) {
            if ((int)result.topPerms.size() >= topKPerms) break;
            vector<int> actList(n);
            for (int j = 0; j < n; ++j) actList[j] = s->getVars()[j];
            bool dup = false;
            for (auto &sv : seen) if (sv == actList) { dup = true; break; }
            if (dup) continue;
            seen.push_back(actList);
            result.topPerms.emplace_back(s->getObjective(0), s->getObjective(1), actList);
        }
    }

    for (auto *s : pop) delete s;
    return result;
}

// ============================================================
//  SCHED_A2 ファイル出力（SCHED_MIP/SCHED_ENC と同一フォーマット）
// ============================================================
static void writeSchedA2(const string &prefix, const string &ctag,
                          RCPSP_Problem_MaxShift &probForMeta,
                          double ms, double cost, const vector<int> &start)
{
    const string outDir = "results/FUN_A2/" + prefix + "/";
    fs::create_directories(outDir);
    const string path = outDir + "SCHED_A2_" + prefix + "_" + ctag + ".txt";
    ofstream f(path.c_str());

    int nJobs = probForMeta.getNumJobs();
    int nRes  = probForMeta.getNumResources();
    f << nJobs << " " << nRes << "\n";

    const auto &dur = probForMeta.getDurations();
    for (int j = 0; j < nJobs; ++j) { f << dur[j]; if (j + 1 < nJobs) f << " "; }
    f << "\n";

    const auto &demand = probForMeta.getDemand();
    for (int j = 0; j < nJobs; ++j) {
        for (int k = 0; k < nRes; ++k) { f << demand[j][k]; if (k + 1 < nRes) f << " "; }
        f << "\n";
    }

    const auto &cap = probForMeta.getCapacity();
    for (int k = 0; k < nRes; ++k) { f << cap[k]; if (k + 1 < nRes) f << " "; }
    f << "\n";

    const auto &cap_t = probForMeta.getCapacityT();
    if (cap_t.empty() || cap_t[0].empty()) {
        f << "0\n";
    } else {
        int T_cap = (int)cap_t[0].size();
        f << T_cap << "\n";
        for (int k = 0; k < nRes; ++k) {
            for (int t = 0; t < T_cap; ++t) { f << cap_t[k][t]; if (t + 1 < T_cap) f << " "; }
            f << "\n";
        }
    }

    f << 1 << "\n";  // フロントサイズ=1（R回中の最良解のみ出力）
    f << ms << " " << cost;
    for (int j = 0; j < nJobs; ++j)
        f << " " << (j < (int)start.size() ? start[j] : 0);
    f << "\n";
}

// ============================================================
//  PERM_A2 ファイル出力（Phase2 シード注入用の順列プール）
//    各行: ms cost perm[0] perm[1] ... perm[n-1]
// ============================================================
static void writePermA2(const string &prefix, const string &ctag,
                         const vector<std::tuple<double,double,vector<int>>> &perms)
{
    const string outDir = "results/FUN_A2/" + prefix + "/";
    fs::create_directories(outDir);
    const string path = outDir + "PERM_A2_" + prefix + "_" + ctag + ".txt";
    ofstream f(path.c_str());
    for (auto &[ms, cost, perm] : perms) {
        f << ms << " " << cost;
        for (int v : perm) f << " " << v;
        f << "\n";
    }
}

int main(int argc, char **argv) {
    try {
        vector<string> instances = {
            "j3034_1", "j309_1", "j3038_1", "j3011_1", "j3022_1", "j3010_1",  // 工期gap上位+j309_1
            "j3019_1", "j3048_1",  // 対照（工期gapが小さい方、6/6条件でMIP実行可能）
        };
        int R = 10;
        int popSize = 100;
        int maxEvaluations = 100000;
        int topK = 0;          // >0 のとき PERM_A2_*.txt を出力（Phase2 シード抽出モード）
        int perRunTopM = 5;    // 1 run あたり収集する上位個体数（プール後に重複除去してtopKへ）

        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "--R" && i + 1 < argc) { R = stoi(argv[++i]); }
            else if (a == "--instances" && i + 1 < argc) {
                instances.clear();
                stringstream ss(argv[++i]);
                string tok;
                while (getline(ss, tok, ',')) instances.push_back(tok);
            } else if (a == "--evals" && i + 1 < argc) {
                maxEvaluations = stoi(argv[++i]);
            } else if (a == "--topK" && i + 1 < argc) {
                topK = stoi(argv[++i]);
            }
        }

        const vector<RCPSP_Cond> &conditions = RCPSP_STANDARD_CONDITIONS();

        fs::create_directories("analysis/gap48");
        ofstream csv("analysis/gap48/a2_raw_runs.csv");
        csv << "instance,condition,run_id,best_ms,best_cost,evals_used\n";

        cout << "============================================================\n";
        cout << " A2 Single-Objective Makespan GA (diagnostic harness)\n";
        cout << " instances=" << instances.size() << "  conditions=" << conditions.size()
             << "  R=" << R << "  popSize=" << popSize
             << "  maxEvaluations=" << maxEvaluations << "\n";
        cout << "============================================================\n";

        auto tAllStart = chrono::steady_clock::now();

        for (const auto &prefix : instances) {
            const string instFile = "j30.sm/" + prefix + ".sm";
            for (const auto &c : conditions) {
                const string ctag = toCondTag(c.rr, c.rv);
                auto t0 = chrono::steady_clock::now();

                // R回を並列実行
                vector<future<RunResult>> futures;
                futures.reserve(R);
                for (int r = 0; r < R; ++r) {
                    unsigned seed = std::hash<string>{}(prefix + ctag) + (unsigned)r * 7919u + 12345u;
                    int perRunK = topK > 0 ? perRunTopM : 0;
                    futures.push_back(async(launch::async, [=]() {
                        return runSingleObjMakespanGA(instFile, c.rr, c.rv,
                                                       popSize, maxEvaluations, seed, perRunK);
                    }));
                }

                vector<RunResult> results;
                results.reserve(R);
                for (auto &f : futures) results.push_back(f.get());

                double best = 1e18, worst = -1e18, sum = 0;
                int bestIdx = 0;
                for (int r = 0; r < R; ++r) {
                    double ms = results[r].bestMs;
                    if (ms < best) { best = ms; bestIdx = r; }
                    if (ms > worst) worst = ms;
                    sum += ms;
                    csv << prefix << "," << ctag << "," << r << ","
                        << results[r].bestMs << "," << results[r].bestCost << ","
                        << results[r].evalsUsed << "\n";
                }
                double mean = sum / R;

                RCPSP_Problem_MaxShift metaProb(instFile, 1, c.rr, c.rv);
                writeSchedA2(prefix, ctag, metaProb,
                             results[bestIdx].bestMs, results[bestIdx].bestCost,
                             results[bestIdx].startTimes);

                if (topK > 0) {
                    // 全R runの上位個体をプールし、重複順列を除いてms昇順topKを保存
                    vector<std::tuple<double,double,vector<int>>> pool;
                    for (auto &res : results)
                        for (auto &t : res.topPerms) pool.push_back(t);
                    sort(pool.begin(), pool.end(), [](auto &a, auto &b) {
                        return std::get<0>(a) < std::get<0>(b);
                    });
                    vector<std::tuple<double,double,vector<int>>> uniquePool;
                    for (auto &t : pool) {
                        if ((int)uniquePool.size() >= topK) break;
                        bool dup = false;
                        for (auto &u : uniquePool)
                            if (std::get<2>(u) == std::get<2>(t)) { dup = true; break; }
                        if (!dup) uniquePool.push_back(t);
                    }
                    writePermA2(prefix, ctag, uniquePool);
                    syncPrint("    -> PERM_A2: " + to_string(uniquePool.size())
                              + " unique perms saved\n");
                }

                double elapsed = chrono::duration<double>(
                        chrono::steady_clock::now() - t0).count();
                syncPrint("  [" + prefix + " " + ctag + "] best=" + to_string((int)best)
                          + "  mean=" + to_string(mean)
                          + "  worst=" + to_string((int)worst)
                          + "  (" + to_string(elapsed) + "s)\n");
            }
        }

        double totalElapsed = chrono::duration<double>(
                chrono::steady_clock::now() - tAllStart).count();
        cout << "\n[DONE] total=" << totalElapsed << "s ("
             << totalElapsed / 60.0 << " min)\n";

        return 0;
    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
