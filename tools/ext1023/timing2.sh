#!/bin/bash
# 計算時間の測り直し（2026-10-24）。10/23 の測定と比べ、同じ手法でも PC の状態で 1.3〜1.4倍ずれたため、
# 手法を「インスタンスごとに交互に」続けて実行し、同じ時間帯の値で比べる。他の計算は止めておく。
# 使い方: bash timing2.sh <出力csv>
cd "$(dirname "$0")/../../cmake-build-release"
OUT=$1
[ -f "$OUT" ] || echo "instance,method,seconds,clock" > "$OUT"
ISL="--pr-init 0.5 --island 20 --regen 0.1 --island-patience 30"
for inst in j30.sm/j301_1.sm j60.sm/j601_1.sm j120.sm/j1201_1.sm; do
  for m in base prbwisrgp prtbw3; do
    case $m in base) o="";; prbwisrgp) o="$ISL --block-win 0.1";; prtbw3) o="$ISL --time-block-win 0.3";; esac
    t0=$(date +%s.%N)
    ./NSGA1023f.exe $inst --p1-only --cost-dir=costs_ext --tag t1024time_$m $o > logs/t1023/time3_$(basename $inst .sm)_$m.log 2>&1
    t1=$(date +%s.%N)
    echo "$(basename $inst .sm),$m,$(python -c "print(round($t1-$t0,1))"),$(date +%H:%M)" >> "$OUT"
    tail -1 "$OUT"
  done
done
echo TIMING2 DONE
