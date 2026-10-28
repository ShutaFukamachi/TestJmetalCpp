#!/bin/bash
# キューファイルを5並列で処理する（独立プロセスとして起動し、ツールの時間制限を受けないようにする）
Q=$1; PROG=$2
cd "$(dirname "$0")/../.."
sed -i 's/\r$//' "$Q"
LOGDIR=cmake-build-release/logs/t1020 xargs -P 5 -L 1 env LOGDIR=logs/t1020 bash tools/ext1014/run_one.sh < "$Q" > "$PROG" 2>&1
echo "QUEUE DONE $(date +%m-%d_%H:%M)" >> "$PROG"
