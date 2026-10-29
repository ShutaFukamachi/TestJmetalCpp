#!/bin/bash
# 計算時間（2026-10-29）: 2集団（dl、工期島あり）と1集団（dl1pm、集団の中の工期島）を、インスタンスごとに交互に2回ずつ実行して測る。他の計算は止めておく。
cd "$(dirname "$0")/../../cmake-build-release"
OUT=analysis/ext1028/timing_1029.csv
[ -f "$OUT" ] || echo "instance,method,rep,seconds,clock" > "$OUT"
C="--pr-init 0.5 --regen 0.1 --time-block-win 0.3 --deadline-gene --deadline-mut 0.2"
for inst in j30.sm/j301_1.sm j60.sm/j601_1.sm j120.sm/j1201_1.sm; do
  for rep in 1 2; do
    for m in dl dl1pm; do
      case $m in dl) o="$C --island 20 --island-patience 30";; dl1pm) o="$C --ms-mate 0.17 --ms-mate-filter";; esac
      t0=$(date +%s.%N)
      ./NSGA1028.exe $inst --p1-only --cost-dir=costs_ext --tag t1029time_${m}_r$rep $o > logs/t1028/time_$(basename $inst .sm)_${m}_r$rep.log 2>&1
      t1=$(date +%s.%N)
      echo "$(basename $inst .sm),$m,$rep,$(python -c "print(round($t1-$t0,1))"),$(date +%H:%M)" >> "$OUT"; tail -1 "$OUT"
    done
  done
done
echo TIMING4 DONE
