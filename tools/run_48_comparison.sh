#!/usr/bin/env bash
# ============================================================
#  run_48_comparison.sh
#  j30X_1 (X=1..48) を RR<=0.50 の6条件・P1のみで
#  MIP / NSGAEncCpp(SchedObj+MaxShift) / NSGAMaxShiftDurCpp(MaxShiftDur) を実行する。
#
#  実行: cmake-build-release から
#        bash ../tools/run_48_comparison.sh [START] [END]
#        例: bash ../tools/run_48_comparison.sh 1 3     # トライアル(j301_1..j303_1)
#            bash ../tools/run_48_comparison.sh          # 全48件
#
#  再開性: インスタンス単位で完了判定し、既に結果が揃っていればスキップする。
#  並列性: 3インスタンスを1チャンクとして扱う。
#          チャンク内のMIPは最大3プロセス並列で同時起動→全完了待ち。
#          NSGA系(NSGAEncCpp/NSGAMaxShiftDurCpp)は costs.csv のcwdコピーが
#          競合するため、チャンク内・チャンク間を問わず必ず逐次実行する。
# ============================================================
set -uo pipefail

START="${1:-1}"
END="${2:-48}"
CONDS="RR000_RV0 RR000_RV1 RR025_RV0 RR025_RV1 RR050_RV0 RR050_RV1"
CHUNK=3

LOG_DIR="../.run48_logs"
mkdir -p "$LOG_DIR"

t_all_start=$(date +%s)

mip_done() {
    local p="$1"
    for c in $CONDS; do
        [ -f "results/FUN_MIP/$p/GAP_MIP_${p}_${c}.csv" ] || return 1
    done
    return 0
}

nsga_enc_done() {
    local p="$1"
    for c in $CONDS; do
        [ -f "results/FUN_ENC/$p/FUN_ENC_${p}_${c}_P1_SchedObj.txt" ] || return 1
        [ -f "results/FUN_ENC/$p/FUN_ENC_${p}_${c}_P1_MaxShift.txt" ] || return 1
    done
    return 0
}

maxshiftdur_done() {
    local p="$1"
    for c in $CONDS; do
        [ -f "results/FUN_ENC/$p/FUN_ENC_${p}_${c}_MaxShiftDur.txt" ] || return 1
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
        for p in "${mip_targets[@]}"; do
            echo "  [MIP] start $p (background)"
            nohup ./RCPSPMIPCpp.exe "j30.sm/$p.sm" > "$LOG_DIR/mip_$p.log" 2>&1 &
            disown
        done
        sleep 3
        until [ "$(tasklist 2>/dev/null | grep -c -i RCPSPMIPCpp)" -eq 0 ]; do
            sleep 15
        done
        for p in "${mip_targets[@]}"; do
            if mip_done "$p"; then
                echo "  [MIP] done $p"
            else
                echo "  [MIP] !!WARNING!! $p did not produce all 6 GAP files"
            fi
        done
    fi

    # ---- NSGA系: チャンク内も逐次 ----
    for p in "${chunk[@]}"; do
        if nsga_enc_done "$p"; then
            echo "  [NSGAEnc] $p already done, skip"
        else
            echo "  [NSGAEnc] start $p"
            t0=$(date +%s)
            ./NSGAEncCpp.exe "j30.sm/$p.sm" --p1-only > "$LOG_DIR/nsgaenc_$p.log" 2>&1
            mkdir -p "results/FUN_ENC/$p"
            mv -f FUN_ENC_${p}_*.txt   "results/FUN_ENC/$p/" 2>/dev/null || true
            mv -f SCHED_ENC_${p}_*.txt "results/FUN_ENC/$p/" 2>/dev/null || true
            t1=$(date +%s)
            if nsga_enc_done "$p"; then
                echo "  [NSGAEnc] done $p ($((t1-t0))s)"
            else
                echo "  [NSGAEnc] !!WARNING!! $p incomplete after run ($((t1-t0))s)"
            fi
        fi

        if maxshiftdur_done "$p"; then
            echo "  [MaxShiftDur] $p already done, skip"
        else
            echo "  [MaxShiftDur] start $p"
            t0=$(date +%s)
            ./NSGAMaxShiftDurCpp.exe "j30.sm/$p.sm" --p1-only > "$LOG_DIR/maxshiftdur_$p.log" 2>&1
            t1=$(date +%s)
            if maxshiftdur_done "$p"; then
                echo "  [MaxShiftDur] done $p ($((t1-t0))s)"
            else
                echo "  [MaxShiftDur] !!WARNING!! $p incomplete after run ($((t1-t0))s)"
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
