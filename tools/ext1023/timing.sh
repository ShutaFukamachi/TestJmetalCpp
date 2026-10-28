#!/bin/bash
# 10/21 指摘「計算時間はどのくらい？以前の手法と比べる」: 1インスタンス（6条件・4戦略・各5万評価）の壁時計時間。
# 他の計算を止めた状態で、手法を順番に1つずつ実行する（trace なし）。
# 使い方: bash timing.sh <出力csv> <method>=<opts> ...   例: base= isrgp="--island 20 ..."
cd "$(dirname "$0")/../../cmake-build-release"
OUT=$1; shift
[ -f "$OUT" ] || echo "instance,method,seconds" > "$OUT"
for inst in j30.sm/j301_1.sm j30.sm/j309_1.sm j60.sm/j601_1.sm j90.sm/j901_1.sm j120.sm/j1201_1.sm; do
  for mo in "$@"; do
    m=${mo%%=*}; o=${mo#*=}
    t0=$(date +%s.%N)
    ./${EXE:-NSGA1023.exe} $inst --p1-only --cost-dir=costs_ext --tag t1023time_$m $o > logs/t1023/time_$(basename $inst .sm)_$m.log 2>&1
    t1=$(date +%s.%N)
    echo "$(basename $inst .sm),$m,$(python -c "print(round($t1-$t0,1))")" >> "$OUT"
    tail -1 "$OUT"
  done
done
echo TIMING DONE
