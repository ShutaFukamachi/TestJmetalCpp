#include "RCPSP_Problem.h"
#include "RCPSP_Reader.h"
#include "Solution.h"
#include "Variable.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <limits>
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <sstream>

static std::mt19937 rng(12345);

bool schedule_feasible(const RCPSP_Instance &instance, const std::vector<int> &startTimes) {
    int nJobs = instance.nJobs;
    int nRes  = instance.nRes;

    std::vector<std::vector<int>> timeResourceUsage;

    int horizon = 0;
    for (int j = 0; j < nJobs; ++j) {
        int st = startTimes[j];
        int ft = st + instance.duration[j];
        if (ft > horizon) horizon = ft;
    }

    timeResourceUsage.assign(horizon, std::vector<int>(nRes, 0));

    for (int j = 0; j < nJobs; ++j) {
        int st = startTimes[j];
        int ft = st + instance.duration[j];
        for (int t = st; t < ft; ++t) {
            for (int k = 0; k < nRes; ++k) {
                timeResourceUsage[t][k] += instance.demand[j][k];
                if (timeResourceUsage[t][k] > instance.capacity[k]) {
                    return false;
                }
            }
        }
    }

    return true;
}

// ==== コスト系列用 RNG ====
struct CostRNG {
    std::mt19937 gen;
    CostRNG() : gen((std::random_device())()) {}

    double uniform(double a, double b) {
        std::uniform_real_distribution<> d(a, b);
        return d(gen);
    }
    double normal(double mean, double sd) {
        std::normal_distribution<> d(mean, sd);
        return d(gen);
    }
};

// 季節性 γ_t の 12 期間パターン
static std::vector<double> seasonal_sequence(double Gamma) {
    return {
        0.0,   Gamma,   2*Gamma,  3*Gamma,
        2*Gamma, Gamma, 0.0,
        -Gamma, -2*Gamma, -3*Gamma, -2*Gamma, -Gamma
    };
}

static bool COST_INITIALIZED = false;
static int  COST_T           = -1;
static int  COST_R           = 4;
static std::vector<std::vector<double>> COST_TABLE;

static bool loadCostTableFromCSV(const std::string &filename, int expectedR) {
    std::ifstream fin(filename);
    if (!fin) {
        return false;
    }

    int R = 0, T = 0;
    if (!(fin >> R >> T)) {
        std::cerr << "[RCPSP_Problem] Failed to read header of " << filename << "\n";
        return false;
    }
    if (R != expectedR) {
        std::cerr << "[RCPSP_Problem] costs.csv R mismatch. expected "
                  << expectedR << " but got " << R << "\n";
        return false;
    }

    std::vector<std::vector<double>> tmp(R, std::vector<double>(T, 0.0));
    for (int k = 0; k < R; ++k) {
        for (int t = 0; t < T; ++t) {
            if (!(fin >> tmp[k][t])) {
                std::cerr << "[RCPSP_Problem] Failed to read c[" << k << "][" << t
                          << "] from " << filename << "\n";
                return false;
            }
        }
    }

    COST_TABLE       = std::move(tmp);
    COST_R           = R;
    COST_T           = T;
    COST_INITIALIZED = true;

    std::cout << "[RCPSP_Problem] Loaded cost table from " << filename
              << " (R=" << R << ", T=" << T << ")\n";
    return true;
}

// horizon T に対して全資源のコスト系列を生成
//  1) costs.csv があればそれを使う
//  2) なければ論文パターンに従ってランダム生成
static void generateCostSeries(int R, int T) {
    // まず costs.csv を試す
    if (loadCostTableFromCSV("costs.csv", R)) {
        return;
    }

    // CSV が無ければ、論文通りのパターンでランダム生成
    CostRNG crng;
    if (T <= 0) T = 1;

    COST_TABLE.assign(R, std::vector<double>(T, 0.0));

    for (int k = 0; k < R; ++k) {
        int pattern = (k % 4) + 1;

        // α ~ U[100,200]
        double alpha = crng.uniform(100.0, 200.0);

        // β の生成
        double beta = 0.0;
        double threshold = (2.0 * T > 0.0) ? (alpha / (2.0 * T)) : 0.0;

        bool positiveTrend = (pattern == 1 || pattern == 3);

        if (positiveTrend) {
            if (threshold > 0.1) beta = crng.uniform(0.1, threshold);
            else                 beta = 0.1;
        } else {
            if (threshold > 0.1) beta = crng.uniform(-threshold, -0.1);
            else                 beta = -0.1;
        }

        // 季節性 γ_t
        std::vector<double> gamma_seq;
        if (pattern == 3 || pattern == 4) {
            double Gamma = crng.uniform(20.0, 30.0);
            gamma_seq = seasonal_sequence(Gamma);
        } else {
            gamma_seq = std::vector<double>(12, 0.0);
        }

        // 各時刻 t のコスト c_{k,t}
        for (int t = 0; t < T; ++t) {
            double gamma_t = gamma_seq[t % gamma_seq.size()];
            double omega_t = crng.normal(0.0, 5.0); // ω_t ~ N(0,5)

            double c = alpha + beta * t + gamma_t + omega_t;
            if (c < 0.0) c = 0.0;

            COST_TABLE[k][t] = c;
        }
    }

    COST_R           = R;
    COST_T           = T;
    COST_INITIALIZED = true;

    std::cout << "[RCPSP_Problem] Generated random cost series (R="
              << R << ", T=" << T << ")\n";
}

// c_{k,t} を返す（必要ならシリーズを初期化）
static double resourceCost(int k, int t, int horizon) {
    if (!COST_INITIALIZED) {
        // 初回呼び出し時のみシリーズを用意
        if (COST_R <= 0) COST_R = 4; // デフォルト。コンストラクタで上書きされる
        generateCostSeries(COST_R, horizon);
    }

    if (COST_T <= 0) return 1.0;

    if (k < 0) k = 0;
    if (k >= COST_R) k = COST_R - 1;

    if (t < 0) t = 0;
    if (t >= COST_T) t = COST_T - 1;

    return COST_TABLE[k][t];
}

// ==== maxShift =====
static void buildMaxShiftVector(
        int strategy,
        int T,
        int nJobs,
        int evalCounter,
        int maxEvaluations,
        std::vector<int> &maxShift
) {
    maxShift.assign(nJobs, 0);
    if (T <= 0) T = 1;
    int halfT = std::max(1, T / 2);

    auto randIn = [&](int A, int B) {
        if (B < A) B = A;
        std::uniform_int_distribution<int> dist(A, B);
        return dist(rng);
    };

    if (strategy == 1) {
        int m = randIn(1, halfT);
        std::fill(maxShift.begin(), maxShift.end(), m);
    } else if (strategy == 2) {
        for (int j = 0; j < nJobs; ++j) {
            maxShift[j] = randIn(1, halfT);
        }
    } else {
        double ratio = 0.0;
        if (maxEvaluations > 0) {
            ratio = static_cast<double>(evalCounter) / maxEvaluations;
        }

        int A = 1, B = halfT;
        if (ratio < 0.10) {
            A = 1;
            B = std::max(1, T / 8);
        } else if (ratio < 0.30) {
            A = std::max(1, T / 8 + 1);
            B = std::max(A, T / 4);
        } else if (ratio < 0.60) {
            A = std::max(1, T / 4 + 1);
            B = std::max(A, 3 * T / 8);
        } else {
            A = std::max(1, 3 * T / 8 + 1);
            B = std::max(A, halfT);
        }

        if (strategy == 3) {
            int m = randIn(A, B);
            std::fill(maxShift.begin(), maxShift.end(), m);
        } else {
            for (int j = 0; j < nJobs; ++j) {
                maxShift[j] = randIn(A, B);
            }
        }
    }

    if (nJobs > 0) maxShift[0]       = 0;
    if (nJobs > 1) maxShift[nJobs-1] = 0;
}

// ==== コンストラクタ ====
RCPSP_Problem::RCPSP_Problem(const std::string &filename, int strategy)
        : Problem(), strategy_(strategy),instance(readPSPLIB_SM(filename)){

    std::cout << "[RCPSP_Problem] Loading instance from " << filename << std::endl;



    numberOfJobs_ = instance.nJobs;

    numberOfVariables_ = numberOfJobs_ * 2;
    numberOfObjectives_ = 2;

    lowerLimit_ = new double[numberOfVariables_];
    upperLimit_ = new double[numberOfVariables_];

    for (int i = 0; i < numberOfJobs_; ++i) {
        lowerLimit_[i] = 0;
        upperLimit_[i] = numberOfJobs_ - 1;
    }

    for (int i = numberOfJobs_; i < numberOfVariables_; ++i) {
        lowerLimit_[i] = 0;
        upperLimit_[i] = 1;
    }

    solutionType_ = new IntSolutionType(this);

    // コスト表用の資源数をセットし、毎インスタンスごとにリセット
    COST_R           = instance.nRes;
    COST_INITIALIZED = false;
    COST_T           = -1;

    std::cout << "[RCPSP_Problem] nJobs=" << instance.nJobs
              << " nRes=" << instance.nRes << std::endl;
}

void RCPSP_Problem::printInfo() const {
    std::cout << "[RCPSP_Problem] nJobs=" << instance.nJobs
              << " nRes=" << instance.nRes << std::endl;
    std::cout << "  capacity: ";
    for (int k = 0; k < instance.nRes; ++k) {
        std::cout << instance.capacity[k] << " ";
    }
    std::cout << std::endl;
}

bool RCPSP_Problem::checkTopological(const std::vector<int> &seq) const {
    if ((int) seq.size() != numberOfJobs_) return false;

    std::vector<int> pos(numberOfJobs_, -1);
    for (int i = 0; i < numberOfJobs_; ++i) {
        int j = seq[i];
        if (j < 0 || j >= numberOfJobs_) return false;
        if (pos[j] != -1) return false;
        pos[j] = i;
    }

    // 先行関係をすべて守っているか
    for (int j = 0; j < numberOfJobs_; ++j) {
        for (int succ : instance.successors[j]) {
            if (pos[succ] <= pos[j]) {
                return false;
            }
        }
    }
    return true;
}

bool RCPSP_Problem::checkTopological(Solution *solution) const {
    std::vector<int> seq(numberOfJobs_);
    Variable **vars = solution->getDecisionVariables();
    for (int i = 0; i < numberOfJobs_; ++i) {
        seq[i] = (int) vars[i]->getValue();
    }
    return checkTopological(seq);
}

void RCPSP_Problem::evaluate(Solution *solution) {
    ++evalCounter_;

    int n    = numberOfJobs_;
    int nRes = instance.nRes;

    Variable **vars = solution->getDecisionVariables();
    int nVars       = solution->getNumberOfVariables();

    std::vector<int> seq(n);
    for (int i = 0; i < n; ++i) {
        seq[i] = (int) vars[i]->getValue();
    }

    if (!checkTopological(seq)) {
        std::cerr << "[RCPSP_Problem::evaluate] Infeasible topological order detected!" << std::endl;
        solution->setObjective(0, 1e9);
        solution->setObjective(1, 1e9);
        return;
    }

    std::vector<int> schedObj(n, 0);
    if (nVars >= 2 * n) {
        for (int j = 0; j < n; ++j) {
            int v = (int) vars[n + j]->getValue();
            schedObj[j] = (v != 0) ? 1 : 0;
        }
    } else {
        std::bernoulli_distribution coin(0.5);
        for (int j = 0; j < n; ++j) {
            schedObj[j] = coin(rng) ? 1 : 0;
        }
    }
    if (n > 0) schedObj[0]     = 0;
    if (n > 1) schedObj[n - 1] = 0;

    int T = 0;
    for (int j = 0; j < n; ++j) {
        T += instance.duration[j];
    }
    if (T <= 0) T = 1;

    // maxShift の設定
    std::vector<int> maxShift;
    buildMaxShiftVector(strategy_, T, n, evalCounter_, maxEvaluations_, maxShift);

    std::vector<std::vector<int>> preds(n);
    for (int j = 0; j < n; ++j) {
        for (int succ : instance.successors[j]) {
            if (succ >= 0 && succ < n) {
                preds[succ].push_back(j);
            }
        }
    }

    // 資源使用量 usage[res][t]
    std::vector<std::vector<int>> usage(nRes, std::vector<int>(T, 0));
    std::vector<int> start(n, 0);
    std::vector<int> finish(n, 0);

    auto canPlace = [&](int j, int t) {
        int d = instance.duration[j];
        if (d <= 0) return true;
        if (t < 0) return false;
        if (t + d > T) return false;

        for (int tau = t; tau < t + d; ++tau) {
            for (int k = 0; k < nRes; ++k) {
                int used = usage[k][tau] + instance.demand[j][k];
                if (used > instance.capacity[k]) return false;
            }
        }
        return true;
    };

    auto place = [&](int j, int t) {
        int d = instance.duration[j];
        if (d <= 0) {
            start[j] = finish[j] = t;
            return;
        }
        for (int tau = t; tau < t + d; ++tau) {
            for (int k = 0; k < nRes; ++k) {
                usage[k][tau] += instance.demand[j][k];
            }
        }
        start[j]  = t;
        finish[j] = t + d;
    };

    auto jobCostAt = [&](int j, int t) {
        int d = instance.duration[j];
        if (d <= 0) return 0.0;
        double c = 0.0;
        for (int tau = t; tau < t + d; ++tau) {
            for (int k = 0; k < nRes; ++k) {
                double ck = resourceCost(k, tau, T);
                c += ck * instance.demand[j][k];
            }
        }
        return c;
    };

    double totalCost = 0.0;

    for (int pos = 0; pos < n; ++pos) {
        int j = seq[pos];
        int d = instance.duration[j];

        if (d <= 0) {
            start[j] = finish[j] = 0;
            continue;
        }

        int est = 0;
        for (int p : preds[j]) {
            est = std::max(est, finish[p]);
        }

        int t_mak = est;
        while (t_mak < T && !canPlace(j, t_mak)) {
            ++t_mak;
        }
        if (t_mak >= T) {
            t_mak = std::max(0, T - d);
            while (t_mak >= 0 && !canPlace(j, t_mak)) {
                --t_mak;
            }
            if (t_mak < 0) {
                std::cerr << "[ERROR] Cannot place job " << j << " in schedule!" << std::endl;
                solution->setObjective(0, 1e9);
                solution->setObjective(1, 1e9);
                return;
            }
        }

        int t_final = t_mak;

        if (schedObj[j] == 1 && maxShift[j] > 0) {
            int latest = std::min(T - d, t_mak + maxShift[j]);
            double bestC = std::numeric_limits<double>::infinity();
            int bestT = -1;

            for (int t = t_mak; t <= latest; ++t) {
                if (!canPlace(j, t)) continue;
                double c = jobCostAt(j, t);
                if (c < bestC) {
                    bestC = c;
                    bestT = t;
                }
            }

            if (bestT >= 0) {
                t_final = bestT;
            } else {
                t_final = t_mak;
            }
        }

        place(j, t_final);
        totalCost += jobCostAt(j, t_final);
    }

    int makespan = 0;
    for (int j = 0; j < n; ++j) {
        if (finish[j] > makespan) makespan = finish[j];
    }

    if (!schedule_feasible(instance, start)) {
        std::cerr << "[ERROR] Infeasible schedule generated!" << std::endl;
        std::exit(1);
    }

    solution->setObjective(0, (double) makespan);
    solution->setObjective(1, totalCost);
}


//  パレート優越チェック関数

static bool dominatesSolution(Solution *a, Solution *b) {
    int nObj = a->getNumberOfObjectives();
    bool betterInAtLeastOne = false;

    for (int i = 0; i < nObj; ++i) {
        double va = a->getObjective(i);
        double vb = b->getObjective(i);

        if (va > vb) return false;   // a が劣っている
        if (va < vb) betterInAtLeastOne = true;
    }
    return betterInAtLeastOne;
}


//  局所探索本体：スケジューリング目的をランダムに反転

void RCPSP_Problem::localSearchOnSchedObj(Solution *solution, int maxLSMoves) {
    int nJobs = numberOfJobs_;
    int nVars = solution->getNumberOfVariables();

    if (nVars < 2 * nJobs) return;
    if (nJobs <= 2) return; // 0 と nJobs-1 が固定

    // 候補ジョブのリスト（0 と nJobs-1 は除外）
    std::vector<int> jobs;
    for (int j = 1; j <= nJobs - 2; ++j) {
        jobs.push_back(j);
    }
    if (jobs.empty()) return;

    int acceptedMoves = 0;

    // 改善が見つからなくなるまで繰り返す
    while (true) {
        bool improvedInThisSweep = false;

        // ジョブ集合を毎回シャッフルしてから全探索
        std::shuffle(jobs.begin(), jobs.end(), rng);

        for (int idx = 0; idx < (int)jobs.size(); ++idx) {
            int j = jobs[idx];

            // 近傍解を複製
            Solution *neighbor = new Solution(solution);

            Variable **varsN = neighbor->getDecisionVariables();
            int schedIndex = nJobs + j;  // 後半が schedObj

            int old = (int)varsN[schedIndex]->getValue();
            varsN[schedIndex]->setValue(1 - old);  // 0 <-> 1 反転

            // 評価
            this->evaluate(neighbor);

            // 近傍が現在の解をパレート優越するなら、その場で受け入れ
            if (dominatesSolution(neighbor, solution)) {
                Variable **varsS = solution->getDecisionVariables();
                for (int k = 0; k < nVars; ++k) {
                    varsS[k]->setValue(varsN[k]->getValue());
                }
                for (int o = 0; o < solution->getNumberOfObjectives(); ++o) {
                    solution->setObjective(o, neighbor->getObjective(o));
                }

                improvedInThisSweep = true;
                ++acceptedMoves;
                delete neighbor;


                break;
            }

            delete neighbor;
        }

        // このスイープで一度も改善が見つからなければ終了
        if (!improvedInThisSweep) {
            break;
        }


        if (maxLSMoves > 0 && acceptedMoves >= maxLSMoves) {
            break;
        }
    }
}

void RCPSP_Problem::localSearchOnActivityOrder(Solution *solution, int maxLSMoves) {
    int nJobs = numberOfJobs_;
    int nVars = solution->getNumberOfVariables();
    if (nJobs <= 2) return;
    if (nVars < 2 * nJobs) return;

    Variable **vars = solution->getDecisionVariables();


    std::vector<int> seq(nJobs);
    for (int i = 0; i < nJobs; ++i) {
        seq[i] = (int)vars[i]->getValue();
    }
    //トポロジカルでないなら何もしない
    if (!checkTopological(seq)) {
        return;
    }


    std::vector<std::vector<int>> preds(nJobs);
    for (int j = 0; j < nJobs; ++j) {
        for (int succ : instance.successors[j]) {
            if (succ >= 0 && succ < nJobs) {
                preds[succ].push_back(j);
            }
        }
    }

    int acceptedMoves = 0;

    // 改善が出なくなるまで繰り返す
    while (true) {
        bool improvedInThisSweep = false;

        // 位置 i<j の全ペアを作る
        std::vector<std::pair<int,int>> candPairs;
        for (int i = 0; i < nJobs - 1; ++i) {
            for (int j = i + 1; j < nJobs; ++j) {

                if (seq[i] == 0 || seq[i] == nJobs-1) continue;
                if (seq[j] == 0 || seq[j] == nJobs-1) continue;
                candPairs.emplace_back(i, j);
            }
        }
        if (candPairs.empty()) break;


        std::shuffle(candPairs.begin(), candPairs.end(), rng);


        std::vector<int> pos(nJobs);
        for (int p = 0; p < nJobs; ++p) {
            pos[seq[p]] = p;
        }

        for (auto &pr : candPairs) {
            int i = pr.first;
            int j = pr.second;
            if (i >= j) continue;

            int a = seq[i];   // i にいる job
            int b = seq[j];   // j にいる job

            // precedence を壊さないかチェック


            bool bad = false;
            for (int s : instance.successors[a]) {
                if (s == b) { bad = true; break; }
            }
            if (!bad) {
                for (int s : instance.successors[b]) {
                    if (s == a) { bad = true; break; }
                }
            }
            if (bad) continue;

            // i〜j の間に「a の後続」がいないか
            for (int s : instance.successors[a]) {
                int ps = pos[s];
                if (i < ps && ps < j) {
                    bad = true;
                    break;
                }
            }
            if (bad) continue;

            // i〜j の間に「b の先行」がいないか
            for (int pPred : preds[b]) {
                int pp = pos[pPred];
                if (i < pp && pp < j) {
                    bad = true;
                    break;
                }
            }
            if (bad) continue;

            //swap した近傍解を評価
            Solution *neighbor = new Solution(solution);
            Variable **varsN = neighbor->getDecisionVariables();

            // 順序部分だけ swap
            double vi = varsN[i]->getValue();
            double vj = varsN[j]->getValue();
            varsN[i]->setValue(vj);
            varsN[j]->setValue(vi);

            // 評価
            this->evaluate(neighbor);

            if (dominatesSolution(neighbor, solution)) {
                // 改善なら採用
                Variable **varsS = solution->getDecisionVariables();
                for (int k = 0; k < nVars; ++k) {
                    varsS[k]->setValue(varsN[k]->getValue());
                }
                for (int o = 0; o < solution->getNumberOfObjectives(); ++o) {
                    solution->setObjective(o, neighbor->getObjective(o));
                }


                std::swap(seq[i], seq[j]);

                improvedInThisSweep = true;
                ++acceptedMoves;
                delete neighbor;
                break;
            }

            delete neighbor;
        }

        if (!improvedInThisSweep) {
            // 改善がなければ局所最適
            break;
        }

        if (maxLSMoves > 0 && acceptedMoves >= maxLSMoves) {
            break;
        }
    }
}


// ランダム・トポロジカル順の個体を1つ生成

Solution* RCPSP_Problem::createRandomTopoSolution() {
    int nJobs = numberOfJobs_;
    int nVars = numberOfVariables_;

    Solution* sol = new Solution(this);
    Variable** vars = sol->getDecisionVariables();



    // 入次数を数える
    std::vector<int> indeg(nJobs, 0);
    for (int j = 0; j < nJobs; ++j) {
        for (int succ : instance.successors[j]) {
            if (succ >= 0 && succ < nJobs) {
                ++indeg[succ];
            }
        }
    }

    // 入次数0のノード集合
    std::vector<int> avail;
    avail.reserve(nJobs);
    for (int j = 0; j < nJobs; ++j) {
        if (indeg[j] == 0) {
            avail.push_back(j);
        }
    }

    std::vector<int> perm;
    perm.reserve(nJobs);

    while (!avail.empty()) {

        std::uniform_int_distribution<int> dist(0, (int)avail.size() - 1);
        int idx = dist(rng);
        int j = avail[idx];


        avail[idx] = avail.back();
        avail.pop_back();

        // 出力順列に追加
        perm.push_back(j);


        for (int succ : instance.successors[j]) {
            if (succ >= 0 && succ < nJobs) {
                if (--indeg[succ] == 0) {
                    avail.push_back(succ);
                }
            }
        }
    }

    // 何らかの理由でトポロジカルソートに失敗した場合
    if ((int)perm.size() != nJobs) {
        perm.resize(nJobs);
        std::iota(perm.begin(), perm.end(), 0);
    }

    // 2. 解の前半に permutation を書き込む
    for (int i = 0; i < nJobs; ++i) {
        vars[i]->setValue((double)perm[i]);
    }

    //  後半の schedObj ビットを設定
    // とりあえず全部 0 makespan優先 にしておく
    for (int j = 0; j < nJobs; ++j) {
        int idx = nJobs + j;
        if (idx < nVars) {
            vars[idx]->setValue(0.0);
        }
    }
    // 再確認
    if (nJobs > 0 && nJobs < nVars)   vars[nJobs + 0]->setValue(0.0);
    if (nJobs > 1 && nJobs + nJobs - 1 < nVars)
        vars[nJobs + nJobs - 1]->setValue(0.0);

    return sol;
}





