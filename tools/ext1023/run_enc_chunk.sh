#!/bin/bash
# SchedObj 比較のかたまり（4並列）。使い方: bash run_enc_chunk.sh <queue>
cd "$(dirname "$0")/../.."
P=cmake-build-release/logs/t1023/_progress_$(basename "$1" .txt).txt
xargs -P ${PAR:-4} -L 1 bash tools/ext1023/run_enc_one.sh < "$1" > "$P" 2>&1
echo "CHUNK DONE $(date +%m-%d_%H:%M)" >> "$P"
