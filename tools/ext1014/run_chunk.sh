#!/bin/bash
# 使い方: [EXE=..] [PAR=4] [LOGD=logs/t1023] bash tools/ext1014/run_chunk.sh <chunkfile>
# 2時間未満のかたまりを PAR 並列（既定4。CPU に余裕を残す）で処理する。
cd "$(dirname "$0")/../.."
LOGD=${LOGD:-logs/t1020}
mkdir -p "cmake-build-release/$LOGD"
P=cmake-build-release/$LOGD/_progress_$(basename "$1" .txt).txt
xargs -P ${PAR:-4} -L 1 env LOGDIR=$LOGD bash tools/ext1014/run_one.sh < "$1" > "$P" 2>&1
echo "CHUNK DONE $(date +%m-%d_%H:%M)" >> "$P"
