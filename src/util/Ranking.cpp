//  Ranking.cpp
//
//  Author:
//       Antonio J. Nebro <antonio@lcc.uma.es>
//       Juan J. Durillo <durillo@lcc.uma.es>
//       Esteban López-Camacho <esteban@lcc.uma.es>
//
//  Copyright (c) 2011 Antonio J. Nebro, Juan J. Durillo
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.


#include <Ranking.h>
#include <cmath>
#include <algorithm>
#include "CrowdingDistanceComparator.h"

/**
 * This class implements some facilities for ranking solutions.
 * Given a <code>SolutionSet</code> object, their solutions are ranked
 * according to scheme proposed in NSGA-II; as a result, a set of subsets
 * are obtained. The subsets are numbered starting from 0 (in NSGA-II, the
 * numbering starts from 1); thus, subset 0 contains the non-dominated
 * solutions, subset 1 contains the non-dominated solutions after removing those
 * belonging to subset 0, and so on.
 */


/**
 * Constructor.
 * @param solutionSet The <code>SolutionSet</code> to be ranked.
 */
Ranking::Ranking (SolutionSet * solutionSet) {

  solutionSet_ = solutionSet;
  dominance_   = new DominanceComparator();
  constraint_  = new OverallConstraintViolationComparator();

  // dominateMe[i] contains the number of solutions dominating i        
  int * dominateMe = new int[solutionSet_->size()];
    
  if (dominateMe == NULL) {
    cout << "Fatal Problem: Cannot reserve memory in class Ranking" << endl;
    exit(-1);
  }

  // iDominate[k] contains the list of solutions dominated by k
  vector<int> * iDominate = new vector<int>[solutionSet_->size()];

  // front[i] contains the list of individuals belonging to the front i
  vector<int> * front = new vector<int>[solutionSet_->size()+1];
        
  // flagDominate is an auxiliar variable
  int flagDominate;    
        
  //-> Fast non dominated sorting algorithm
  for (int p = 0; p < solutionSet_->size(); p++) {
    dominateMe[p] = 0;
  }

  // For all q individuals , calculate if p dominates q or vice versa
  for (int p = 0; p < (solutionSet_->size() - 1); p++) {
    for (int q = p + 1; q < solutionSet_->size(); q++) {

      flagDominate =constraint_->compare(solutionSet->get(p),solutionSet->get(q));
      if (flagDominate == 0) {
        flagDominate =dominance_->compare(solutionSet->get(p),solutionSet->get(q));
      } // if

      if (flagDominate == -1) {
        iDominate[p].push_back(q);
        dominateMe[q]++;
      } else if (flagDominate == 1) {
        iDominate[q].push_back(p);
        dominateMe[p]++;
      } // if

    } // for
  } // for

  for (int p = 0; p < solutionSet_->size(); p++) {
    // If nobody dominates p, p belongs to the first front
    if (dominateMe[p] == 0) {
      front[0].push_back(p);
      solutionSet_->get(p)->setRank(0);
    } // if

  } // for

  // Obtain the rest of fronts
  int i = 0;
  vector<int>::iterator it1, it2;
  while (front[i].size()!=0) {
    i++;
    for (it1=front[i-1].begin(); it1<front[i-1].end(); it1++) {
      for (it2=iDominate[*it1].begin(); it2 < iDominate[*it1].end();it2++) {
        dominateMe[*it2]--;
        if (dominateMe[*it2]==0) {
          front[i].push_back(*it2);
          solutionSet->get(*it2)->setRank(i);
        } // if
      } // for
    } // for
  } // while
        
  ranking_ = new SolutionSet*[i];
  if (ranking_ == NULL) {
    cout << "Fatal Error: Impossible to reserve memory in Ranking" << endl;
    exit(-1);
  }
    
  //0,1,2,....,i-1 are front, then i fronts
    
  numberOfSubfronts_ = i;
    
  for (int j = 0; j < i; j++) {
    ranking_[j] = new SolutionSet(front[j].size());
    for (it1=front[j].begin(); it1<front[j].end(); it1++) {
      ranking_[j]->add(new Solution(solutionSet_->get(*it1)));
    } // for
  } // for

  delete [] dominateMe;
  delete [] iDominate;
  delete [] front;

} // Ranking


/**
 * Destructor
 */
Ranking::~Ranking() {
  for (int i = 0; i < numberOfSubfronts_; i++) {
    delete ranking_[i];
  }
  delete [] ranking_;
  delete dominance_;
  delete constraint_;
} // ~Ranking


/**
 * Returns a <code>SolutionSet</code> containing the solutions of a given rank.
 * @param rank The rank
 * @return Object representing the <code>SolutionSet</code>.
 */
SolutionSet * Ranking::getSubfront(int rank) {
  return ranking_[rank];
} // getSubFront


/**
 * Returns the total number of subFronts founds.
 */
int Ranking::getNumberOfSubfronts() {
  return numberOfSubfronts_;
} // getNumberOfSubfronts

/**
 * Crowding distance assignment for a set of solutions.
 * Calculates the crowding distance of each solution in the front.
 */
void Ranking::crowdingDistanceAssignment(SolutionSet *front, int numberOfObjectives) {
  int size = front->size();
  if (size == 0) return;
  if (size == 1) {
    front->get(0)->setCrowdingDistance(1e9);
    return;
  }
  if (size == 2) {
    front->get(0)->setCrowdingDistance(1e9);
    front->get(1)->setCrowdingDistance(1e9);
    return;
  }

  // 初期化
  for (int i = 0; i < size; i++) {
    front->get(i)->setCrowdingDistance(0.0);
  }

  // 各目的ごとに計算
  for (int m = 0; m < numberOfObjectives; m++) {

    // インデックス配列を目的mでソートする
    std::vector<int> idx(size);
    for (int i = 0; i < size; i++) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b){
        return front->get(a)->getObjective(m) < front->get(b)->getObjective(m);
    });

    double fmin = front->get(idx.front())->getObjective(m);
    double fmax = front->get(idx.back())->getObjective(m);
    if (fmax - fmin < 1e-12) {
      // ほぼ同じ値なら距離に寄与しない
      continue;
    }

    // 端の2つは∞扱い
    front->get(idx.front())->setCrowdingDistance(1e9);
    front->get(idx.back())->setCrowdingDistance(1e9);

    // 中間の個体のcrowding distanceを加算
    for (int k = 1; k < size-1; k++) {
      double prevF = front->get(idx[k-1])->getObjective(m);
      double nextF = front->get(idx[k+1])->getObjective(m);
      double dist  = (nextF - prevF) / (fmax - fmin);

      double oldCD = front->get(idx[k])->getCrowdingDistance();
      front->get(idx[k])->setCrowdingDistance(oldCD + dist);
    }
  }
}
