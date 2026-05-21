#include "PermutationCrossover.h"
#include <algorithm>
#include <random>
#include <vector>

// ============================================================
// Hartmann (1998) の 2点交叉
//
// 活動リスト部分:
//   k1, k2 を乱数で決め 3 ブロックに分割
//   子1: [Mother の前ブロック] + [Father の未使用を Father の順で] + [Mother の未使用を Mother の順で]
//   子2: 親の役割を入れ替えて同様
//
// schedObj 部分:
//   各ジョブを「どちらの親から貢献されたか」に応じて、
//   そのジョブに対応する親の schedObj を継承する
//   （schedObj は vars[nJobs + job_id] でジョブ ID 単位に格納）
// ============================================================

void * PermutationCrossover::execute(void * object) {
    void ** parents = (void **)object;
    Solution * p1 = (Solution *)parents[0];  // Mother
    Solution * p2 = (Solution *)parents[1];  // Father

    const int nVars = p1->getNumberOfVariables();
    const int nJobs = nVars / 2;  // 前半: 活動リスト, 後半: schedObj

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution<> prob01(0.0, 1.0);

    // 交叉しない場合は複製して返す
    if (prob01(gen) > probability) {
        Solution **offs = new Solution*[2];
        offs[0] = new Solution(p1);
        offs[1] = new Solution(p2);
        return (void*)offs;
    }

    Variable **v1 = p1->getDecisionVariables();
    Variable **v2 = p2->getDecisionVariables();

    // 活動リストと schedObj を配列に展開
    std::vector<int> mother(nJobs), father(nJobs);
    std::vector<int> mSched(nJobs), fSched(nJobs);
    for (int i = 0; i < nJobs; ++i) {
        mother[i] = (int)v1[i]->getValue();
        father[i] = (int)v2[i]->getValue();
    }
    for (int j = 0; j < nJobs; ++j) {
        mSched[j] = (int)v1[nJobs + j]->getValue();  // job j の schedObj (Mother)
        fSched[j] = (int)v2[nJobs + j]->getValue();  // job j の schedObj (Father)
    }

    // 交叉点を 2 つ決める（ダミー端点を除いた範囲: 1..nJobs-1）
    std::uniform_int_distribution<> dis(1, nJobs - 1);
    int k1 = dis(gen), k2 = dis(gen);
    while (k1 == k2) k2 = dis(gen);
    if (k1 > k2) std::swap(k1, k2);
    // ブロック: [0, k1), [k1, k2), [k2, nJobs)

    // make_child: (前ブロック親, 中ブロック親, 後ブロック親) から子を生成
    //   活動リスト: 前ブロックを front_p からそのままコピー
    //              中ブロックを mid_p の未使用ジョブで埋める
    //              後ブロックを rear_p の未使用ジョブで埋める
    //   schedObj:  ジョブを提供した親の schedObj を継承
    //
    //   返り値: (活動リスト, schedObj) ペア
    auto make_child = [&](
        const std::vector<int>& front_p, const std::vector<int>& front_s,
        const std::vector<int>& mid_p,   const std::vector<int>& mid_s,
        const std::vector<int>& rear_p,  const std::vector<int>& rear_s)
    {
        std::vector<int> child_al(nJobs, -1);
        std::vector<int> child_so(nJobs, 0);
        std::vector<bool> used(nJobs, false);

        // ---- 前ブロック: front_p の [0, k1) をそのままコピー ----
        for (int i = 0; i < k1; ++i) {
            int job = front_p[i];
            child_al[i] = job;
            child_so[job] = front_s[job];  // job の schedObj は front 親から継承
            used[job] = true;
        }

        // ---- 中ブロック: mid_p の未使用ジョブを順に (k2-k1) 個 ----
        int pos = k1;
        for (int i = 0; i < nJobs && pos < k2; ++i) {
            int job = mid_p[i];
            if (!used[job]) {
                child_al[pos] = job;
                child_so[job] = mid_s[job];  // job の schedObj は mid 親から継承
                used[job] = true;
                ++pos;
            }
        }

        // ---- 後ブロック: rear_p の未使用ジョブを順に (nJobs-k2) 個 ----
        pos = k2;
        for (int i = 0; i < nJobs && pos < nJobs; ++i) {
            int job = rear_p[i];
            if (!used[job]) {
                child_al[pos] = job;
                child_so[job] = rear_s[job];  // job の schedObj は rear 親から継承
                used[job] = true;
                ++pos;
            }
        }

        return std::make_pair(child_al, child_so);
    };

    // 子1 (Daughter): 前=Mother, 中=Father, 後=Mother
    auto [alA, soA] = make_child(mother, mSched, father, fSched, mother, mSched);
    // 子2 (Son):      前=Father, 中=Mother, 後=Father
    auto [alB, soB] = make_child(father, fSched, mother, mSched, father, fSched);

    Solution **offspring = new Solution*[2];
    offspring[0] = new Solution(p1);
    offspring[1] = new Solution(p2);

    for (int i = 0; i < nJobs; ++i) {
        offspring[0]->getDecisionVariables()[i]->setValue(alA[i]);
        offspring[1]->getDecisionVariables()[i]->setValue(alB[i]);
    }
    for (int j = 0; j < nJobs; ++j) {
        offspring[0]->getDecisionVariables()[nJobs + j]->setValue(soA[j]);
        offspring[1]->getDecisionVariables()[nJobs + j]->setValue(soB[j]);
    }

    return (void*)offspring;
}
