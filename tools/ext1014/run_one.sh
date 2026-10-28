#!/bin/bash
# 1インスタンス分の実行（xargs で並列に呼ぶ）。引数: <size> <prefix> <tag> [オプション...]
# 最後に出力ファイル（最後に書かれる RR050_RV1 の SCHED）の有無を確かめて表示する（.miss_memory/046）。
SIZE=$1; P=$2; TAG=$3; shift 3
cd "$(dirname "$0")/../../cmake-build-release"
LOGDIR=${LOGDIR:-logs/t1016}
mkdir -p "$LOGDIR"
./${EXE:-NSGA1014c.exe} "$SIZE.sm/$P.sm" --p1-only --cost-dir=costs_ext --tag "$TAG" --trace "$@" > "$LOGDIR/${P}_${TAG}.log" 2>&1
rc=$?
out="results/EXT_COST/FUN_ENC/$P/SCHED_ENC_${P}_RR050_RV1_MaxShiftDur_${TAG}.txt"
[ -f "$out" ] && st=ok || st=MISSING
echo "$(date +%m-%d_%H:%M:%S) $P $TAG exit=$rc out=$st"
