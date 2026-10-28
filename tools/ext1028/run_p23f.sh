#!/bin/bash
# 課題3: 最終設定（目標工期つき）で P1/P2/P3 を5問題例について実行する（--p1-only なし、タグ p23f）。1つずつ順に。
cd "$(dirname "$0")/../../cmake-build-release"
DL="--pr-init 0.5 --island 20 --regen 0.1 --island-patience 30 --time-block-win 0.3 --deadline-gene --deadline-mut 0.2"
for P in j309_1 j3011_1 j3021_1 j3031_1 j3042_1; do
  ./NSGA1028.exe j30.sm/$P.sm --cost-dir=costs_ext --tag p23f $DL > logs/t1028/p23f_$P.log 2>&1
  echo "$P rc=$? $(date +%H:%M) files=$(ls results/EXT_COST/FUN_ENC/$P | grep -c p23f)"
done
echo P23F DONE
