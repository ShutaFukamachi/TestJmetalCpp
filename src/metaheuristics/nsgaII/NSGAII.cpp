#include "NSGAII.h"
#include "Ranking.h"
#include "CrowdingDistanceComparator.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>


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
    {
        void *p = getInputParameter("eliteMakespanSlots");
        if (p) eliteMakespanSlots = *static_cast<int *>(p);
        p = getInputParameter("makespanBiasProb");
        if (p) makespanBiasProb = *static_cast<double *>(p);
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
        }
        for (int i = 0; i < nExtremes && filled < populationSize; ++i, ++filled) {
            Solution *sol = msProb->createCostExtremeSolution();
            problem_->evaluate(sol);
            population->add(sol);
        }
        std::cout << "[NSGAII] Injected " << (nExtremes * 2) << " extreme seed solutions\n";
    }

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
    }

    evaluations = population->size();

    // ---- タスク1: 世代ログ準備 (MaxShift 診断) ----
    // generation_log.csv に append。4戦略分が同一ファイルに蓄積される。
    // run_id は静的カウンタで管理し、ヘッダーは最初の1回のみ書く。
    static int  sRunId         = 0;
    static bool sHeaderWritten = false;
    ++sRunId;
    const bool msMode = (dynamic_cast<RCPSP_Problem_MaxShift*>(problem_) != nullptr);
    int genCount = 0;
    std::ofstream genLog;
    if (msMode) {
        genLog.open("generation_log.csv",
                    sRunId == 1 ? std::ios::trunc : std::ios::app);
        if (!sHeaderWritten && genLog.is_open()) {
            genLog << "run_id,generation,avg_max_shift,min_makespan_in_front,"
                      "count_all_zero_shift,min_max_shift,max_max_shift\n";
            sHeaderWritten = true;
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

        for (int i = 0; i < populationSize && evaluations < maxEvaluations; i += 2) {
            // ---- B: makespan 最小解を親の一方に固定（確率 makespanBiasProb）----
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
            delete [] parents;

            Solution *c1 = (Solution *)offs[0];
            Solution *c2 = (Solution *)offs[1];
//            delete [] offs;

            mutationOperator->execute(c1);
            mutationOperator->execute(c2);

            problem_->evaluate(c1);
            problem_->evaluate(c2);

            // ここで RCPSP 用の局所探索を追加
            // if (useLocalSearch) {
            //     if (auto rcpsp = dynamic_cast<RCPSP_Problem*>(problem_)) {
            //         // -1 or 0 なら「改善できなくなるまで」
            //         rcpsp->localSearchOnActivityOrder(c1, -1);
            //         rcpsp->localSearchOnActivityOrder(c2, -1);
            //
            //         rcpsp->localSearchOnSchedObj(c1, -1);
            //         rcpsp->localSearchOnSchedObj(c2, -1);
            //     }
            // }



            offspringPopulation->add(new Solution(c1));
            if (offspringPopulation->size() < populationSize)
                offspringPopulation->add(new Solution(c2));
//            else {
//                // 奇数個体数の場合、最後の1個は追加しない
//                delete c2;
//            }
            delete offs[0];
            delete offs[1];
            delete [] offs;

            evaluations += 2;
        }

        // 親 + 子 = unionPopulation
        unionPopulation = new SolutionSet(population->size() + offspringPopulation->size());
        for (int i = 0; i < population->size(); ++i) {
            unionPopulation->add(new Solution(population->get(i)));
        }
        for (int i = 0; i < offspringPopulation->size(); ++i) {
            unionPopulation->add(new Solution(offspringPopulation->get(i)));
        }

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
                front->sort(new CrowdingDistanceComparator());
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
        if (msMode && genLog.is_open()) {
            ++genCount;
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
    }
    setOutputParameter("evaluations", &evaluations);
    return population;
}







