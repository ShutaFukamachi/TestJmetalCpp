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
#include <queue>

static std::mt19937 rng(12345);

// ============================================================
// [追加] capacityAt() ヘルパー
//   capacity_t[k][t] が設定済みならその値を返す
//   未設定（空）なら従来通り capacity[k] を返す
//   → これだけで RR/RV が全体に反映される
// ============================================================
static int capacityAt(const RCPSP_Instance &inst, int k, int t) {
    if (!inst.capacity_t.empty()
        && k < (int)inst.capacity_t.size()
        && t >= 0
        && t < (int)inst.capacity_t[k].size()) {
        return inst.capacity_t[k][t];
    }
    return inst.capacity[k];
}

// ============================================================
// repairToTopological: 先行制約を破った活動リストを修復する
//   入力 seq の位置を優先度として Kahn のアルゴリズムを実行し、
//   先行制約を満たしつつ元の順序をできるだけ保った ALR を返す
// ============================================================
static std::vector<int> repairToTopological(
    const std::vector<int>& seq,
    const std::vector<std::vector<int>>& successors,
    int nJobs)
{
    std::vector<int> indeg(nJobs, 0);
    for (int j = 0; j < nJobs; ++j)
        for (int s : successors[j])
            if (s >= 0 && s < nJobs) ++indeg[s];

    // seq の位置を優先度にする（小さい = 優先）
    std::vector<int> priority(nJobs, nJobs);
    for (int i = 0; i < nJobs; ++i)
        if (seq[i] >= 0 && seq[i] < nJobs)
            priority[seq[i]] = i;

    auto cmp = [&](int a, int b) { return priority[a] > priority[b]; };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

    for (int j = 0; j < nJobs; ++j)
        if (indeg[j] == 0) pq.push(j);

    std::vector<int> result;
    result.reserve(nJobs);
    while (!pq.empty()) {
        int j = pq.top(); pq.pop();
        result.push_back(j);
        for (int s : successors[j])
            if (s >= 0 && s < nJobs && --indeg[s] == 0) pq.push(s);
    }
    // サイクルがある場合（通常は起きない）は元の seq をそのまま返す
    if ((int)result.size() != nJobs) return seq;
    return result;
}

// ============================================================
// schedule_feasible: capacity[k] → capacityAt() に変更（1行だけ）
// ============================================================
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
                // [変更] instance.capacity[k] → capacityAt(instance, k, t)
                if (timeResourceUsage[t][k] > capacityAt(instance, k, t)) {
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
    if (!fin) return false;

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

static bool writeCostTableToCSV(const std::string &filename) {
    if (!COST_INITIALIZED || COST_R <= 0 || COST_T <= 0 || (int)COST_TABLE.size() != COST_R) {
        std::cerr << "[RCPSP_Problem] Cannot write cost table: not initialized.\n";
        return false;
    }
    std::ofstream fout(filename);
    if (!fout) {
        std::cerr << "[RCPSP_Problem] Failed to open " << filename << " for writing.\n";
        return false;
    }
    fout << COST_R << " " << COST_T << "\n";
    for (int k = 0; k < COST_R; ++k) {
        for (int t = 0; t < COST_T; ++t) {
            fout << COST_TABLE[k][t];
            if (t + 1 < COST_T) fout << " ";
        }
        fout << "\n";
    }
    std::cout << "[RCPSP_Problem] Wrote cost table to " << filename
              << " (R=" << COST_R << ", T=" << COST_T << ")\n";
    return true;
}

static void resetCostSeriesInternal() {
    COST_INITIALIZED = false;
    COST_T = -1;
    COST_R = 4;
    COST_TABLE.clear();
    std::cout << "[RCPSP_Problem] Reset global cost series.\n";
}

static void generateCostSeries(int R, int T) {
    if (loadCostTableFromCSV("costs.csv", R)) return;

    CostRNG crng;
    if (T <= 0) T = 1;

    COST_TABLE.assign(R, std::vector<double>(T, 0.0));

    for (int k = 0; k < R; ++k) {
        int pattern = (k % 4) + 1;
        double alpha = crng.uniform(100.0, 200.0);
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
        std::vector<double> gamma_seq;
        if (pattern == 3 || pattern == 4) {
            double Gamma = crng.uniform(20.0, 30.0);
            gamma_seq = seasonal_sequence(Gamma);
        } else {
            gamma_seq = std::vector<double>(12, 0.0);
        }
        for (int t = 0; t < T; ++t) {
            double gamma_t = gamma_seq[t % gamma_seq.size()];
            double omega_t = crng.normal(0.0, 5.0);
            double c = alpha + beta * t + gamma_t + omega_t;
            if (c < 0.0) c = 0.0;
            COST_TABLE[k][t] = c;
        }
    }

    COST_R = R; COST_T = T; COST_INITIALIZED = true;
    writeCostTableToCSV("costs.csv");
    std::cout << "[RCPSP_Problem] Generated random cost series (R=" << R << ", T=" << T << ")\n";
}

static double resourceCost(int k, int t, int horizon) {
    if (!COST_INITIALIZED) {
        if (COST_R <= 0) COST_R = 4;
        generateCostSeries(COST_R, horizon);
    }
    if (COST_T <= 0) return 1.0;
    if (k < 0) k = 0;
    if (k >= COST_R) k = COST_R - 1;
    if (t < 0) t = 0;
    if (t >= COST_T) t = COST_T - 1;
    return COST_TABLE[k][t];
}

// ============================================================
// BnB 用公開アクセサ: job j を時刻 t に配置したときのコスト
// ============================================================
double RCPSP_Problem::computeJobCostAt(int j, int t, int horizon) const {
    int d = instance.duration[j];
    if (d <= 0) return 0.0;
    double c = 0.0;
    for (int tau = t; tau < t + d; ++tau) {
        for (int k = 0; k < instance.nRes; ++k) {
            c += resourceCost(k, tau, horizon) * (double)instance.demand[j][k];
        }
    }
    return c;
}

// ==== maxShift =====
static void buildMaxShiftVector(
        int strategy, int T, int nJobs,
        int evalCounter, int maxEvaluations,
        std::vector<int> &maxShift) {
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
        for (int j = 0; j < nJobs; ++j) maxShift[j] = randIn(1, halfT);
    } else {
        double ratio = 0.0;
        if (maxEvaluations > 0) ratio = static_cast<double>(evalCounter) / maxEvaluations;
        int A = 1, B = halfT;
        if      (ratio < 0.10) { A = 1;                B = std::max(1, T/8); }
        else if (ratio < 0.30) { A = std::max(1,T/8+1);  B = std::max(A, T/4); }
        else if (ratio < 0.60) { A = std::max(1,T/4+1);  B = std::max(A, 3*T/8); }
        else                   { A = std::max(1,3*T/8+1); B = std::max(A, halfT); }
        if (strategy == 3) {
            int m = randIn(A, B);
            std::fill(maxShift.begin(), maxShift.end(), m);
        } else {
            for (int j = 0; j < nJobs; ++j) maxShift[j] = randIn(A, B);
        }
    }
    if (nJobs > 0) maxShift[0]       = 0;
    if (nJobs > 1) maxShift[nJobs-1] = 0;
}

// ============================================================
// [変更] コンストラクタ: 引数に rr, rv を追加
//   デフォルト値 rr=0.0, rv=false のため
//   既存の呼び出し new RCPSP_Problem(file) はそのまま動く
// ============================================================
RCPSP_Problem::RCPSP_Problem(const std::string &filename, int strategy,
                              double rr, bool rv)
        : Problem(), strategy_(strategy), instance(readPSPLIB_SM(filename)) {

    std::cout << "[RCPSP_Problem] Loading instance from " << filename
              << "  RR=" << rr << "  RV=" << (rv ? 1 : 0) << std::endl;

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

    COST_R           = instance.nRes;
    COST_INITIALIZED = false;
    COST_T           = -1;

    // ============================================================
    // [追加] RR>0 または RV=true のとき時間依存容量テーブルを生成
    //   rr=0.0 かつ rv=false → capacity_t は空 → 既存通り capacity[k] 使用
    // ============================================================
    if (rr > 0.0 || rv) {
        buildTimeVaryingCapacity(rr, rv);
    }

    std::cout << "[RCPSP_Problem] nJobs=" << instance.nJobs
              << " nRes=" << instance.nRes << std::endl;
}

// ============================================================
// [新規追加] buildTimeVaryingCapacity
//
//   論文 Appendix A Step5-7 の忠実な実装
//
//   Step5: U_kt ~ Uniform(U0_k*(1-rr),  U0_k*(1+rr))
//     rr = 0.0  → 全時刻で U0_k（定数、RR=0 に相当）
//     rr = 0.25 → 論文の RR=0.25
//     rr = 0.5  → 論文の RR=0.5
//     rr = 0.75 → 論文の RR=0.75
//
//   Step7: rv=true のとき MOD(t,14)==vacDay を容量0にする
//     vacDay は rn<0.5 なら 0、そうでなければ 7（論文 Step7 通り）
// ============================================================
void RCPSP_Problem::buildTimeVaryingCapacity(double rr, bool rv) {
    int nRes = instance.nRes;

    // ホライゾン T = 全 duration の合計 × 余裕係数
    // RV あり の場合は休暇日の分だけ余裕を大きくする
    int T = 0;
    for (int d : instance.duration) T += d;
    // RV=1（休暇あり）は14日に1日休暇 ≈ 7%の追加余裕が必要。
    // RR=0.75 では容量が最大25%になるため更に多くのタイムスロットが必要。
    double factor = rv ? 3.5 : (rr >= 0.75 ? 2.5 : 1.5);
    T = static_cast<int>(T * factor) + 20;
    if (T <= 0) T = 100;

    // 毎回異なる乱数列を使う
    static thread_local std::mt19937 capRng(std::random_device{}());

    instance.capacity_t.assign(nRes, std::vector<int>(T, 0));

    for (int k = 0; k < nRes; ++k) {
        double U0 = static_cast<double>(instance.capacity[k]);

        // ---- Step 5: Resource Range ----
        for (int t = 0; t < T; ++t) {
            int cap;
            if (rr == 0.0) {
                // RR=0: 定数（時間依存なし）
                cap = instance.capacity[k];
            } else {
                double lo = U0 * (1.0 - rr);
                double hi = U0 * (1.0 + rr);
                if (lo < 0.0) lo = 0.0;  // 負にならないようガード
                std::uniform_real_distribution<> dist(lo, hi);
                cap = static_cast<int>(std::round(dist(capRng)));
                if (cap < 1) cap = 1;  // 休暇日以外は最低1を保証
            }
            instance.capacity_t[k][t] = cap;
        }

        // ---- Step 7: Resource Vacation ----
        if (rv) {
            // rn を生成して vacDay を決定（論文 Step7 通り）
            std::uniform_real_distribution<> u01(0.0, 1.0);
            int vacDay = (u01(capRng) < 0.5) ? 0 : 7;
            for (int t = 0; t < T; ++t) {
                if (t % 14 == vacDay) {
                    instance.capacity_t[k][t] = 0;  // 資源休暇
                }
            }
        }
    }

    std::cout << "[RCPSP_Problem] Built time-varying capacity"
              << " (nRes=" << nRes << ", T=" << T
              << ", RR=" << rr << ", RV=" << (rv ? 1 : 0) << ")\n";
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

    for (int j = 0; j < numberOfJobs_; ++j) {
        for (int succ : instance.successors[j]) {
            if (pos[succ] <= pos[j]) return false;
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

    std::vector<int> seq(n);
    for (int i = 0; i < n; ++i) {
        seq[i] = (int) vars[i]->getValue();
    }

    if (!checkTopological(seq)) {
        // ペナルティではなく先行制約を保った順序に修復する
        seq = repairToTopological(seq, instance.successors, n);
        // 修復済みシーケンスを変数に書き戻す
        for (int i = 0; i < n; ++i) vars[i]->setValue((double)seq[i]);
    }

    // ---- ホライゾン T の決定 ----
    int T = 0;
    for (int j = 0; j < n; ++j) T += instance.duration[j];
    if (!instance.capacity_t.empty() && !instance.capacity_t[0].empty()) {
        int tvT = static_cast<int>(instance.capacity_t[0].size());
        if (tvT > T) T = tvT;
    }
    if (T <= 0) T = 1;

    // ---- schedObj ベクタ取得 ----
    // schedObj[j] == 0: makespan 優先（ESS）
    // schedObj[j] == 1: コスト優先（[t_mak, t_mak+maxShift] 内の最安時刻）
    std::vector<int> schedObj(n, 0);
    if (numberOfVariables_ >= 2 * n) {
        for (int j = 0; j < n; ++j) {
            int v = (int)vars[n + j]->getValue();
            schedObj[j] = (v != 0) ? 1 : 0;
        }
    }
    // ダミー端点は常に makespan 優先
    if (n > 0) schedObj[0]     = 0;
    if (n > 1) schedObj[n - 1] = 0;

    // ---- maxShift ベクタ構築 ----
    std::vector<int> maxShift(n, 0);
    if (outputMaxShift_ >= 0) {
        std::fill(maxShift.begin(), maxShift.end(), outputMaxShift_);
    } else {
        maxShift = buildMaxShiftForEval(T);
    }
    if (n > 0) maxShift[0]     = 0;
    if (n > 1) maxShift[n - 1] = 0;

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

    // ============================================================
    // [変更] canPlace: capacity[k] → capacityAt() に変更（1行だけ）
    //   これだけで RR/RV が評価全体に反映される
    // ============================================================
    auto canPlace = [&](int j, int t) {
        int d = instance.duration[j];
        if (d <= 0) return true;
        if (t < 0) return false;
        if (t + d > T) return false;

        for (int tau = t; tau < t + d; ++tau) {
            for (int k = 0; k < nRes; ++k) {
                int used = usage[k][tau] + instance.demand[j][k];
                // [変更] instance.capacity[k] → capacityAt(instance, k, tau)
                if (used > capacityAt(instance, k, tau)) return false;
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
                solution->setObjective(0, 1e9);
                solution->setObjective(1, 1e9);
                return;
            }
        }

        // P1: 常に最早実行可能時刻に配置（コスト最適化なし）
        place(j, t_mak);
        totalCost += jobCostAt(j, t_mak);
    }

    int makespan = 0;
    for (int j = 0; j < n; ++j) {
        if (finish[j] > makespan) makespan = finish[j];
    }

    if (!schedule_feasible(instance, start)) {
        std::cerr << "[ERROR] Infeasible schedule generated!" << std::endl;
        std::exit(1);
    }

    // コストシフト後の実際の開始時刻をキャッシュしておく
    // → computeStartTimes() がガントチャート用に正確な開始時刻を返せるようにする
    startTimesCache_[solution] = start;
    // Solution オブジェクト自身にも保存（コピー時も引き継がれる）
    solution->startTimes_ = start;

    // P1: 連続実行なので execSlots_[j] = [start_j, start_j+1, ..., start_j+d_j-1]
    solution->execSlots_.resize(n);
    for (int j = 0; j < n; ++j) {
        int dj = instance.duration[j];
        solution->execSlots_[j].resize(dj);
        for (int i = 0; i < dj; ++i) {
            solution->execSlots_[j][i] = start[j] + i;
        }
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

        if (va > vb) return false;
        if (va < vb) betterInAtLeastOne = true;
    }
    return betterInAtLeastOne;
}


//  局所探索本体：スケジューリング目的をランダムに反転

void RCPSP_Problem::localSearchOnSchedObj(Solution *solution, int maxLSMoves) {
    int nJobs = numberOfJobs_;
    int nVars = solution->getNumberOfVariables();

    if (nVars < 2 * nJobs) return;
    if (nJobs <= 2) return;

    std::vector<int> jobs;
    for (int j = 1; j <= nJobs - 2; ++j) {
        jobs.push_back(j);
    }
    if (jobs.empty()) return;

    int acceptedMoves = 0;

    while (true) {
        bool improvedInThisSweep = false;

        std::shuffle(jobs.begin(), jobs.end(), rng);

        for (int idx = 0; idx < (int)jobs.size(); ++idx) {
            int j = jobs[idx];

            Solution *neighbor = new Solution(solution);

            Variable **varsN = neighbor->getDecisionVariables();
            int schedIndex = nJobs + j;

            int old = (int)varsN[schedIndex]->getValue();
            varsN[schedIndex]->setValue(1 - old);

            this->evaluate(neighbor);

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

        if (!improvedInThisSweep) break;
        if (maxLSMoves > 0 && acceptedMoves >= maxLSMoves) break;
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
    if (!checkTopological(seq)) return;

    std::vector<std::vector<int>> preds(nJobs);
    for (int j = 0; j < nJobs; ++j) {
        for (int succ : instance.successors[j]) {
            if (succ >= 0 && succ < nJobs) {
                preds[succ].push_back(j);
            }
        }
    }

    int acceptedMoves = 0;

    while (true) {
        bool improvedInThisSweep = false;

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
        for (int p = 0; p < nJobs; ++p) pos[seq[p]] = p;

        for (auto &pr : candPairs) {
            int i = pr.first;
            int j = pr.second;
            if (i >= j) continue;

            int a = seq[i];
            int b = seq[j];

            bool bad = false;
            for (int s : instance.successors[a]) if (s == b) { bad = true; break; }
            if (!bad) for (int s : instance.successors[b]) if (s == a) { bad = true; break; }
            if (bad) continue;

            for (int s : instance.successors[a]) {
                int ps = pos[s];
                if (i < ps && ps < j) { bad = true; break; }
            }
            if (bad) continue;

            for (int pPred : preds[b]) {
                int pp = pos[pPred];
                if (i < pp && pp < j) { bad = true; break; }
            }
            if (bad) continue;

            Solution *neighbor = new Solution(solution);
            Variable **varsN = neighbor->getDecisionVariables();

            double vi = varsN[i]->getValue();
            double vj = varsN[j]->getValue();
            varsN[i]->setValue(vj);
            varsN[j]->setValue(vi);

            this->evaluate(neighbor);

            if (dominatesSolution(neighbor, solution)) {
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

        if (!improvedInThisSweep) break;
        if (maxLSMoves > 0 && acceptedMoves >= maxLSMoves) break;
    }
}

Solution* RCPSP_Problem::createRandomTopoSolution() {
    int nJobs = numberOfJobs_;
    int nVars = numberOfVariables_;

    Solution* sol = new Solution(this);
    Variable** vars = sol->getDecisionVariables();

    std::vector<int> indeg(nJobs, 0);
    for (int j = 0; j < nJobs; ++j) {
        for (int succ : instance.successors[j]) {
            if (succ >= 0 && succ < nJobs) ++indeg[succ];
        }
    }

    std::vector<int> avail;
    avail.reserve(nJobs);
    for (int j = 0; j < nJobs; ++j) {
        if (indeg[j] == 0) avail.push_back(j);
    }

    std::vector<int> perm;
    perm.reserve(nJobs);

    while (!avail.empty()) {
        std::uniform_int_distribution<int> dist(0, (int)avail.size() - 1);
        int idx = dist(rng);
        int j = avail[idx];
        avail[idx] = avail.back();
        avail.pop_back();
        perm.push_back(j);
        for (int succ : instance.successors[j]) {
            if (succ >= 0 && succ < nJobs) {
                if (--indeg[succ] == 0) avail.push_back(succ);
            }
        }
    }

    if ((int)perm.size() != nJobs) {
        perm.resize(nJobs);
        std::iota(perm.begin(), perm.end(), 0);
    }

    for (int i = 0; i < nJobs; ++i) vars[i]->setValue((double)perm[i]);

    // schedObj を 50% 確率でランダム割り当て（フェーズ2の仕様通り）
    std::bernoulli_distribution coin(0.5);
    for (int j = 0; j < nJobs; ++j) {
        int idx = nJobs + j;
        if (idx < nVars) vars[idx]->setValue(coin(rng) ? 1.0 : 0.0);
    }
    // ダミー端点は常に 0（makespan 優先）
    if (nJobs > 0 && nJobs < nVars)              vars[nJobs + 0]->setValue(0.0);
    if (nJobs > 1 && nJobs + nJobs - 1 < nVars) vars[nJobs + nJobs - 1]->setValue(0.0);

    return sol;
}

std::vector<int> RCPSP_Problem::computeStartTimes(Solution *solution) const {
    // evaluate() でキャッシュされた実際の開始時刻（コストシフト済み）があれば返す
    auto it = startTimesCache_.find(solution);
    if (it != startTimesCache_.end()) {
        return it->second;
    }

    // Solution オブジェクト自身に保存された開始時刻があれば返す
    // （コピーによってポインタが変わっても startTimes_ は引き継がれる）
    if (!solution->startTimes_.empty()) {
        return solution->startTimes_;
    }

    // キャッシュがない場合は従来の最早開始スケジュール（ESS）で計算
    int n    = numberOfJobs_;
    int nRes = instance.nRes;

    Variable **vars = solution->getDecisionVariables();
    std::vector<int> seq(n);
    for (int i = 0; i < n; ++i) seq[i] = (int)vars[i]->getValue();

    std::vector<std::vector<int>> preds(n);
    for (int j = 0; j < n; ++j)
        for (int succ : instance.successors[j])
            if (succ >= 0 && succ < n) preds[succ].push_back(j);

    int T = 0;
    for (int d : instance.duration) T += d;
    if (!instance.capacity_t.empty() && !instance.capacity_t[0].empty()) {
        int tvT = (int)instance.capacity_t[0].size();
        if (tvT > T) T = tvT;
    }
    if (T <= 0) T = 1;

    std::vector<std::vector<int>> usage(nRes, std::vector<int>(T, 0));
    std::vector<int> start(n, 0), finish(n, 0);

    auto canPlace = [&](int j, int t) {
        int d = instance.duration[j];
        if (d <= 0) return true;
        if (t < 0 || t + d > T) return false;
        for (int tau = t; tau < t + d; ++tau)
            for (int k = 0; k < nRes; ++k)
                if (usage[k][tau] + instance.demand[j][k] > capacityAt(instance, k, tau)) return false;
        return true;
    };

    for (int pos = 0; pos < n; ++pos) {
        int j = seq[pos];
        int d = instance.duration[j];
        if (d <= 0) { start[j] = finish[j] = 0; continue; }

        int est = 0;
        for (int p : preds[j]) est = std::max(est, finish[p]);

        int t = est;
        while (t < T && !canPlace(j, t)) ++t;
        if (t >= T) t = std::max(0, T - d);

        for (int tau = t; tau < t + d; ++tau)
            for (int k = 0; k < nRes; ++k)
                usage[k][tau] += instance.demand[j][k];
        start[j]  = t;
        finish[j] = t + d;
    }
    return start;
}

std::vector<std::vector<int>> RCPSP_Problem::get_precedence_matrix() const {
    int n = numberOfJobs_;
    // mat[i][j] = 1: ジョブiはジョブjより前に来なければならない（推移閉包）
    std::vector<std::vector<int>> mat(n, std::vector<int>(n, 0));

    // 直接の先行制約を設定
    for (int i = 0; i < n; ++i) {
        for (int succ : instance.successors[i]) {
            if (succ >= 0 && succ < n) {
                mat[i][succ] = 1;
            }
        }
    }

    // Warshallアルゴリズムで推移閉包を計算
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                if (mat[i][k] && mat[k][j]) {
                    mat[i][j] = 1;
                }
            }
        }
    }

    return mat;
}

// ===== 派生クラス向け protected ヘルパー =====

// ============================================================
// buildMaxShiftForEval
//
//  P2/P3 の evaluate() から呼び出す。
//  基底クラス P1 と同一の静的 rng を使って maxShift を生成することで、
//  P1 と P2/P3 の maxShift 統計的性質を揃える。
//
//  【修正前の問題】
//    RCPSP_Problem_Splitting.cpp 内の splitRng(54321) を使っていたため、
//    同一解を P1 と P2 で評価しても maxShift が異なり、
//    schedObj=1 ジョブのシフト量が変わってスケジュールが diverge していた。
// ============================================================
std::vector<int> RCPSP_Problem::buildMaxShiftForEval(int T) const {
    std::vector<int> maxShift;
    buildMaxShiftVector(strategy_, T, numberOfJobs_, evalCounter_, maxEvaluations_, maxShift);
    return maxShift;
}

double RCPSP_Problem::computeSlotCost(int j, int t, int horizon) const {
    double c = 0.0;
    for (int k = 0; k < instance.nRes; ++k) {
        c += resourceCost(k, t, horizon) * (double)instance.demand[j][k];
    }
    return c;
}

int RCPSP_Problem::capacityAtTime(int k, int t) const {
    return capacityAt(instance, k, t);
}

// ===== RCPSP_Problem: global cost series control =====
void RCPSP_Problem::resetGlobalCostSeries() {
    resetCostSeriesInternal();
}

bool RCPSP_Problem::writeGlobalCostSeriesCSV(const std::string &filename) {
    return writeCostTableToCSV(filename);
}
