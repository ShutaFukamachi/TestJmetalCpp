#!/bin/bash
# 9/10 指摘「P2・P3 をソルバーで解いて比較」の2つ目の問題例（j309_1 と同じ範囲）。他の計算を止めてから実行する（Gurobi は全コアを使う）。
# 出力: results/EXT_COST/FUN_MIP_P2/<p>/, FUN_MIP_P3SLOT/<p>/, FUN_ENC/<p>/*_p23（公式結果とは別）
P=${1:-j3011_1}
cd "$(dirname "$0")/../../cmake-build-release"
L=logs/t1023/p23_$P; mkdir -p $L
echo "start $(date +%H:%M)"
./NSGA1023f.exe j30.sm/$P.sm --cost-dir=costs_ext --tag p23 > $L/nsga.log 2>&1; echo "nsga P1/P2/P3 done $(date +%H:%M) rc=$?"
for rv in 0 1; do
  ./P3Slot.exe j30.sm/$P.sm 0 $rv 10 --cost-dir=costs_ext --time-limit=120 --warm=results/EXT_COST/FUN_MIP/$P/SCHED_MIP_${P}_RR000_RV$rv.txt > $L/p3_rv$rv.log 2>&1
  echo "P3Slot RR000_RV$rv done $(date +%H:%M) rc=$?"
done
./RCPSPMIPCpp.exe j30.sm/$P.sm 0 1 30 --split-mode=P2 --cost-dir=costs_ext --time-limit=120 > $L/p2_rv1.log 2>&1
echo "MIP P2 RR000_RV1 done $(date +%H:%M) rc=$?"
echo "ALL DONE $(date +%H:%M)"
