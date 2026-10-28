#!/bin/bash
# 課題3: P3 の MIP を制限時間 600秒/点で解き直す（120秒の結果は上書きしない: --tag=tl600）。引数: <prefix> <rv>。Gurobi は単独で。
P=$1; RV=$2
cd "$(dirname "$0")/../../cmake-build-release"
./P3Slot.exe j30.sm/$P.sm 0 $RV 10 --cost-dir=costs_ext --time-limit=600 --tag=tl600 \
  --warm=results/EXT_COST/FUN_MIP/$P/SCHED_MIP_${P}_RR000_RV$RV.txt > logs/t1028/p3long_${P}_RV$RV.log 2>&1
echo "P3 long $P RV$RV rc=$? $(date +%H:%M)"; ls results/EXT_COST/FUN_MIP_P3SLOT/$P/ | grep tl600
