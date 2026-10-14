#!/usr/bin/env bash
# ============================================================
#  run_48_comparison_ext.sh
#
#  run_48_comparison.sh（公式・変更禁止）の拡張コスト表版。
#  j30X_1 (X=1..48) を RR<=0.50 の6条件・P1のみで
#  MIP / NSGAEncCpp(SchedObj+MaxShift) / NSGAMaxShiftDurCpp(MaxShiftDur) を、
#  正本 costs/ の代わりに costs_ext/（.miss_memory/041 参照）で実行する。
#
#  公式版との違い（これだけ）:
#    - 3プログラムすべてに --cost-dir=costs_ext を付与
#    - 完了判定の参照先を results/EXT_COST/FUN_MIP, results/EXT_COST/FUN_ENC に変更
#    - NSGAEncCpp/NSGAMaxShiftDurCpp とも --cost-dir=costs_ext 指定時は
#      results/EXT_COST/FUN_ENC/<prefix>/ に直接書き込む実装になったため mv 行は削除
#    - ログ出力先を ../.run48_ext_logs/ に変更（公式ログと混在させない）
#    - インスタンス×プログラムごとの所要秒数を ../.run48_ext_logs/timing.csv に追記
#
#  実行: cmake-build-release から
#        bash ../tools/run_48_comparison_ext.sh [START] [END]
#        例: bash ../tools/run_48_comparison_ext.sh 9 9   # j309_1 のみ（試運転）
#            bash ../tools/run_48_comparison_ext.sh        # 全48件（本タスクでは未実行）
#
#  絶対に守ること（.miss_memory/041 のタスク指示）:
#    - costs/（正本）、results/FUN_MIP/、results/FUN_ENC/ には一切書き込まない
#    - tools/run_48_comparison.sh は変更しない
# ============================================================
set -uo pipefail

START="${1:-1}"
END="${2:-48}"
CONDS="RR000_RV0 RR000_RV1 RR025_RV0 RR025_RV1 RR050_RV0 RR050_RV1"
CHUNK=3
COST_DIR="costs_ext"

LOG_DIR="../.run48_ext_logs"
mkdir -p "$LOG_DIR"
TIMING_CSV="$LOG_DIR/timing.csv"
if [ ! -f "$TIMING_CSV" ]; then
    echo "instance,program,seconds" > "$TIMING_CSV"
fi
SUSPECT_CSV="$LOG_DIR/suspect.csv"
if [ ! -f "$SUSPECT_CSV" ]; then
    echo "instance,condition,makespan,solve_seconds" > "$SUSPECT_CSV"
fi
# TimeLimit(120s) x 1.5 = 180s。壁時計のTimeLimitはスリープ等の中断で
# 見かけ上の経過時間が本来の求解時間より長くなりうる（.miss_memory/042 参照）。
SUSPECT_THRESHOLD=180

# ---- MIPのGAP_*.csvをsolve_seconds>閾値で走査し、疑わしい行をsuspect.csvに記録する
#      （処理は止めない。--cost-dir=costs以外で本スクリプト専用に使う想定）。
check_mip_suspect() {
    local p="$1"
    local found=0
    for c in $CONDS; do
        local gapfile="results/EXT_COST/FUN_MIP/$p/GAP_MIP_${p}_${c}.csv"
        [ -f "$gapfile" ] || continue
        while IFS=',' read -r ms cost gap optimal secs rest; do
            case "$ms" in ''|makespan|'#'*) continue ;; esac
            awk -v s="$secs" -v t="$SUSPECT_THRESHOLD" 'BEGIN{exit !(s+0>t)}' && {
                echo "$p,$c,$ms,$secs" >> "$SUSPECT_CSV"
                echo "  [SUSPECT] $p $c ms=$ms solve_seconds=$secs (> ${SUSPECT_THRESHOLD}s, see .miss_memory/042)"
                found=1
            }
        done < "$gapfile"
    done
    return 0
}

# ---- 前提チェック: costs_ext/ が存在し、j30 が48本あること ----
if [ ! -d "$COST_DIR" ]; then
    echo "[FATAL] $COST_DIR/ not found. Run CostExtend first (see .miss_memory/041)." >&2
    exit 1
fi
n_j30=$(ls "$COST_DIR"/costs_j30*_1.csv 2>/dev/null | wc -l)
if [ "$n_j30" -ne 48 ]; then
    echo "[FATAL] expected 48 j30 cost files in $COST_DIR/, found $n_j30." >&2
    exit 1
fi
echo "[CHECK] $COST_DIR/ has $n_j30 j30 cost files (OK)"
echo "[NOTE] SetThreadExecutionState keeps Windows from idle-sleeping while a program"
echo "       is running, but it cannot stop a manual sleep or a laptop lid close."

t_all_start=$(date +%s)

mip_done() {
    local p="$1"
    for c in $CONDS; do
        [ -f "results/EXT_COST/FUN_MIP/$p/GAP_MIP_${p}_${c}.csv" ] || return 1
    done
    return 0
}

nsga_enc_done() {
    local p="$1"
    for c in $CONDS; do
        [ -f "results/EXT_COST/FUN_ENC/$p/FUN_ENC_${p}_${c}_P1_SchedObj.txt" ] || return 1
        [ -f "results/EXT_COST/FUN_ENC/$p/FUN_ENC_${p}_${c}_P1_MaxShift.txt" ] || return 1
    done
    return 0
}

maxshiftdur_done() {
    local p="$1"
    for c in $CONDS; do
        [ -f "results/EXT_COST/FUN_ENC/$p/FUN_ENC_${p}_${c}_MaxShiftDur.txt" ] || return 1
    done
    return 0
}

# ---- 1チャンク（最大3インスタンス）を処理 ----
run_chunk() {
    local chunk=("$@")

    # ---- MIP: このチャンクで未完了のものを並列起動 ----
    local mip_targets=()
    for p in "${chunk[@]}"; do
        if mip_done "$p"; then
            echo "  [MIP] $p already done, skip"
        else
            mip_targets+=("$p")
        fi
    done
    if [ "${#mip_targets[@]}" -gt 0 ]; then
        declare -A mip_t0
        for p in "${mip_targets[@]}"; do
            echo "  [MIP] start $p (background)"
            mip_t0[$p]=$(date +%s)
            nohup ./RCPSPMIPCpp.exe "j30.sm/$p.sm" --cost-dir=$COST_DIR > "$LOG_DIR/mip_$p.log" 2>&1 &
            disown
        done
        sleep 3
        until [ "$(tasklist 2>/dev/null | grep -c -i RCPSPMIPCpp)" -eq 0 ]; do
            sleep 15
        done
        for p in "${mip_targets[@]}"; do
            t1=$(date +%s)
            elapsed=$((t1 - mip_t0[$p]))
            echo "$p,MIP,$elapsed" >> "$TIMING_CSV"
            if mip_done "$p"; then
                echo "  [MIP] done $p (${elapsed}s)"
            else
                echo "  [MIP] !!WARNING!! $p did not produce all 6 GAP files"
            fi
        done
    fi
    # 既に完了済みでスキップしたインスタンスも含め、GAP_*.csv を毎回チェックする
    # （過去の実行がスリープ等の影響を受けていないか、後から気づけるように）。
    for p in "${chunk[@]}"; do
        check_mip_suspect "$p"
    done

    # ---- NSGA系: チャンク内も逐次 ----
    for p in "${chunk[@]}"; do
        if nsga_enc_done "$p"; then
            echo "  [NSGAEnc] $p already done, skip"
        else
            echo "  [NSGAEnc] start $p"
            t0=$(date +%s)
            ./NSGAEncCpp.exe "j30.sm/$p.sm" --p1-only --cost-dir=$COST_DIR > "$LOG_DIR/nsgaenc_$p.log" 2>&1
            t1=$(date +%s)
            elapsed=$((t1 - t0))
            echo "$p,NSGAEnc,$elapsed" >> "$TIMING_CSV"
            if nsga_enc_done "$p"; then
                echo "  [NSGAEnc] done $p (${elapsed}s)"
            else
                echo "  [NSGAEnc] !!WARNING!! $p incomplete after run (${elapsed}s)"
            fi
        fi

        if maxshiftdur_done "$p"; then
            echo "  [MaxShiftDur] $p already done, skip"
        else
            echo "  [MaxShiftDur] start $p"
            t0=$(date +%s)
            ./NSGAMaxShiftDurCpp.exe "j30.sm/$p.sm" --p1-only --cost-dir=$COST_DIR > "$LOG_DIR/maxshiftdur_$p.log" 2>&1
            t1=$(date +%s)
            elapsed=$((t1 - t0))
            echo "$p,MaxShiftDur,$elapsed" >> "$TIMING_CSV"
            if maxshiftdur_done "$p"; then
                echo "  [MaxShiftDur] done $p (${elapsed}s)"
            else
                echo "  [MaxShiftDur] !!WARNING!! $p incomplete after run (${elapsed}s)"
            fi
        fi
    done
}

total=$((END - START + 1))
i=0
chunk=()

for x in $(seq "$START" "$END"); do
    i=$((i+1))
    p="j30${x}_1"
    chunk+=("$p")

    if [ "${#chunk[@]}" -eq "$CHUNK" ] || [ "$i" -eq "$total" ]; then
        now=$(date +%s)
        elapsed=$((now - t_all_start))
        echo ""
        echo "============================================================"
        echo "[chunk ending at $i/$total] ${chunk[*]}   (elapsed ${elapsed}s)"
        echo "============================================================"
        run_chunk "${chunk[@]}"
        chunk=()
    fi
done

t_all_end=$(date +%s)
echo ""
echo "============================================================"
echo "[DONE] $total instances  total=$((t_all_end - t_all_start))s"
echo "============================================================"
