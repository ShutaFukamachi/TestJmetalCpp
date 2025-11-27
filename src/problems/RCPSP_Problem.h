#ifndef RCPSP_PROBLEM_H
#define RCPSP_PROBLEM_H

#include "Problem.h"
#include "IntSolutionType.h"
#include "RCPSP_Reader.h"
#include <vector>
#include <string>
#include <random>

class RCPSP_Problem : public Problem {
public:

    explicit RCPSP_Problem(const std::string &filename, int strategy = 1);
    ~RCPSP_Problem() override = default;


    void evaluate(Solution *solution) override;


    void printInfo() const;


    void setMaxEvaluations(int maxEval) { maxEvaluations_ = maxEval; }

    // ランダムに実行可能なジョブ順序（topological) を生成する
    vector<int> random_topological_sort(const int seed) const {
        int n = (int)instance.successors.size();
        vector<int> indeg(n, 0);

        // 入次数を計算
        for (int u = 0; u < n; ++u) {
            for (int v : instance.successors[u]) {
                ++indeg[v];
            }
        }

        // 入次数0のノードを収集
        vector<int> zero;
        zero.reserve(n);
        for (int i = 0; i < n; ++i) {
            if (indeg[i] == 0) zero.push_back(i);
        }

        // 乱数エンジン
        static std::mt19937 gen(seed);

        vector<int> order;
        order.reserve(n);

        while (!zero.empty()) {
            // zero からランダムに1つ選ぶ
            uniform_int_distribution<int> dist(0, (int)zero.size() - 1);
            int idx = dist(gen);
            int u = zero[idx];

            // 選んだ要素を末尾と交換してpop_backで削除（O(1)削除）
            zero[idx] = zero.back();
            zero.pop_back();

            order.push_back(u);

            // 後続ノードの入次数を1減らして、0になったらzeroに入れる
            for (int v : instance.successors[u]) {
                if (--indeg[v] == 0) {
                    zero.push_back(v);
                }
            }
        }

        // DAGではない（サイクルあり）の場合
        if ((int)order.size() != n) {
            throw runtime_error("Graph is not a DAG (cycle detected).");
        }

        return order;
    }

    vector<vector<int>> get_precedence_matrix() const {
        int n = (int)instance.successors.size();
        vector<vector<int>> mat(n, vector<int>(n, 0));
        for (int u = 0; u < n; ++u) {
            for (int v : instance.successors[u]) {
                mat[u][v] = 1;
            }
        }
        return mat;
    }

private:

    bool checkTopological(const std::vector<int> &seq) const;
    bool checkTopological(Solution *solution) const;



    int numberOfJobs_{0};
    const RCPSP_Instance instance;


    int strategy_{1};
    int maxEvaluations_{0};
    int evalCounter_{0};
};

#endif
