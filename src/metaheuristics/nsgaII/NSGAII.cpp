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
    {
        void *p = getInputParameter("eliteMakespanSlots");
        if (p) eliteMakespanSlots = *static_cast<int *>(p);
        p = getInputParameter("makespanBiasProb");
        if (p) makespanBiasProb = *static_cast<double *>(p);
        p = getInputParameter("noveltyFilter");
        if (p) noveltyFilter = (*static_cast<int *>(p) != 0);
        p = getInputParameter("a1PrioritySeed");
        if (p) a1PrioritySeed = (*static_cast<int *>(p) != 0);
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
                ++filled;
                ++injected;
            }
        }
        std::cout << "[NSGAII] A1 Priority-Rule Seed: injected "
                  << injected << " (LFT/MTS/GRPW), best ms=" << bestA1ms << "\n";
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
    static thread_local int  sRunId         = 0;
    static thread_local bool sHeaderWritten = false;
    ++sRunId;
    const bool msMode = (dynamic_cast<RCPSP_Problem_MaxShift*>(problem_) != nullptr);
    int genCount = 0;
    std::ofstream genLog;
    if (msMode) {
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
    {
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

                mutationOperator->execute(c1);
                mutationOperator->execute(c2);

                problem_->evaluate(c1);
                problem_->evaluate(c2);

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
    setOutputParameter("evaluations", &evaluations);
    return population;
}







