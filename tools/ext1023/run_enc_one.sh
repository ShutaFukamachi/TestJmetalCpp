#!/bin/bash
# SchedObj（従来法）と MaxShift の P1 比較を1インスタンスで（拡張コスト表）。引数: <size> <prefix>
# 出力: results/EXT_COST/FUN_ENC/<prefix>/SCHED_ENC_<prefix>_<cond>_P1_SchedObj.txt など
SIZE=$1; P=$2
cd "$(dirname "$0")/../../cmake-build-release"
mkdir -p logs/t1023
out="results/EXT_COST/FUN_ENC/$P/SCHED_ENC_${P}_RR050_RV1_P1_SchedObj.txt"
if [ -f "$out" ]; then echo "$(date +%m-%d_%H:%M:%S) $P skip(exists)"; exit 0; fi
./NSGAEncCpp.exe "$SIZE.sm/$P.sm" --p1-only --cost-dir=costs_ext > "logs/t1023/enc_${P}.log" 2>&1
rc=$?
[ -f "$out" ] && st=ok || st=MISSING
echo "$(date +%m-%d_%H:%M:%S) $P enc exit=$rc out=$st"
