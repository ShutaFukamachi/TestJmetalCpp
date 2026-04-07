#pragma once
#ifndef RCPSP_PROBLEM_H
#define RCPSP_PROBLEM_H

#include "core/Problem.h"
#include "Solution.h"
#include "IntSolutionType.h"
#include "RCPSP_Reader.h"
#include <string>
#include <vector>

class RCPSP_Problem : public Problem {
public:
    // ============================================================
    // [変更] コンストラクタに rr, rv を追加
    //   rr : Resource Range  (0.0 / 0.25 / 0.5 / 0.75)
    //   rv : Resource Vacation (false=なし / true=14日に1日休暇)
    //
    //   デフォルト値 rr=0.0, rv=false のため
    //   既存の呼び出し new RCPSP_Problem(file) はそのまま動く
    // ============================================================
    explicit RCPSP_Problem(const std::string &filename,
                            int    strategy = 4,
                            double rr       = 0.0,
                            bool   rv       = false);

    void evaluate(Solution *solution) override;

    void printInfo() const;

    bool checkTopological(const std::vector<int> &seq) const;
    bool checkTopological(Solution *solution) const;

    Solution* createRandomTopoSolution();

    void localSearchOnActivityOrder(Solution *solution, int maxLSMoves = -1);
    void localSearchOnSchedObj     (Solution *solution, int maxLSMoves = -1);

    void setMaxEvaluations(int me) { maxEvaluations_ = me; }

    const std::vector<std::vector<int>>& getSuccessors() const {
        return instance.successors;
    }

    int getNumJobs()      const { return numberOfJobs_; }
    int getNumResources() const { return instance.nRes; }
    const std::vector<int>&              getDurations() const { return instance.duration; }
    const std::vector<std::vector<int>>& getDemand()   const { return instance.demand; }
    const std::vector<int>&              getCapacity()  const { return instance.capacity; }
    const std::vector<std::vector<int>>& getCapacityT() const { return instance.capacity_t; }

    // 各ジョブの開始時刻を返す（ESS: Earliest Start Schedule）
    std::vector<int> computeStartTimes(Solution *solution) const;

    // 先行制約の推移閉包行列: mat[i][j]==1 ならジョブiはジョブjより前に来なければならない
    std::vector<std::vector<int>> get_precedence_matrix() const;

    // [追加] 時間依存容量テーブルを（再）生成する
    //   コンストラクタが自動で呼ぶが、後から呼び直すことも可能
    void buildTimeVaryingCapacity(double rr, bool rv);

    static void resetGlobalCostSeries();
    static bool writeGlobalCostSeriesCSV(const std::string &filename);

protected:
    RCPSP_Instance instance;

private:
    int strategy_       = 4;
    int evalCounter_    = 0;
    int maxEvaluations_ = 0;
    int numberOfJobs_   = 0;
};

#endif // RCPSP_PROBLEM_H