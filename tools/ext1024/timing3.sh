#!/bin/bash
# 計算時間（2026-10-25）: 新しい最終設定（prtbw3）と ＋目標工期（dl）を、インスタンスごとに交互に実行して測る。他の計算は止めておく。
cd "$(dirname "$0")/../../cmake-build-release"
OUT=analysis/ext1014/timing_1025.csv
[ -f "$OUT" ] || echo "instance,method,seconds,clock" > "$OUT"
B="--pr-init 0.5 --island 20 --regen 0.1 --island-patience 30 --time-block-win 0.3"
for inst in j30.sm/j301_1.sm j60.sm/j601_1.sm j120.sm/j1201_1.sm; do
  for m in base prtbw3 dl; do
    case $m in base) o="";; prtbw3) o="$B";; dl) o="$B --deadline-gene --deadline-mut 0.2";; esac
    t0=$(date +%s.%N)
    ./NSGA1024b.exe $inst --p1-only --cost-dir=costs_ext --tag t1025time_$m $o > logs/t1024/time_$(basename $inst .sm)_$m.log 2>&1
    t1=$(date +%s.%N)
    echo "$(basename $inst .sm),$m,$(python -c "print(round($t1-$t0,1))"),$(date +%H:%M)" >> "$OUT"; tail -1 "$OUT"
  done
done
echo TIMING3 DONE
