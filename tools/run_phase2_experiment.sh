#!/usr/bin/env bash
# Phase2 実験ドライバ: baseline x4rep, seed6-full, seed12-full, seed6-reduced
# cmake-build-release から実行: bash ../tools/run_phase2_experiment.sh
set -uo pipefail

INSTANCES="j3034_1 j309_1 j3038_1 j3011_1 j3022_1 j3010_1 j3019_1 j3048_1"
OUT_ROOT="results/FUN_ENC_phase2"
LOG_DIR="../.run48_logs/phase2"
mkdir -p "$LOG_DIR"

run_variant() {
    local tag="$1"; shift
    local extra_args="$*"
    echo ""
    echo "============================================================"
    echo "[VARIANT] $tag  args=[$extra_args]"
    echo "============================================================"
    for p in $INSTANCES; do
        t0=$(date +%s)
        ./NSGAMaxShiftDurCpp.exe "j30.sm/$p.sm" --p1-only $extra_args \
            > "$LOG_DIR/${tag}_${p}.log" 2>&1
        rc=$?
        t1=$(date +%s)
        mkdir -p "$OUT_ROOT/$tag/$p"
        mv -f results/FUN_ENC/$p/*MaxShiftDur*.txt "$OUT_ROOT/$tag/$p/" 2>/dev/null
        echo "  [$tag] $p  exit=$rc  ($((t1-t0))s)"
    done
}

t_all0=$(date +%s)

run_variant baseline_rep1
run_variant baseline_rep2
run_variant baseline_rep3
run_variant baseline_rep4

run_variant seed6_full   --a2seed 6
run_variant seed12_full  --a2seed 12
run_variant seed6_reduced --a2seed 6 --evals 20000

t_all1=$(date +%s)
echo ""
echo "[ALL DONE] total=$((t_all1-t_all0))s"
