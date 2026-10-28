#include "NSGAII.h"
#include "Ranking.h"
#include "CrowdingDistanceComparator.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>
#include <thread>

namespace fs = std::filesystem;


#include "problems/RCPSP_Problem.h"
#include "problems/RCPSP_Problem_MaxShift.h"

NSGAII::NSGAII(Problem *problem)
    : Algorithm(problem),
      population(nullptr),
      offspringPopulation(nullptr),
      unionPopulation(nullptr),
      evaluations(0),
      populationSize(0) {}

NSGAII::~NSGAII() {
}

SolutionSet *NSGAII::execute() {
    populationSize    = *(int *)getInputParameter("populationSize");
    maxEvaluations    = *(int *)getInputParameter("maxEvaluations");
    crossoverOperator = getOperator("crossover");
    mutationOperator  = getOperator("mutation");
    selectionOperator = getOperator("selection");

    // ---- A/B: makespan エリート保存・交叉バイアス パラメータ読み込み ----
    // eliteMakespanSlots : 毎世代必ず残す makespan 最小解の数 (デフォルト 2)
    // makespanBiasProb   : 交叉時に片親を makespan 最小解に固定する確率 (デフォルト 0.10)
    int    eliteMakespanSlots = 2;
    double makespanBiasProb   = 0.10;
    // noveltyFilter: true のとき「2個ずつ生成 → 親世代にないスケジュールのみ採用」に切り替わる
    // false（デフォルト）のときは従来の NSGA-II と完全に同一動作
    bool   noveltyFilter      = false;
    // a1PrioritySeed: A1 priority-rule シードの ON/OFF（デフォルト OFF=0）。
    // 1 を渡すと LFT/MTS/GRPW 優先規則順列を初期集団に注入する。
    // （旧 A3 ランダム選抜シードは A1 に劣位・不採用のため 2026-07-22 に削除）
    bool   a1PrioritySeed     = false;
    // diagnosticLogs: logs/{generation,manytoone,innovation}/ への世代ごとの診断ログ出力の
    // ON/OFF（デフォルト OFF=false）。OFF のときはログファイルを開かないため、
    // 下流の集計計算（many-to-one の map 構築・innovation の支配判定など）も
    // .is_open() ガードにより自動的にスキップされる（大規模一括実行での I/O・CPU 負荷対策）。
    bool   diagnosticLogs     = false;
    {
        void *p = getInputParameter("eliteMakespanSlots");
        if (p) eliteMakespanSlots = *static_cast<int *>(p);
        p = getInputParameter("makespanBiasProb");
        if (p) makespanBiasProb = *static_cast<double *>(p);
        p = getInputParameter("noveltyFilter");
        if (p) noveltyFilter = (*static_cast<int *>(p) != 0);
        p = getInputParameter("a1PrioritySeed");
        if (p) a1PrioritySeed = (*static_cast<int *>(p) != 0);
        p = getInputParameter("diagnosticLogs");
        if (p) diagnosticLogs = (*static_cast<int *>(p) != 0);
    }
    // ---- 2026-10-14 ゼミ指摘への対応（既定はすべて OFF = 従来と同一動作）----
    // prInitFrac : ランダム初期解の枠のうち、優先規則＋ランダム性で作業リストを作る割合 [0,1]
    // prAlpha    : 偏りの強さ（重み (優先度差+1)^alpha）。0 で一様ランダム
    // regenProb  : 子1体ごとに「作業リストの後半を優先規則＋ランダム性で作り直す」確率
    // traceFile  : std::string*。指定時は初期集団の全個体と世代ごとの要約を CSV に書く
    double prInitFrac = 0.0, prAlpha = 3.0, regenProb = 0.0;
    std::string traceFile;
    {
        void *p = getInputParameter("prInitFrac");
        if (p) prInitFrac = *static_cast<double *>(p);
        p = getInputParameter("prAlpha");
        if (p) prAlpha = *static_cast<double *>(p);
        p = getInputParameter("regenProb");
        if (p) regenProb = *static_cast<double *>(p);
        p = getInputParameter("traceFile");
        if (p) traceFile = *static_cast<std::string *>(p);
    }
    // msIsland     : 工期島の個体数（0 = OFF）。窓幅を0に固定した作業リストだけの小集団を、
    //                メイクスパンだけで選んで進化させ、毎世代その最良を NSGA の集団に送る。
    // msIslandKids : 工期島が1世代に作る子の数（NSGA と同じ評価回数の予算から使う）
    // msIslandPatience : 工期島の最良が この世代数だけ改善しなければ島を止め、予算を NSGA に返す（0 = 止めない）
    int msIsland = 0, msIslandKids = 20, msIslandPatience = 0;
    {
        void *p = getInputParameter("msIsland");
        if (p) msIsland = *static_cast<int *>(p);
        p = getInputParameter("msIslandKids");
        if (p) msIslandKids = *static_cast<int *>(p);
        p = getInputParameter("msIslandPatience");
        if (p) msIslandPatience = *static_cast<int *>(p);
    }
    // blockWinProb : 子1体ごとに「作業リストで連続する数作業の窓幅（遺伝子）を一緒に動かす」確率（0 = OFF）。
    //   2026-10-20: 中間部の解は1作業ずつ窓幅を変えても改善しない（mid_diagnose.py）ことへの対策の試行。
    double blockWinProb = 0.0;
    {
        void *p = getInputParameter("blockWinProb");
        if (p) blockWinProb = *static_cast<double *>(p);
    }
    // ---- 2026-10-21 ゼミ指摘: 工期島の効き目を「1つの集団」で再現できるかを調べるためのオプション ----
    // msSelProb / msSelTop   : 親を選ぶたびに確率 msSelProb で、メイクスパン上位 msSelTop（割合）の中から
    //                          メイクスパンで2者択一トーナメントして親にする（工期島の「工期だけで選ぶ」を模す）
    // costSelProb / costSelTop: 同じことをコストで行う（コスト側の端のため）
    // zeroWinProb            : 子1体ごとに確率 zeroWinProb で窓幅の遺伝子をすべて0にする（工期島の「窓幅0」を模す）
    // traceChildren          : 1 なら trace に「親と子の内訳」を書く（子ごとに窓幅0での復号を1回余分に行う。診断専用）
    double msSelProb = 0.0, msSelTop = 0.1, costSelProb = 0.0, costSelTop = 0.1, zeroWinProb = 0.0;
    int traceChildren = 0;
    // msMateProb / msMatePatience : 「1集団の中の工期島」。子の組ごとに確率 msMateProb で、両親ともメイクスパン上位
    //   msSelTop から選び、その子は窓幅0で評価する。集団の最良メイクスパンが msMatePatience 世代改善しなければ止める（0=止めない）
    double msMateProb = 0.0; int msMatePatience = 0;
    // msMateFilter : 1 なら、工期側の組から生まれた窓幅0の子は「集団の最良メイクスパンより短いときだけ」集団に入れる
    //   （工期島が最良の1体だけを移民として送るのと同じ働き。窓幅0の個体が集団にあふれるのを防ぐ）
    int msMateFilter = 0;
    // msMateStallChild : 1 なら停止の判定を「集団の最良工期」ではなく「工期側の組から生まれた窓幅0の子の最良工期」で行う
    //   （工期島が島の中の最良で止め時を決めるのと同じ）
    int msMateStallChild = 0;
    // timeBlockWinProb : 子ごとの確率で「親のスケジュールで開始時刻が近い作業のまとまり」の窓幅を一緒に動かす（0=OFF）
    //   ブロック窓変異（作業リストで隣り合う作業）の、時間で区切る版。押し出しの連鎖は時間的に近い作業の間で起きるため。
    double timeBlockWinProb = 0.0;
    // deadlineMutProb : 子ごとの確率で「目標工期の遺伝子」vars[n+0] を動かす（RCPSP_Problem_MaxShiftDur::setDeadlineGene と組で使う。0=OFF）
    //   通常の変異は端点ダミーの遺伝子を動かさないので、目標工期は交叉とこの変異でだけ変わる。
    double deadlineMutProb = 0.0;
    // lambdaMutProb : 子ごとの確率で λ 遺伝子 vars[n+n-1] を動かす（RCPSP_Problem_MaxShiftDur::setLambdaGene と組、0=OFF）
    double lambdaMutProb = 0.0;
    // costIsland : コスト島（工期島と対称）。初期集団の作業リストを窓幅最大・目標工期なし・λ=0 で評価し、コストの低い順に残す。
    //   島の中だけで子を作り、コストだけで生き残りを決め、集団の最小コスト以下で新しいスケジュールなら1体を移民として送る。
    //   （窓幅は親のまま、目標工期の制限なし・λ=0 にして評価する）
    //   costIslandPatience 世代改善しなければ止まり、予算を本体に返す（0=止めない）。
    int costIsland = 0, costIslandKids = 20, costIslandPatience = 30;
    int deadlineOnlyFlag = 0;   // 1 なら窓は目標工期だけで決まるので、「窓幅0」は目標工期を最小（遺伝子0）にして表す
    {
        void *pd = getInputParameter("deadlineMutProb"); if (pd) deadlineMutProb = *static_cast<double *>(pd);
        pd = getInputParameter("deadlineOnly"); if (pd) deadlineOnlyFlag = *static_cast<int *>(pd);
        pd = getInputParameter("lambdaMutProb"); if (pd) lambdaMutProb = *static_cast<double *>(pd);
        pd = getInputParameter("costIsland"); if (pd) costIsland = *static_cast<int *>(pd);
        pd = getInputParameter("costIslandKids"); if (pd) costIslandKids = *static_cast<int *>(pd);
        pd = getInputParameter("costIslandPatience"); if (pd) costIslandPatience = *static_cast<int *>(pd);
    }
    {
        void *p = getInputParameter("msSelProb");    if (p) msSelProb = *static_cast<double *>(p);
        p = getInputParameter("msSelTop");           if (p) msSelTop = *static_cast<double *>(p);
        p = getInputParameter("costSelProb");        if (p) costSelProb = *static_cast<double *>(p);
        p = getInputParameter("costSelTop");         if (p) costSelTop = *static_cast<double *>(p);
        p = getInputParameter("zeroWinProb");        if (p) zeroWinProb = *static_cast<double *>(p);
        p = getInputParameter("traceChildren");      if (p) traceChildren = *static_cast<int *>(p);
        p = getInputParameter("msMateProb");         if (p) msMateProb = *static_cast<double *>(p);
        p = getInputParameter("msMatePatience");     if (p) msMatePatience = *static_cast<int *>(p);
        p = getInputParameter("msMateFilter");       if (p) msMateFilter = *static_cast<int *>(p);
        p = getInputParameter("msMateStallChild");   if (p) msMateStallChild = *static_cast<int *>(p);
        p = getInputParameter("timeBlockWinProb");   if (p) timeBlockWinProb = *static_cast<double *>(p);
    }
    static thread_local std::mt19937 rng_ms{std::random_device{}()};
    std::uniform_real_distribution<> d01_ms(0.0, 1.0);

    // 追加: 初期個体のポインタを取得（渡されていなければ nullptr）
    SolutionSet *seedPopulation = nullptr;
    {
        void *ptr = getInputParameter("initialPopulation");
        if (ptr != nullptr) {
            seedPopulation = static_cast<SolutionSet *>(ptr);
        }
    }


    // 初期個体生成

    population = new SolutionSet(populationSize);
    int filled = 0;
    // traceFile 用: 初期個体の作り方（seed / ext_ms / ext_cost / a1 / pr / random）
    std::vector<std::string> initOrigin;

    // Optional: toggle local search by input parameter "useLocalSearch" (int: 0/1).
    bool useLocalSearch = true;
    {
        void *pLS = getInputParameter("useLocalSearch");
        if (pLS != nullptr) {
            int v = *static_cast<int *>(pLS);
            useLocalSearch = (v != 0);
        }
    }


    if (seedPopulation != nullptr) {
        int seedSize = seedPopulation->size();
        for (int i = 0; i < seedSize && filled < populationSize; ++i) {
            Solution *orig = seedPopulation->get(i);
            Solution *copy = new Solution(orig);
            problem_->evaluate(copy);
            population->add(copy);
            initOrigin.push_back("seed");
            ++filled;
        }
        std::cout << "[NSGAII] Seeded " << filled << " solutions from initialPopulation\n";
    }

    // MaxShift エンコーディング用: 両端の極端解をシード挿入
    // メイクスパン極端解 × 3 + コスト極端解 × 3
    if (auto msProb = dynamic_cast<RCPSP_Problem_MaxShift*>(problem_)) {
        const int nExtremes = 3;
        for (int i = 0; i < nExtremes && filled < populationSize; ++i, ++filled) {
            Solution *sol = msProb->createMakespanExtremeSolution();
            problem_->evaluate(sol);
            population->add(sol);
            initOrigin.push_back("ext_ms");
        }
        for (int i = 0; i < nExtremes && filled < populationSize; ++i, ++filled) {
            Solution *sol = msProb->createCostExtremeSolution();
            problem_->evaluate(sol);
            population->add(sol);
            initOrigin.push_back("ext_cost");
        }
        std::cout << "[NSGAII] Injected " << (nExtremes * 2) << " extreme seed solutions\n";
    }

    // ---- A1: priority-rule シード ----
    // LFT/MTS/GRPW の優先規則順列を初期集団に注入する。
    // 各規則につき複数個体を生成（タイブレーク乱数で多様化）。
    RCPSP_Problem_MaxShift* a1Prob =
        a1PrioritySeed ? dynamic_cast<RCPSP_Problem_MaxShift*>(problem_) : nullptr;
    if (auto msProb = a1Prob) {
        const int perRule   = 4;   // 各規則あたりの生成数
        const int halfT     = msProb->getEffectiveHalfT();
        const int nJobs     = msProb->getNumJobs();
        double bestA1ms = std::numeric_limits<double>::max();
        int    injected = 0;
        for (int rule = 0; rule < 3 && filled < populationSize; ++rule) {
            for (int k = 0; k < perRule && filled < populationSize; ++k) {
                Solution *sol = msProb->createPriorityRuleSolution(rule);
                // 半分は max_shift=0、半分は Uniform[0, halfT] を付与（コスト多様性）
                if (k % 2 == 1 && halfT > 0) {
                    std::uniform_int_distribution<int> msDist(0, halfT);
                    auto &vars = sol->getVars();
                    for (int j = 1; j < nJobs - 1; ++j)
                        vars[nJobs + j] = msDist(rng_ms);
                }
                msProb->evaluate(sol);
                bestA1ms = std::min(bestA1ms, sol->getObjective(0));
                population->add(sol);
                initOrigin.push_back("a1");
                ++filled;
                ++injected;
            }
        }
        std::cout << "[NSGAII] A1 Priority-Rule Seed: injected "
                  << injected << " (LFT/MTS/GRPW), best ms=" << bestA1ms << "\n";
    }

    // ---- 優先規則＋ランダム性の初期解（prInitFrac > 0 のとき）----
    // 窓幅（max_shift / rho）の遺伝子は通常のランダム初期解と同じ分布のまま、
    // 作業リストだけを偏りランダム生成（4規則から一様に選ぶ）に置き換える。
    auto *prProb = dynamic_cast<RCPSP_Problem_MaxShift*>(problem_);
    static thread_local std::mt19937 rng_pr{std::random_device{}()};
    const int nPr = (prProb && prInitFrac > 0.0)
        ? (int)std::lround(prInitFrac * (populationSize - filled)) : 0;
    for (int k = 0; k < nPr && filled < populationSize; ++k, ++filled) {
        Solution *sol = prProb->createRandomTopoSolution();
        std::vector<int> lst = prProb->sampleBiasedPriorityList(-1, prAlpha, rng_pr);
        auto &vars = sol->getVars();
        for (int i = 0; i < (int)lst.size(); ++i) vars[i] = lst[i];
        problem_->evaluate(sol);
        population->add(sol);
        initOrigin.push_back("pr");
    }
    if (nPr > 0)
        std::cout << "[NSGAII] Priority-rule biased random init: " << nPr
                  << " (alpha=" << prAlpha << ")\n";

    //ランダムトポロジカルソートで生成
    for (; filled < populationSize; ++filled) {
        Solution *sol = nullptr;

        if (auto rcpsp = dynamic_cast<RCPSP_Problem*>(problem_)) {
            sol = rcpsp->createRandomTopoSolution();  // 先ほど作った関数
        } else {
            sol = new Solution(problem_);
        }

        problem_->evaluate(sol);
        population->add(sol);
        initOrigin.push_back("random");
    }

    // ---- traceFile: 初期集団の全個体と、世代ごとの要約 ----
    // list_ms = 同じ作業リストを窓幅0（最早配置）で復号したメイクスパン
    //         = 「作業リストそのものの工期の実力」。評価回数には数えない（診断専用）。
    std::ofstream trace;
    if (!traceFile.empty() && prProb) trace.open(traceFile);
    auto listMakespan = [&](Solution *s) -> double {
        Solution tmp(s);
        const int n = prProb->getNumJobs();
        auto &v = tmp.getVars();
        for (int j = 0; j < n && n + j < (int)v.size(); ++j) v[n + j] = 0;
        problem_->evaluate(&tmp);
        return tmp.getObjective(0);
    };
    auto writeGenSummary = [&](int gen, int evals) {
        if (!trace.is_open()) return;
        const int n = prProb->getNumJobs();
        double minMs = 1e18, minList = 1e18;
        std::vector<double> lms(population->size());
        for (int i = 0; i < population->size(); ++i) {
            Solution *s = population->get(i);
            minMs = std::min(minMs, s->getObjective(0));
            lms[i] = listMakespan(s);
            minList = std::min(minList, lms[i]);
        }
        // 工期側の個体（ms <= 1.05 * 最小）の数と、その中の異なる作業リストの数
        int nNear = 0, nZero = 0;
        double sumWin = 0.0;  // 集団の平均窓幅（復号の手間の目安。計算時間の比較用）
        std::set<std::vector<int>> nearLists;
        for (int i = 0; i < population->size(); ++i) {
            Solution *s = population->get(i);
            const auto &v = s->getVars();
            bool allZero = true;
            for (int j = 1; j < n - 1; ++j) { sumWin += v[n + j]; if (v[n + j] != 0) allZero = false; }
            if (allZero) ++nZero;
            if (s->getObjective(0) <= 1.05 * minMs) {
                ++nNear;
                nearLists.insert(std::vector<int>(v.begin(), v.begin() + n));
            }
        }
        // 作業リストの実力が最小の個体の、実際のメイクスパン
        double msOfBestList = 1e18;
        for (int i = 0; i < population->size(); ++i)
            if (lms[i] == minList) msOfBestList = std::min(msOfBestList, population->get(i)->getObjective(0));
        trace << "gen," << gen << "," << evals << "," << minMs << "," << minList << ","
              << msOfBestList << "," << nNear << "," << nearLists.size() << "," << nZero << ","
              << sumWin / std::max(1.0, (double)population->size() * std::max(1, n - 2)) << "\n";
    };
    if (trace.is_open()) {
        trace << "# init,idx,origin,ms,cost,list_ms\n"
                 "# gen,gen,evals,min_ms,min_list_ms,ms_of_best_list,n_near5pct,distinct_lists_near,n_all_zero_window,avg_window\n";
        for (int i = 0; i < population->size(); ++i) {
            Solution *s = population->get(i);
            trace << "init," << i << "," << (i < (int)initOrigin.size() ? initOrigin[i] : "?") << ","
                  << s->getObjective(0) << "," << s->getObjective(1) << "," << listMakespan(s) << "\n";
        }
        writeGenSummary(0, population->size());
    }

    // ---- 工期島（msIsland > 0 のとき）----
    // 初期集団の作業リストを窓幅0にして評価し、工期の短い順に重複なく msIsland 個を残す。
    // 島の評価も evaluations に数える（同じ予算の中でやりくりする）。
    std::vector<Solution *> island;
    int islandEvals = 0;
    double islandBest = std::numeric_limits<double>::max();
    int islandStall = 0;
    bool islandActive = true;
    auto zeroWindows = [&](Solution *s) {
        const int n = prProb->getNumJobs();
        auto &v = s->getVars();
        // j=0（開始ダミー）の遺伝子は目標工期に使うことがあるので残す（従来は常に0で、復号にも使われない）
        for (int j = 1; j < n && n + j < (int)v.size(); ++j) v[n + j] = 0;
        if (deadlineOnlyFlag && n < (int)v.size()) v[n] = 0;
    };
    auto islandSurvive = [&](std::vector<Solution *> &cand) {
        std::sort(cand.begin(), cand.end(), [](Solution *a, Solution *b) {
            if (a->getObjective(0) != b->getObjective(0)) return a->getObjective(0) < b->getObjective(0);
            return a->getObjective(1) < b->getObjective(1);
        });
        std::vector<Solution *> keep;
        std::set<std::vector<int>> seen;
        for (Solution *c : cand) {
            if ((int)keep.size() < msIsland && seen.insert(c->startTimes_).second) keep.push_back(c);
            else delete c;
        }
        island.swap(keep);
    };
    if (prProb && msIsland > 0) {
        std::vector<Solution *> cand;
        for (int i = 0; i < population->size(); ++i) {
            Solution *c = new Solution(population->get(i));
            zeroWindows(c);
            problem_->evaluate(c);
            ++islandEvals;
            cand.push_back(c);
        }
        islandSurvive(cand);
        std::cout << "[NSGAII] makespan island: size " << island.size()
                  << ", best ms=" << (island.empty() ? -1.0 : island[0]->getObjective(0)) << "\n";
    }

    // ---- コスト島（costIsland > 0 のとき）----
    std::vector<Solution *> cisland;
    double cislandBest = std::numeric_limits<double>::max();
    int cislandStall = 0;
    bool cislandActive = true;
    auto costSideGenes = [&](Solution *s) {   // コスト島で評価するときの遺伝子（目標工期の制限なし、λ=0）
        const int n = prProb->getNumJobs();
        auto &v = s->getVars();
        // 窓幅は親のものを引き継ぐ（全部を最大にすると作業が地平線の外へ押し出されて実行不能になる）
        if (n < (int)v.size()) v[n] = (int)problem_->getUpperLimit(n);              // 目標工期の制限なし
        if (n + n - 1 < (int)v.size()) v[n + n - 1] = 0;                            // λ = 0
    };
    auto cislandSurvive = [&](std::vector<Solution *> &cand) {
        std::sort(cand.begin(), cand.end(), [](Solution *a, Solution *b) {
            if (a->getObjective(1) != b->getObjective(1)) return a->getObjective(1) < b->getObjective(1);
            return a->getObjective(0) < b->getObjective(0);
        });
        std::vector<Solution *> keep;
        std::set<std::vector<int>> seen;
        for (Solution *c : cand) {
            if ((int)keep.size() < costIsland && seen.insert(c->startTimes_).second) keep.push_back(c);
            else delete c;
        }
        cisland.swap(keep);
    };
    if (prProb && costIsland > 0) {
        std::vector<Solution *> cand;
        for (int i = 0; i < population->size(); ++i) {
            Solution *c = new Solution(population->get(i));
            costSideGenes(c);
            problem_->evaluate(c);
            ++islandEvals;
            cand.push_back(c);
        }
        cislandSurvive(cand);
        std::cout << "[NSGAII] cost island: size " << cisland.size()
                  << ", best cost=" << (cisland.empty() ? -1.0 : cisland[0]->getObjective(1)) << "\n";
    }

    evaluations = population->size() + islandEvals;
    // 親・子の内訳（traceChildren のとき）。書き出すたびに 0 に戻す
    long dParents = 0, dParentsNear = 0, dChildren = 0, dChildBetter = 0, dChildListBetter = 0, dChildNear = 0, dMigrantBetter = 0;
    long dChildListNew = 0;
    double mateBest = std::numeric_limits<double>::max(); int mateStall = 0; bool mateActive = true;
    double mateChildBest = std::numeric_limits<double>::max(), mateChildBestPrev = std::numeric_limits<double>::max();
    auto writeChildLine = [&](int gen, int evals) {
        if (!trace.is_open() || !traceChildren) return;
        trace << "child," << gen << "," << evals << "," << dParents << "," << dParentsNear << "," << dChildren << ","
              << dChildBetter << "," << dChildListBetter << "," << dChildNear << "," << dMigrantBetter << "," << dChildListNew << "\n";
        dParents = dParentsNear = dChildren = dChildBetter = dChildListBetter = dChildNear = dMigrantBetter = 0; dChildListNew = 0;
    };
    if (trace.is_open() && traceChildren)
        trace << "# child,gen,evals,parents,parents_near5pct,children,child_better_than_popmin,"
                 "child_list_better_than_popmin,child_near5pct,migrant_better,child_list_better_than_both_mins\n";

    // ---- タスク1: 世代ログ準備 (MaxShift 診断) ----
    // generation_log.csv に append。4戦略分が同一ファイルに蓄積される。
    // run_id は静的カウンタで管理し、ヘッダーは最初の1回のみ書く。
    static thread_local int  sRunId         = 0;
    static thread_local bool sHeaderWritten = false;
    ++sRunId;
    const bool msMode = (dynamic_cast<RCPSP_Problem_MaxShift*>(problem_) != nullptr);
    int genCount = 0;
    std::ofstream genLog;
    if (diagnosticLogs && msMode) {
        {
            int stratId = dynamic_cast<RCPSP_Problem*>(problem_)->getStrategy();
            const std::string logDir = "logs/generation/";
            fs::create_directories(logDir);
            std::string logFileName = logDir + "generation_log_s" + std::to_string(stratId) + ".csv";
            genLog.open(logFileName,
                        sRunId == 1 ? std::ios::trunc : std::ios::app);
        }
        if (!sHeaderWritten && genLog.is_open()) {
            genLog << "run_id,generation,avg_max_shift,min_makespan_in_front,"
                      "count_all_zero_shift,min_max_shift,max_max_shift\n";
            sHeaderWritten = true;
        }
    }

    // ---- innovation ログ準備 (有効イノベーション率) ----
    std::ofstream innovLog;

    // ---- many-to-one ログ準備 (全エンコーディング共通) ----
    // manytoone_log_s{stratId}.csv: 世代ごとに母集団内の
    // 「actList が異なるがスケジュール(startTimes_)が同じ」解の数を記録する
    std::ofstream m2oLog;
    // noveltyFilter ON のときはログ名に _NF を付けて既存ログと区別する
    // encodingName() で派生クラスごとに固有のタグを返す
    std::string encTag = "SchedObj";
    if (auto rcpsp = dynamic_cast<RCPSP_Problem*>(problem_)) {
        encTag = rcpsp->encodingName();
    }
    if (noveltyFilter) encTag += "_NF";
    if (diagnosticLogs) {
        if (auto rcpsp = dynamic_cast<RCPSP_Problem*>(problem_)) {
            int stratId = rcpsp->getStrategy();
            const std::string &inst = rcpsp->getInstancePrefix();
            const std::string m2oDir = "logs/manytoone/";
            fs::create_directories(m2oDir);
            std::string m2oFileName = m2oDir + "manytoone_log_" + inst
                                      + "_s" + std::to_string(stratId)
                                      + "_" + encTag + ".csv";
            const bool doTrunc = (sRunId == 1);
            m2oLog.open(m2oFileName, doTrunc ? std::ios::trunc : std::ios::app);
            if (m2oLog.is_open() && doTrunc) {
                m2oLog << "# run_id       : trial index (which independent run)\n"
                          "# generation   : generation number (1-based)\n"
                          "# total_pop    : population size\n"
                          "# unique_scheds: number of unique schedules (startTimes) in population\n"
                          "# dup_sol_count: solutions in groups where 2+ distinct actLists\n"
                          "#                map to the same schedule\n"
                          "# dup_percent  : dup_sol_count / total_pop * 100 (%)\n"
                          "run_id,generation,total_pop,unique_scheds,"
                          "dup_sol_count,dup_percent\n";
            }

            // innovation ログファイルオープン
            const std::string innovDir = "logs/innovation/";
            fs::create_directories(innovDir);
            std::string innovFileName = innovDir + "innovation_log_" + inst
                                        + "_s" + std::to_string(stratId)
                                        + "_" + encTag + ".csv";
            innovLog.open(innovFileName, doTrunc ? std::ios::trunc : std::ios::app);
            if (innovLog.is_open() && doTrunc) {
                innovLog << "run_id,generation,offspring_evaluated,offspring_accepted,"
                            "novel_sched,dominates_parent,nondominated_vs_parents,"
                            "improves_extremes\n";
            }
        }
    }

    // メインループ

    while (evaluations < maxEvaluations) {
        static const int LOG_EVERY_EVAL = 20000;
        if (evaluations % LOG_EVERY_EVAL == 0) {
            std::cout << "evaluations = " << evaluations << std::endl;
        }

        offspringPopulation = new SolutionSet(populationSize);

        // ---- B: 交叉バイアス用 - 今世代の makespan 最小解を探索 ----
        Solution *makespanMinSol = nullptr;
        if (msMode && makespanBiasProb > 0.0) {
            double minMs = std::numeric_limits<double>::max();
            for (int i = 0; i < population->size(); ++i) {
                double ms = population->get(i)->getObjective(0);
                if (ms < minMs) { minMs = ms; makespanMinSol = population->get(i); }
            }
        }

        // ---- 2026-10-21: 1集団での工期島の模倣（msSel / costSel）と、親・子の内訳の記録 ----
        double genPopMin = std::numeric_limits<double>::max();
        for (int i = 0; i < population->size(); ++i)
            genPopMin = std::min(genPopMin, population->get(i)->getObjective(0));
        // 集団の中の作業リストの実力（窓幅0）の最小。子の作業リストが「本当に新しく良い」かを判定するため（診断専用）
        double genPopMinList = std::numeric_limits<double>::max();
        if (traceChildren && prProb)
            for (int i = 0; i < population->size(); ++i)
                genPopMinList = std::min(genPopMinList, listMakespan(population->get(i)));
        std::vector<Solution *> topMs, topCost;
        auto buildTop = [&](std::vector<Solution *> &top, int obj, double frac) {
            std::vector<Solution *> all;
            for (int i = 0; i < population->size(); ++i) all.push_back(population->get(i));
            std::sort(all.begin(), all.end(), [obj](Solution *a, Solution *b) {
                if (a->getObjective(obj) != b->getObjective(obj)) return a->getObjective(obj) < b->getObjective(obj);
                return a->getObjective(1 - obj) < b->getObjective(1 - obj);
            });
            int K = std::max(2, (int)std::lround(frac * all.size()));
            top.assign(all.begin(), all.begin() + std::min(K, (int)all.size()));
        };
        if (msSelProb > 0.0 || msMateProb > 0.0) buildTop(topMs, 0, msSelTop);
        if (msMateProb > 0.0 && mateActive) {
            if (msMateStallChild) {
                // 前の世代までに生まれた工期側の子（窓幅0）の最良が改善したか
                if (mateChildBest < mateChildBestPrev) { mateChildBestPrev = mateChildBest; mateStall = 0; }
                else if (msMatePatience > 0 && ++mateStall >= msMatePatience) mateActive = false;
            } else {
                if (genPopMin < mateBest) { mateBest = genPopMin; mateStall = 0; }
                else if (msMatePatience > 0 && ++mateStall >= msMatePatience) mateActive = false;
            }
        }
        if (costSelProb > 0.0) buildTop(topCost, 1, costSelTop);
        auto tourBy = [&](std::vector<Solution *> &top, int obj) -> Solution * {
            std::uniform_int_distribution<int> pk(0, (int)top.size() - 1);
            Solution *a = top[pk(rng_ms)], *b = top[pk(rng_ms)];
            if (a->getObjective(obj) != b->getObjective(obj)) return a->getObjective(obj) < b->getObjective(obj) ? a : b;
            return a->getObjective(1 - obj) <= b->getObjective(1 - obj) ? a : b;
        };
        auto pickParent = [&]() -> Solution * {
            Solution *pp;
            double r = d01_ms(rng_ms);
            if (r < msSelProb && topMs.size() >= 2) pp = tourBy(topMs, 0);
            else if (r < msSelProb + costSelProb && topCost.size() >= 2) pp = tourBy(topCost, 1);
            else pp = (Solution *)selectionOperator->execute(population);
            if (traceChildren) {
                ++dParents;
                if (pp->getObjective(0) <= 1.05 * genPopMin) ++dParentsNear;
            }
            return pp;
        };

        // ---- innovation ログ用: 親世代の情報を収集 ----
        // 親スケジュール集合（novel_sched 判定用）
        std::set<std::vector<int>> parentSchedsForInnov;
        // 親目的値ペア（支配判定用）
        std::vector<std::pair<double,double>> parentObjs;
        double parentMinMakespan = std::numeric_limits<double>::max();
        double parentMinCost     = std::numeric_limits<double>::max();
        // innovation カウンタ
        int innov_offspring_evaluated = 0;
        int innov_offspring_accepted  = 0;
        int innov_novel_sched         = 0;
        int innov_dominates_parent    = 0;
        int innov_nondominated        = 0;
        int innov_improves_extremes   = 0;

        if (innovLog.is_open()) {
            if (auto rcpsp = dynamic_cast<RCPSP_Problem*>(problem_)) {
                for (int i = 0; i < population->size(); ++i) {
                    Solution *s = population->get(i);
                    if (!s->startTimes_.empty())
                        parentSchedsForInnov.insert(s->startTimes_);
                    double ms = s->getObjective(0);
                    double co = s->getObjective(1);
                    parentObjs.push_back({ms, co});
                    if (ms < parentMinMakespan) parentMinMakespan = ms;
                    if (co < parentMinCost)     parentMinCost = co;
                }
            }
        }

        // innovation 記録ラムダ: 子1体の評価直後に呼ぶ
        auto recordChild = [&](Solution *child, bool accepted) {
            if (!innovLog.is_open()) return;
            ++innov_offspring_evaluated;
            if (accepted) ++innov_offspring_accepted;
            double cMs = child->getObjective(0);
            double cCo = child->getObjective(1);
            // 1e9 フォールバック解は除外
            if (cMs >= 1e8) return;
            // novel_sched
            if (!child->startTimes_.empty()
                && parentSchedsForInnov.find(child->startTimes_) == parentSchedsForInnov.end()) {
                ++innov_novel_sched;
            }
            // dominates_parent: 親の少なくとも1解を支配するか
            bool dominatesAny = false;
            bool dominatedByAny = false;
            for (auto &[pMs, pCo] : parentObjs) {
                // child dominates parent: 両目的 ≤ かつ少なくとも一方で <
                if (cMs <= pMs && cCo <= pCo && (cMs < pMs || cCo < pCo)) {
                    dominatesAny = true;
                }
                // parent dominates child
                if (pMs <= cMs && pCo <= cCo && (pMs < cMs || pCo < cCo)) {
                    dominatedByAny = true;
                }
            }
            if (dominatesAny) ++innov_dominates_parent;
            if (!dominatedByAny) ++innov_nondominated;
            // improves_extremes
            if (cMs < parentMinMakespan || cCo < parentMinCost) {
                ++innov_improves_extremes;
            }
        };

        if (noveltyFilter) {
            // ================================================================
            // ノベルティフィルタ版
            //   2 個ずつ生成 → 突然変異まで一気に適用 →
            //   親世代にないスケジュール(startTimes_)の解のみ子孫集団に追加
            //   最大 5×populationSize 回試行で打ち切り
            //   (重複率が高い後半世代でも無限ループしない)
            // ================================================================
            std::set<std::vector<int>> parentScheds;
            if (auto rcpsp = dynamic_cast<RCPSP_Problem*>(problem_)) {
                for (int i = 0; i < population->size(); ++i) {
                    Solution *s = population->get(i);
                    if (!s->startTimes_.empty())
                        parentScheds.insert(s->startTimes_);
                }
            }

            const int maxTries = 5 * populationSize;
            int tries = 0;
            while (offspringPopulation->size() < populationSize
                   && evaluations < maxEvaluations
                   && tries < maxTries) {
                ++tries;

                // ---- B: makespan バイアス（元のコードと同じ）----
                Solution *p1;
                if (msMode && makespanMinSol && d01_ms(rng_ms) < makespanBiasProb)
                    p1 = makespanMinSol;
                else
                    p1 = (Solution *)selectionOperator->execute(population);
                Solution *p2 = (Solution *)selectionOperator->execute(population);

                void **parents = new void*[2];
                parents[0] = p1;
                parents[1] = p2;
                void **offs = (void **)crossoverOperator->execute(parents);
                delete[] parents;

                Solution *c1 = (Solution *)offs[0];
                Solution *c2 = (Solution *)offs[1];
                delete[] offs;

                mutationOperator->execute(c1);
                mutationOperator->execute(c2);
                problem_->evaluate(c1);
                problem_->evaluate(c2);
                evaluations += 2;

                // 親世代にないスケジュールなら採用、あれば破棄
                bool c1Novel = c1->startTimes_.empty()
                               || parentScheds.find(c1->startTimes_) == parentScheds.end();
                bool c2Novel = c2->startTimes_.empty()
                               || parentScheds.find(c2->startTimes_) == parentScheds.end();

                bool c1Accepted = c1Novel && offspringPopulation->size() < populationSize;
                if (c1Accepted)
                    offspringPopulation->add(c1);

                recordChild(c1, c1Accepted);
                if (!c1Accepted) delete c1;

                bool c2Accepted = c2Novel && offspringPopulation->size() < populationSize;
                if (c2Accepted)
                    offspringPopulation->add(c2);

                recordChild(c2, c2Accepted);
                if (!c2Accepted) delete c2;
            }

        } else {
            // ================================================================
            // 元の NSGA-II 子孫生成（変更なし）
            // ================================================================
            for (int i = 0; i < populationSize && evaluations < maxEvaluations; i += 2) {
                // ---- B: makespan 最小解を親の一方に固定（確率 makespanBiasProb）----
                Solution *p1, *p2;
                const bool mate = prProb && msMateProb > 0.0 && mateActive && topMs.size() >= 2 && d01_ms(rng_ms) < msMateProb;
                if (mate) {
                    p1 = tourBy(topMs, 0); p2 = tourBy(topMs, 0);
                    if (traceChildren) { dParents += 2; dParentsNear += (p1->getObjective(0) <= 1.05 * genPopMin) + (p2->getObjective(0) <= 1.05 * genPopMin); }
                } else {
                    if (msMode && makespanMinSol && d01_ms(rng_ms) < makespanBiasProb) {
                        p1 = makespanMinSol;
                        if (traceChildren) { ++dParents; if (p1->getObjective(0) <= 1.05 * genPopMin) ++dParentsNear; }
                    } else
                        p1 = pickParent();
                    p2 = pickParent();
                }

                void **parents = new void*[2];
                parents[0] = p1;
                parents[1] = p2;

                void **offs = (void **)crossoverOperator->execute(parents);
                delete [] parents;

                Solution *c1 = (Solution *)offs[0];
                Solution *c2 = (Solution *)offs[1];

                mutationOperator->execute(c1);
                mutationOperator->execute(c2);

                // ---- 部分再構築変異（regenProb > 0 のとき）----
                // 作業リストの先頭 keep 個を残し、残りを優先規則＋ランダム性で作り直す。
                // 挿入変異（1作業ずつ）では届かない「作業のかたまりの順序替え」を起こすため。
                // 窓幅の遺伝子は作業ごとに持っているので、そのまま引き継がれる。
                if (prProb && regenProb > 0.0) {
                    const int n = prProb->getNumJobs();
                    std::uniform_int_distribution<int> kd(1, std::max(1, n - 2));
                    for (Solution *c : {c1, c2}) {
                        if (d01_ms(rng_pr) >= regenProb) continue;
                        auto &v = c->getVars();
                        std::vector<int> cur(v.begin(), v.begin() + n);
                        std::vector<int> nl = prProb->sampleBiasedPriorityList(-1, prAlpha, rng_pr, &cur, kd(rng_pr));
                        for (int i = 0; i < n; ++i) v[i] = nl[i];
                    }
                }

                // ---- 連続ブロックの窓幅変異（blockWinProb > 0 のとき）----
                // 作業リストの連続する L 個（L は 2〜n/5）の窓幅遺伝子を、半分の確率で同じ値に、
                // 残りの半分で同じ量だけずらす（上下限でクリップ）。押し出しの連鎖をまとめて動かすため。
                if (prProb && blockWinProb > 0.0) {
                    const int n = prProb->getNumJobs();
                    const int up = (int)problem_->getUpperLimit(n + 1);
                    std::uniform_int_distribution<int> Ld(2, std::max(2, n / 5));
                    std::uniform_int_distribution<int> keyd(0, std::max(0, up));
                    std::uniform_int_distribution<int> dd(-std::max(1, up / 4), std::max(1, up / 4));
                    for (Solution *c : {c1, c2}) {
                        if (d01_ms(rng_pr) >= blockWinProb || up <= 0) continue;
                        auto &v = c->getVars();
                        const int L = Ld(rng_pr);
                        std::uniform_int_distribution<int> kd2(1, std::max(1, n - 1 - L));
                        const int k = kd2(rng_pr);
                        const bool same = d01_ms(rng_pr) < 0.5;
                        const int val = keyd(rng_pr), delta = dd(rng_pr);
                        for (int i = k; i < k + L && i < n - 1; ++i) {
                            int job = v[i];
                            if (job <= 0 || job >= n - 1) continue;
                            int nv = same ? val : v[n + job] + delta;
                            v[n + job] = std::max(0, std::min(up, nv));
                        }
                    }
                }

                // ---- 目標工期の遺伝子の変異（deadlineMutProb > 0 のとき）----
                // 90% は今の値から正規分布（標準偏差 R/10）で動かし、10% は一様乱数で選び直す。
                if (prProb && deadlineMutProb > 0.0) {
                    const int n = prProb->getNumJobs();
                    const int up = (int)problem_->getUpperLimit(n);
                    std::normal_distribution<double> nd(0.0, std::max(1.0, up / 10.0));
                    std::uniform_int_distribution<int> ud(0, std::max(0, up));
                    for (Solution *c : {c1, c2}) {
                        if (d01_ms(rng_pr) >= deadlineMutProb || up <= 0) continue;
                        auto &v = c->getVars();
                        int nv = d01_ms(rng_pr) < 0.1 ? ud(rng_pr) : (int)std::lround(v[n] + nd(rng_pr));
                        v[n] = std::max(0, std::min(up, nv));
                    }
                }

                // ---- λ 遺伝子の変異（lambdaMutProb > 0 のとき）: 目標工期と同じ作り（90% 正規分布 sd R/10、10% 一様）----
                if (prProb && lambdaMutProb > 0.0) {
                    const int n = prProb->getNumJobs();
                    const int up = (int)problem_->getUpperLimit(n + n - 1);
                    std::normal_distribution<double> nd(0.0, std::max(1.0, up / 10.0));
                    std::uniform_int_distribution<int> ud(0, std::max(0, up));
                    for (Solution *c : {c1, c2}) {
                        if (d01_ms(rng_pr) >= lambdaMutProb || up <= 0) continue;
                        auto &v = c->getVars();
                        int nv = d01_ms(rng_pr) < 0.1 ? ud(rng_pr) : (int)std::lround(v[n + n - 1] + nd(rng_pr));
                        v[n + n - 1] = std::max(0, std::min(up, nv));
                    }
                }

                // ---- 時間ブロックの窓幅変異（timeBlockWinProb > 0 のとき）----
                // 子が受け継いだ親のスケジュール（startTimes_）で、ある作業の開始時刻から W コマ以内に始まる作業をまとめ、
                // それらの窓幅を半分の確率で同じ値に、残りで同じ量ずらす。W は 1〜T/10 の一様乱数。
                if (prProb && timeBlockWinProb > 0.0) {
                    const int n = prProb->getNumJobs();
                    const int up = (int)problem_->getUpperLimit(n + 1);
                    const int Th = std::max(10, prProb->getHorizon());
                    std::uniform_int_distribution<int> jd(1, std::max(1, n - 2));
                    std::uniform_int_distribution<int> Wd(1, std::max(1, Th / 10));
                    std::uniform_int_distribution<int> keyd(0, std::max(0, up));
                    std::uniform_int_distribution<int> dd(-std::max(1, up / 4), std::max(1, up / 4));
                    for (Solution *c : {c1, c2}) {
                        if (d01_ms(rng_pr) >= timeBlockWinProb || up <= 0) continue;
                        const auto &st = c->startTimes_;
                        if ((int)st.size() != n) continue;
                        auto &v = c->getVars();
                        const int j0 = jd(rng_pr), W = Wd(rng_pr);
                        const bool same = d01_ms(rng_pr) < 0.5;
                        const int val = keyd(rng_pr), delta = dd(rng_pr);
                        for (int job = 1; job < n - 1; ++job) {
                            if (std::abs(st[job] - st[j0]) > W) continue;
                            int nv = same ? val : v[n + job] + delta;
                            v[n + job] = std::max(0, std::min(up, nv));
                        }
                    }
                }

                // ---- 窓幅0の子（zeroWinProb > 0 のとき）: 工期島の「窓幅0で評価」を1集団の中で模す ----
                if (prProb && zeroWinProb > 0.0) {
                    for (Solution *c : {c1, c2})
                        if (d01_ms(rng_pr) < zeroWinProb) zeroWindows(c);
                }
                if (mate) { zeroWindows(c1); zeroWindows(c2); }

                problem_->evaluate(c1);
                problem_->evaluate(c2);

                if (traceChildren && prProb) {
                    for (Solution *c : {c1, c2}) {
                        ++dChildren;
                        double cm = c->getObjective(0);
                        if (cm < genPopMin) ++dChildBetter;
                        if (cm <= 1.05 * genPopMin) ++dChildNear;
                        double lm = listMakespan(c);
                        if (lm < genPopMin) ++dChildListBetter;
                        if (lm < std::min(genPopMin, genPopMinList)) ++dChildListNew;
                    }
                }

                if (mate) mateChildBest = std::min({mateChildBest, c1->getObjective(0), c2->getObjective(0)});
                if (mate && msMateFilter) {
                    // 工期側の組の子は、集団の最良メイクスパンより短いときだけ受け入れる（それ以外は評価だけして捨てる）
                    for (Solution *c : {c1, c2}) {
                        if (c->getObjective(0) < genPopMin && offspringPopulation->size() < populationSize) {
                            offspringPopulation->add(c); recordChild(c, true);
                        } else {
                            recordChild(c, false); delete c;
                        }
                    }
                    delete [] offs;
                    evaluations += 2;
                    continue;
                }

                offspringPopulation->add(c1);
                recordChild(c1, true);

                if (offspringPopulation->size() < populationSize) {
                    offspringPopulation->add(c2);
                    recordChild(c2, true);
                } else {
                    recordChild(c2, false);
                    delete c2;
                }
                delete [] offs;

                evaluations += 2;
            }
        } // end noveltyFilter branch

        // ---- 工期島の1世代（msIsland > 0 のとき）----
        // 島の中だけで交叉・変異（と部分再構築）を行い、窓幅0・メイクスパンだけで生き残りを決める。
        // 最良の作業リストが NSGA の集団に無ければ、移民として unionPopulation に加える。
        Solution *migrant = nullptr;
        if (prProb && msIsland > 0 && islandActive && island.size() >= 2) {
            std::vector<Solution *> cand(island.begin(), island.end());
            std::uniform_int_distribution<int> pick(0, (int)island.size() - 1);
            auto tour = [&]() {
                Solution *a = island[pick(rng_pr)], *b = island[pick(rng_pr)];
                return a->getObjective(0) <= b->getObjective(0) ? a : b;
            };
            const int n = prProb->getNumJobs();
            std::uniform_int_distribution<int> kd(1, std::max(1, n - 2));
            for (int k = 0; k < msIslandKids && evaluations < maxEvaluations; k += 2) {
                void **par = new void*[2];
                par[0] = tour(); par[1] = tour();
                void **offs = (void **)crossoverOperator->execute(par);
                delete[] par;
                for (int c = 0; c < 2; ++c) {
                    Solution *ch = (Solution *)offs[c];
                    mutationOperator->execute(ch);
                    if (regenProb > 0.0 && d01_ms(rng_pr) < regenProb) {
                        auto &v = ch->getVars();
                        std::vector<int> cur(v.begin(), v.begin() + n);
                        std::vector<int> nl = prProb->sampleBiasedPriorityList(-1, prAlpha, rng_pr, &cur, kd(rng_pr));
                        for (int i = 0; i < n; ++i) v[i] = nl[i];
                    }
                    zeroWindows(ch);
                    problem_->evaluate(ch);
                    ++evaluations;
                    cand.push_back(ch);
                }
                delete[] offs;
            }
            islandSurvive(cand);
            if (!island.empty() && island[0]->getObjective(0) < islandBest) {
                islandBest = island[0]->getObjective(0); islandStall = 0;
            } else if (++islandStall >= msIslandPatience && msIslandPatience > 0) {
                islandActive = false;   // 以後は子を作らない（最後の移民判定だけは下で行う）
            }
            double popMin = std::numeric_limits<double>::max();
            std::set<std::vector<int>> popScheds;
            for (int i = 0; i < population->size(); ++i) {
                popMin = std::min(popMin, population->get(i)->getObjective(0));
                popScheds.insert(population->get(i)->startTimes_);
            }
            if (!island.empty() && island[0]->getObjective(0) <= popMin
                && popScheds.find(island[0]->startTimes_) == popScheds.end())
                migrant = island[0];
            if (traceChildren && migrant && migrant->getObjective(0) < genPopMin) ++dMigrantBetter;
        }

        // ---- コスト島の1世代（costIsland > 0 のとき）: 工期島と同じ作りで、目的がコスト・窓幅最大 ----
        Solution *cmigrant = nullptr;
        if (prProb && costIsland > 0 && cislandActive && cisland.size() >= 2) {
            std::vector<Solution *> cand(cisland.begin(), cisland.end());
            std::uniform_int_distribution<int> pick(0, (int)cisland.size() - 1);
            auto tour = [&]() {
                Solution *a = cisland[pick(rng_pr)], *b = cisland[pick(rng_pr)];
                return a->getObjective(1) <= b->getObjective(1) ? a : b;
            };
            const int n = prProb->getNumJobs();
            std::uniform_int_distribution<int> kd(1, std::max(1, n - 2));
            for (int k = 0; k < costIslandKids && evaluations < maxEvaluations; k += 2) {
                void **par = new void*[2];
                par[0] = tour(); par[1] = tour();
                void **offs = (void **)crossoverOperator->execute(par);
                delete[] par;
                for (int c = 0; c < 2; ++c) {
                    Solution *ch = (Solution *)offs[c];
                    mutationOperator->execute(ch);
                    if (regenProb > 0.0 && d01_ms(rng_pr) < regenProb) {
                        auto &v = ch->getVars();
                        std::vector<int> cur(v.begin(), v.begin() + n);
                        std::vector<int> nl = prProb->sampleBiasedPriorityList(-1, prAlpha, rng_pr, &cur, kd(rng_pr));
                        for (int i = 0; i < n; ++i) v[i] = nl[i];
                    }
                    costSideGenes(ch);
                    problem_->evaluate(ch);
                    ++evaluations;
                    cand.push_back(ch);
                }
                delete[] offs;
            }
            cislandSurvive(cand);
            if (!cisland.empty() && cisland[0]->getObjective(1) < cislandBest - 1e-9) {
                cislandBest = cisland[0]->getObjective(1); cislandStall = 0;
            } else if (++cislandStall >= costIslandPatience && costIslandPatience > 0) {
                cislandActive = false;
            }
            double popMinC = std::numeric_limits<double>::max();
            std::set<std::vector<int>> popScheds;
            for (int i = 0; i < population->size(); ++i) {
                popMinC = std::min(popMinC, population->get(i)->getObjective(1));
                popScheds.insert(population->get(i)->startTimes_);
            }
            if (!cisland.empty() && cisland[0]->getObjective(1) <= popMinC
                && popScheds.find(cisland[0]->startTimes_) == popScheds.end())
                cmigrant = cisland[0];
        }

        // 親 + 子 = unionPopulation
        unionPopulation = new SolutionSet(population->size() + offspringPopulation->size() + (migrant ? 1 : 0) + (cmigrant ? 1 : 0));
        for (int i = 0; i < population->size(); ++i) {
            unionPopulation->add(new Solution(population->get(i)));
        }
        for (int i = 0; i < offspringPopulation->size(); ++i) {
            unionPopulation->add(new Solution(offspringPopulation->get(i)));
        }
        if (migrant) unionPopulation->add(new Solution(migrant));
        if (cmigrant) unionPopulation->add(new Solution(cmigrant));

        Ranking ranking(unionPopulation);

        SolutionSet *nextGen = new SolutionSet(populationSize);
        int remain = populationSize;
        int idx = 0;

        while (remain > 0 && idx < ranking.getNumberOfSubfronts()) {
            SolutionSet *front = ranking.getSubfront(idx);
            if (front->size() <= remain) {
                for (int k = 0; k < front->size(); ++k)
                    nextGen->add(new Solution(front->get(k)));
                remain -= front->size();
            } else {
                ranking.crowdingDistanceAssignment(front, problem_->getNumberOfObjectives());
                CrowdingDistanceComparator cdcLocal;
                front->sort(&cdcLocal);
                for (int k = 0; k < remain; ++k)
                    nextGen->add(new Solution(front->get(k)));
                remain = 0;
            }
            ++idx;
        }

        // ---- A: エリート保存 - makespan 最小解を nextGen に強制保存 ----
        // unionPopulation 削除前に実行する必要がある
        if (msMode && eliteMakespanSlots > 0) {
            // unionPopulation から makespan 昇順でソートし上位候補を取得
            std::vector<std::pair<double, Solution *>> cands;
            cands.reserve(unionPopulation->size());
            for (int i = 0; i < unionPopulation->size(); ++i) {
                Solution *s = unionPopulation->get(i);
                cands.push_back({s->getObjective(0), s});
            }
            std::sort(cands.begin(), cands.end());

            // nextGen でカバーされていない候補（より小さい makespan を持つ）を収集
            std::vector<Solution *> toInject;
            for (int e = 0; e < eliteMakespanSlots && e < (int)cands.size(); ++e) {
                double eliteMs = cands[e].first;
                bool covered = false;
                for (int k = 0; k < nextGen->size(); ++k) {
                    if (nextGen->get(k)->getObjective(0) <= eliteMs) {
                        covered = true;
                        break;
                    }
                }
                if (!covered) toInject.push_back(cands[e].second);
            }

            // 注入が必要な場合: nextGen の末尾（NSGA-II 優先度が最低の解）と入れ替え
            if (!toInject.empty()) {
                int injectCount = (int)toInject.size();
                int keep = populationSize - injectCount;
                SolutionSet *newNextGen = new SolutionSet(populationSize);
                // 先頭 keep 個はそのまま保持（NSGA-II ランク・crowding 優先順）
                for (int k = 0; k < keep && k < nextGen->size(); ++k)
                    newNextGen->add(new Solution(nextGen->get(k)));
                // makespan エリートを追加
                for (Solution *e : toInject)
                    newNextGen->add(new Solution(e));
                delete nextGen;
                nextGen = newNextGen;
            }
        }

        delete unionPopulation;
        unionPopulation = nullptr;

        delete offspringPopulation;
        offspringPopulation = nullptr;

        delete population;

        population = nextGen;

        // ---- タスク1: 世代ごとの統計をログ ----
        ++genCount;
        // 診断の追加復号を抑えるため、最初の50世代は毎世代、その後は10世代ごとに記録
        if (genCount <= 50 || genCount % 10 == 0) { writeGenSummary(genCount, evaluations); writeChildLine(genCount, evaluations); }
        if (msMode && genLog.is_open()) {
            auto *msProb = dynamic_cast<RCPSP_Problem_MaxShift*>(problem_);
            const int nJobs = msProb->getNumJobs();
            const int popSz = population->size();
            const int innerJobs = std::max(1, nJobs - 2);  // ダミー端点除く

            double sumShift  = 0.0;
            int    minShift  = std::numeric_limits<int>::max();
            int    maxShift  = std::numeric_limits<int>::min();
            int    cntAllZero = 0;
            double minMakespan = std::numeric_limits<double>::max();

            for (int i = 0; i < popSz; ++i) {
                Solution  *s    = population->get(i);
                const auto &vars = s->getVars();
                bool allZero = true;
                for (int j = 1; j < nJobs - 1; ++j) {
                    int v = vars[nJobs + j];
                    sumShift += v;
                    if (v < minShift) minShift = v;
                    if (v > maxShift) maxShift = v;
                    if (v != 0) allZero = false;
                }
                if (allZero) ++cntAllZero;
                double ms = s->getObjective(0);
                if (ms < minMakespan) minMakespan = ms;
            }

            // 母集団中の makespan 最小 = パレートフロント上の makespan 最小
            // （最小 makespan 解を支配できる解は存在しないため）
            double avgShift = sumShift / static_cast<double>(popSz * innerJobs);
            genLog << sRunId << "," << genCount << "," << avgShift << ","
                   << static_cast<int>(minMakespan) << "," << cntAllZero << ","
                   << minShift << "," << maxShift << "\n";
            if (genCount % 50 == 0) genLog.flush();  // 毎世代 flush は I/O 負荷過大のため 50 世代ごとに変更
        }

        // ---- many-to-one 集計: actList が違うがスケジュールが同じ解を数える ----
        // 出力列:
        //   total_pop     : 母集団サイズ
        //   unique_scheds : 母集団内のユニーク startTimes_ 数
        //   dup_sol_count : 「同一スケジュールで異なるactListを持つグループ」に
        //                   属する解の総数（何個中何個、の「何個」側）
        //   dup_percent   : dup_sol_count / total_pop × 100
        if (m2oLog.is_open()) {
            if (auto rcpsp = dynamic_cast<RCPSP_Problem*>(problem_)) {
                const int nJobs = rcpsp->getNumJobs();
                const int popSz = population->size();

                // startTimes_ -> actList の全リスト（重複あり）
                std::map<std::vector<int>, std::vector<std::vector<int>>> schedToActLists;
                for (int i = 0; i < popSz; ++i) {
                    Solution *s = population->get(i);
                    if (s->startTimes_.empty()) continue;
                    const auto &vars = s->getVars();
                    std::vector<int> actList(vars.begin(),
                                             vars.begin() + std::min(nJobs, (int)vars.size()));
                    schedToActLists[s->startTimes_].push_back(actList);
                }

                int uniqueScheds = static_cast<int>(schedToActLists.size());
                // グループ内に2種類以上の distinct actList がある → many-to-one
                // そのグループに属する解の総数（実際の解数）を数える
                int dupGroupSols = 0;
                for (auto &[sched, actLists] : schedToActLists) {
                    std::set<std::vector<int>> distinct(actLists.begin(), actLists.end());
                    if ((int)distinct.size() > 1) {
                        dupGroupSols += static_cast<int>(actLists.size());
                    }
                }
                double dupPct = (popSz > 0)
                    ? 100.0 * dupGroupSols / static_cast<double>(popSz) : 0.0;

                m2oLog << sRunId << "," << genCount << "," << popSz << ","
                       << uniqueScheds << "," << dupGroupSols << ","
                       << dupPct << "\n";
                if (genCount % 50 == 0) m2oLog.flush();
            }
        }

        // ---- innovation ログ書き込み ----
        if (innovLog.is_open()) {
            innovLog << sRunId << "," << genCount << ","
                     << innov_offspring_evaluated << ","
                     << innov_offspring_accepted << ","
                     << innov_novel_sched << ","
                     << innov_dominates_parent << ","
                     << innov_nondominated << ","
                     << innov_improves_extremes << "\n";
            if (genCount % 50 == 0) innovLog.flush();
        }
    }
    for (Solution *s : island) delete s;
    for (Solution *s : cisland) delete s;
    setOutputParameter("evaluations", &evaluations);
    return population;
}







