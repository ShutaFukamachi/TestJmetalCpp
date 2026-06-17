#pragma once
#ifndef RCPSP_PROBLEM_H
#define RCPSP_PROBLEM_H

#include "core/Problem.h"
#include "Solution.h"
#include "RCPSP_Reader.h"
#include <map>
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

    virtual Solution* createRandomTopoSolution();

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
    void buildTimeVaryingCapacity(double rr, bool rv, uint32_t seed);

    // P1/P2/P3 インスタンス間で同一の capacity_t を共有するためのセッター
    // 外部で生成した capacity_t をそのままセットする（コピー渡し）
    void setCapacityT(const std::vector<std::vector<int>>& cap_t) {
        instance.capacity_t = cap_t;
    }

    // BnB 用: ジョブ j を時刻 t に配置したときのコストを返す
    double computeJobCostAt(int j, int t, int horizon) const;

    static void resetGlobalCostSeries();
    static bool writeGlobalCostSeriesCSV(const std::string &filename);

    // 戦略切り替え・カウンタリセット（4戦略独立実行用）
    virtual void setStrategy(int s) { strategy_ = s; }
    void resetEvalCounter()      { evalCounter_ = 0; }
    void clearStartTimesCache()  { startTimesCache_.clear(); }

    // 出力用再評価: maxShift を固定値に上書きする（-1 で通常のランダム動作に戻す）
    //   0     → ESS（全ジョブ最早時刻）
    //   N > 0 → 全 schedObj=1 ジョブに maxShift=N を固定（決定論的コスト最適化）
    void setOutputMaxShift(int shift) { outputMaxShift_ = shift; }

protected:
    RCPSP_Instance instance;

    int  strategy_           = 4;
    int  evalCounter_        = 0;
    int  maxEvaluations_     = 0;
    int  outputMaxShift_     = -1;   // -1: 通常ランダム, ≥0: 固定値

    // ---- 派生クラス（RCPSP_Problem_Splitting）向けヘルパー ----

    // ジョブ j が時刻 t に 1 単位実行したときのコスト（全資源の合計）
    double computeSlotCost(int j, int t, int horizon) const;

    // 資源 k の時刻 t における容量（時間依存テーブル優先、なければ定数）
    int capacityAtTime(int k, int t) const;

    // P2/P3 派生クラス向け: 基底クラスと同一の RNG を使って maxShift を生成する
    // → P1 と P2/P3 が同じ乱数列を共有することで maxShift の統計的性質を揃える
    std::vector<int> buildMaxShiftForEval(int T) const;

    // MaxShift 派生クラス向け: 先行制約を修復したトポロジカル順序を返す
    static std::vector<int> topoRepair(const std::vector<int> &seq,
                                       const std::vector<std::vector<int>> &successors,
                                       int nJobs);

private:
    int  numberOfJobs_       = 0;

    // evaluate() で確定した実際の開始時刻をキャッシュする
    // computeStartTimes() はこれを優先して返す
    mutable std::map<Solution*, std::vector<int>> startTimesCache_;
};

#endif // RCPSP_PROBLEM_H