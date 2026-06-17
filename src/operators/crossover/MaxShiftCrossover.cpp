#include "MaxShiftCrossover.h"
#include <algorithm>
#include <random>
#include <vector>

// ============================================================
//  MaxShiftCrossover::execute
//
//  Hartmann (1998) の 2 点交叉
//
//  活動リスト（vars[0..n-1]）:
//    k1, k2 を乱数で決め 3 ブロックに分割
//    子1: [Mother の前ブロック] + [Father の未使用 (Father 順)] + [Mother の未使用 (Mother 順)]
//    子2: 親の役割を入れ替えて同様
//
//  max_shift リスト（vars[n..2n-1]）:
//    各ジョブを提供した親の max_shift[job] を継承する。
//    ジョブ単位で格納されているため、vars[n + job_id] で参照する。
// ============================================================
void * MaxShiftCrossover::execute(void * object) {
    void ** parents = (void **)object;
    Solution *p1 = (Solution *)parents[0];  // Mother
    Solution *p2 = (Solution *)parents[1];  // Father

    const int nVars = p1->getNumberOfVariables();
    const int nJobs = nVars / 2;   // 前半: 活動リスト, 後半: max_shift

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution<> prob01(0.0, 1.0);

    // 交叉しない場合は複製して返す
    if (prob01(gen) > probability) {
        Solution **offs = new Solution*[2];
        offs[0] = new Solution(p1);
        offs[1] = new Solution(p2);
        return (void*)offs;
    }

    const auto &v1 = p1->getVars();
    const auto &v2 = p2->getVars();

    // 活動リストと max_shift を配列に展開
    std::vector<int> mother(nJobs), father(nJobs);
    std::vector<int> mShift(nJobs), fShift(nJobs);

    for (int i = 0; i < nJobs; ++i) {
        mother[i] = v1[i];
        father[i] = v2[i];
    }
    for (int j = 0; j < nJobs; ++j) {
        mShift[j] = v1[nJobs + j];
        fShift[j] = v2[nJobs + j];
    }

    // 交叉点を 2 つ決める（範囲: 1..nJobs-1）
    std::uniform_int_distribution<> dis(1, nJobs - 1);
    int k1 = dis(gen), k2 = dis(gen);
    while (k1 == k2) k2 = dis(gen);
    if (k1 > k2) std::swap(k1, k2);
    // ブロック: [0, k1), [k1, k2), [k2, nJobs)

    // make_child: 3 ブロックの親からそれぞれ子を生成
    //   活動リスト: 前ブロック = front_p のまま
    //              中ブロック = mid_p の未使用ジョブ（mid_p の順で）
    //              後ブロック = rear_p の未使用ジョブ（rear_p の順で）
    //   max_shift:  ジョブを提供した親の max_shift[job] を継承
    auto make_child = [&](
        const std::vector<int>& front_p, const std::vector<int>& front_ms,
        const std::vector<int>& mid_p,   const std::vector<int>& mid_ms,
        const std::vector<int>& rear_p,  const std::vector<int>& rear_ms)
    {
        std::vector<int> child_al(nJobs, -1);
        std::vector<int> child_ms(nJobs, 0);
        std::vector<bool> used(nJobs, false);

        // 前ブロック: front_p の [0, k1) をそのまま
        for (int i = 0; i < k1; ++i) {
            int job = front_p[i];
            child_al[i]    = job;
            child_ms[job]  = front_ms[job];
            used[job]      = true;
        }

        // 中ブロック: mid_p の未使用ジョブを順に (k2-k1) 個
        int pos = k1;
        for (int i = 0; i < nJobs && pos < k2; ++i) {
            int job = mid_p[i];
            if (!used[job]) {
                child_al[pos] = job;
                child_ms[job] = mid_ms[job];
                used[job]     = true;
                ++pos;
            }
        }

        // 後ブロック: rear_p の未使用ジョブを順に (nJobs-k2) 個
        pos = k2;
        for (int i = 0; i < nJobs && pos < nJobs; ++i) {
            int job = rear_p[i];
            if (!used[job]) {
                child_al[pos] = job;
                child_ms[job] = rear_ms[job];
                used[job]     = true;
                ++pos;
            }
        }

        return std::make_pair(child_al, child_ms);
    };

    // 子1 (Daughter): 前=Mother, 中=Father, 後=Mother
    auto [alA, msA] = make_child(mother, mShift, father, fShift, mother, mShift);
    // 子2 (Son):      前=Father, 中=Mother, 後=Father
    auto [alB, msB] = make_child(father, fShift, mother, mShift, father, fShift);

    Solution **offspring = new Solution*[2];
    offspring[0] = new Solution(p1);
    offspring[1] = new Solution(p2);

    for (int i = 0; i < nJobs; ++i) {
        offspring[0]->getVars()[i] = alA[i];
        offspring[1]->getVars()[i] = alB[i];
    }
    for (int j = 0; j < nJobs; ++j) {
        offspring[0]->getVars()[nJobs + j] = msA[j];
        offspring[1]->getVars()[nJobs + j] = msB[j];
    }

    return (void*)offspring;
}
