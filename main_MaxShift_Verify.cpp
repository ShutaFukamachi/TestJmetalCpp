// ============================================================
//  main_MaxShift_Verify.cpp  —  提案手法(NSGA)の前線が制約を満たすかの検証
//
//  目的: 「MIP の makespan が大きすぎる／提案手法が制約違反していないか」への回答。
//        提案手法(MaxShift P1 / MaxShiftDur)を実際に走らせ、最終前線の
//        **全解**を capacity_t に対して feasibility 検証する:
//          - 先行制約違反 / 資源制約違反 / 休暇日(cap=0)貫通
//        「報告 ms*（違反解も含む最小値）」と「実行可能のみ ms*」を比較し、
//        022 の serial フォールバック phantom（est より前に配置＝違反付きで
//        makespan が不当に小さい）が高 RR/RV で混入していないか判定する。
//
//  使い方: MSVERIFY [instance] [rr] [rv] [trials] [evals/strategy]
//     例:  MSVERIFY j30.sm/j309_1.sm 0.50 1 3 50000
// ============================================================
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "core/Algorithm.h"
#include "core/SolutionSet.h"
#include "Solution.h"
#include "metaheuristics/nsgaII/NSGAII.h"
#include "problems/RCPSP_Problem_MaxShift.h"
#include "problems/RCPSP_Problem_MaxShiftDur.h"
#include "operators/crossover/MaxShiftCrossover.h"
#include "operators/mutation/MaxShiftMutation.h"
#include "operators/mutation/MaxShiftDurMutation.h"
#include "operators/selection/BinaryTournament2.h"
#include "util/Ranking.h"

using namespace std;

struct FrontPoint { double ms, cost; vector<int> start; };

// capacity_t に対する feasibility 検証（ダミー端点 d<=0 は無視）
static void checkFeasibility(RCPSP_Problem_MaxShift *prob, const vector<int> &start,
                             int &precViol, int &resViol, int &vacPierce)
{
    const int n=prob->getNumJobs(), nRes=prob->getNumResources(), T=prob->getHorizon();
    const auto &dur=prob->getDurations(); const auto &dem=prob->getDemand();
    const auto &cap=prob->getCapacity();  const auto &capT=prob->getCapacityT();
    const auto &succ=prob->getSuccessors();
    auto capAt=[&](int k,int t)->int{
        if(!capT.empty()&&!capT[k].empty()&&t<(int)capT[k].size()) return capT[k][t];
        return cap[k]; };
    precViol=resViol=vacPierce=0;
    if((int)start.size()<n){ precViol=-1; return; }
    for(int i=0;i<n;++i){ if(dur[i]<=0) continue;
        for(int s:succ[i]){ if(s<0||s>=n||dur[s]<=0) continue;
            if(start[i]+dur[i]>start[s]) ++precViol; } }
    vector<vector<int>> usage(nRes, vector<int>(T,0));
    for(int j=0;j<n;++j){ if(dur[j]<=0) continue;
        bool pierced=false;
        for(int t=start[j]; t<start[j]+dur[j] && t<T; ++t)
            for(int k=0;k<nRes;++k){ usage[k][t]+=dem[j][k];
                if(dem[j][k]>0 && capAt(k,t)==0) pierced=true; }
        if(pierced) ++vacPierce; }
    for(int t=0;t<T;++t) for(int k=0;k<nRes;++k)
        if(usage[k][t]>capAt(k,t)) ++resViol;
}

enum Enc { MAXSHIFT, MAXSHIFTDUR };

static void runOnce(Enc enc, const string &inst, double rr, bool rv,
                    int popSize, int evals, int numStr,
                    vector<FrontPoint> &collector, RCPSP_Problem_MaxShift *&checker)
{
    using SR = pair<SolutionSet*, RCPSP_Problem_MaxShift*>;
    vector<future<SR>> futs;
    for(int s=1;s<=numStr;++s){
        futs.push_back(async(launch::async,[=]()->SR{
            RCPSP_Problem_MaxShift *prob;
            Operator *mut;
            if(enc==MAXSHIFT){ auto p=new RCPSP_Problem_MaxShift(inst,s,rr,rv);
                prob=p; mut=new MaxShiftMutation(1.0/(double)p->getNumberOfVariables(), p); }
            else { auto p=new RCPSP_Problem_MaxShiftDur(inst,s,rr,rv);
                prob=p; mut=new MaxShiftDurMutation(1.0/(double)p->getNumberOfVariables(), p); }
            prob->resetEvalCounter();

            Algorithm *algo=new NSGAII(prob);
            int popSz=popSize, maxEv=evals, ls=0;
            algo->setInputParameter("populationSize",&popSz);
            algo->setInputParameter("maxEvaluations",&maxEv);
            algo->setInputParameter("useLocalSearch",&ls);
            algo->addOperator("crossover", new MaxShiftCrossover(0.9));
            algo->addOperator("mutation",  mut);
            map<string,void*> sel; algo->addOperator("selection", new BinaryTournament2(sel));

            SolutionSet *pop=algo->execute();
            SolutionSet *res=new SolutionSet(popSize*4);
            Ranking rk(pop);
            if(rk.getNumberOfSubfronts()>0){ SolutionSet *f0=rk.getSubfront(0);
                for(int i=0;i<f0->size();++i) res->add(new Solution(f0->get(i))); }
            delete pop; delete algo;
            return {res, prob};
        }));
    }
    SolutionSet *comb=new SolutionSet(numStr*popSize*4);
    vector<RCPSP_Problem_MaxShift*> probs;
    for(int s=0;s<numStr;++s){ auto [r,p]=futs[s].get();
        for(int i=0;i<r->size();++i) comb->add(new Solution(r->get(i)));
        delete r; probs.push_back(p); }
    Ranking fr(comb);
    if(fr.getNumberOfSubfronts()>0){ SolutionSet *f0=fr.getSubfront(0);
        for(int i=0;i<f0->size();++i){ double ms=f0->get(i)->getObjective(0), c=f0->get(i)->getObjective(1);
            if(ms>=1e8||c>=1e8) continue;
            collector.push_back({ms,c,f0->get(i)->startTimes_}); } }
    if(checker==nullptr && !probs.empty()){ checker=probs[0]; probs[0]=nullptr; }
    delete comb; for(auto*p:probs) delete p;
}

int main(int argc,char**argv){
    string inst=(argc>=2)?argv[1]:"j30.sm/j309_1.sm";
    double rr =(argc>=3)?atof(argv[2]):0.50;
    bool   rv =(argc>=4)?(atoi(argv[3])!=0):true;
    int trials=(argc>=5)?atoi(argv[4]):3;
    int evals =(argc>=6)?atoi(argv[5]):50000;
    const int popSize=100, numStr=4;

    cout<<"=== Proposed-method (NSGA) feasibility verification ===\n";
    cout<<"instance="<<inst<<"  rr="<<rr<<"  rv="<<rv
        <<"  trials="<<trials<<"  evals/strategy="<<evals<<"\n\n";

    struct E{ Enc e; const char*name; };
    vector<E> encs={ {MAXSHIFT,"MaxShift (P1)"}, {MAXSHIFTDUR,"MaxShiftDur"} };

    cout<<left<<setw(16)<<"encoding"<<right<<setw(8)<<"front"
        <<setw(12)<<"report ms*"<<setw(12)<<"feas ms*"
        <<setw(10)<<"infeas"<<setw(10)<<"maxPrec"<<setw(9)<<"maxRes"<<setw(9)<<"vacPrc"<<"\n";
    cout<<string(86,'-')<<"\n";

    for(auto &E:encs){
        vector<FrontPoint> all; RCPSP_Problem_MaxShift *checker=nullptr;
        for(int t=0;t<trials;++t) runOnce(E.e,inst,rr,rv,popSize,evals,numStr,all,checker);

        double reportMs=1e18, feasMs=1e18;
        int infeas=0, maxPrec=0, maxRes=0, maxVac=0;
        for(auto &p:all){
            reportMs=min(reportMs,p.ms);
            int pv,rvio,vac; checkFeasibility(checker,p.start,pv,rvio,vac);
            if(pv==0&&rvio==0){ feasMs=min(feasMs,p.ms); }
            else { ++infeas; maxPrec=max(maxPrec,pv); maxRes=max(maxRes,rvio); }
            maxVac=max(maxVac,vac);
        }
        cout<<left<<setw(16)<<E.name<<right<<setw(8)<<(int)all.size()
            <<setw(12)<<(int)reportMs<<setw(12)<<(feasMs>1e17?-1:(int)feasMs)
            <<setw(10)<<infeas<<setw(10)<<maxPrec<<setw(9)<<maxRes<<setw(9)<<maxVac<<"\n";
        delete checker;
    }
    cout<<string(86,'-')<<"\n";
    cout<<"report ms* = min makespan over ALL front solutions (incl. constraint-violating phantoms)\n";
    cout<<"feas ms*   = min makespan over FEASIBLE-only solutions (the honest value)\n";
    cout<<"infeas>0 or feas>report => proposed method has phantom (constraint-violating) solutions\n";
    cout<<"(compare with MIP optimum from MIPVERIFY)\n";
    return 0;
}
