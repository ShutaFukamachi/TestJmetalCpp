#!/bin/bash
# 10/14 ゼミ指摘: 初期解（優先規則＋ランダム性）と探索（部分再構築変異）の A/B を j30 全48インスタンスで回す。
# 使い方: bash tools/ext1014/run_variants.sh <tag> [追加オプション...]
#   例: bash tools/ext1014/run_variants.sh t1014pr --pr-init 0.5
# 出力: results/EXT_COST/FUN_ENC/<prefix>/*_MaxShiftDur_<tag>.txt（公式結果とは別名）
#       results/EXT_COST/TRACE/<prefix>/TRACE_*_<tag>.csv
set -u
TAG=$1; shift
BD="$(cd "$(dirname "$0")/../../cmake-build-release" && pwd)"
cd "$BD"
EXE=${EXE:-./NSGA1014.exe}
mkdir -p logs/t1014
SIZE=${SIZE:-j30}
N=${N:-48}
for x in $(seq ${START:-1} $N); do
  p="${SIZE}${x}_1"
  [ -f "$SIZE.sm/$p.sm" ] || continue
  $EXE "$SIZE.sm/$p.sm" --p1-only --cost-dir=costs_ext --tag "$TAG" --trace "$@" > "logs/t1014/${p}_${TAG}.log" 2>&1
  echo "$p $(date +%H:%M:%S) exit=$?"
done
echo "ALL DONE $TAG"
