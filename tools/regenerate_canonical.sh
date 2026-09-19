#!/usr/bin/env bash
# ============================================================
#  regenerate_canonical.sh
#  正本コスト表 costs/costs_<prefix>.csv で MIP / MaxShift / SchedObj を
#  再生成し（MaxShiftDur は温存）、整合検証と図の再生成まで一括で行う。
#
#  実行: cmake-build-release から
#        bash ../tools/regenerate_canonical.sh
#
#  前提:
#    - RCPSPMIPCpp.exe / NSGAEncCpp.exe をビルド済み
#      （nmake RCPSPMIPCpp NSGAEncCpp）
#    - costs/costs_<prefix>.csv（正本）が存在（無ければ実行時に FATAL 停止）
#
#  重要:
#    - NSGAEncCpp は SchedObj/MaxShift(P1/P2/P3) のみ出力＝MaxShiftDur は生成せず温存。
#    - 正本 CSV は読み取り専用（コード側で自動生成・上書きは禁止済み）。
#    - 所要時間の目安: MIP の RV1/高RR 条件は ε 範囲が広く重い（全体で数時間規模）。
# ============================================================
set -uo pipefail
INSTANCES="j3011_1 j3017_1 j3026_1 j3047_1 j309_1"

for p in $INSTANCES; do
  echo "############################## $p : MIP ##############################"
  ./RCPSPMIPCpp.exe "j30.sm/$p.sm"       # -> results/FUN_MIP/$p/ に直接出力

  echo "############################## $p : NSGA (SchedObj / MaxShift) ##############################"
  ./NSGAEncCpp.exe "j30.sm/$p.sm"        # -> cwd に FUN_ENC_*/SCHED_ENC_* を出力

  # 生成物（SchedObj/MaxShift, P1/P2/P3）を results/FUN_ENC/$p/ へ移動。
  # NSGAEncCpp は MaxShiftDur を生成しないため、温存対象は決して上書きされない。
  mkdir -p "results/FUN_ENC/$p"
  mv -f FUN_ENC_${p}_*.txt   "results/FUN_ENC/$p/" 2>/dev/null || true
  mv -f SCHED_ENC_${p}_*.txt "results/FUN_ENC/$p/" 2>/dev/null || true
done

echo "############################## verify cost consistency ##############################"
python ../tools/verify_cost_consistency.py --build-dir .
VERIFY_RC=$?

echo "############################## regenerate figures ##############################"
python ../visualize_mip_comparison.py --build-dir .
# encoding 図（MaxShiftDur 側）: build_dir 直下 or results/FUN_ENC を参照。
python ../visualize_maxshiftdur.py 2>/dev/null || \
  echo "  [note] visualize_maxshiftdur.py はパス指定が必要な場合あり（results/FUN_ENC 配下を参照）"

echo "=================================================================="
if [ "$VERIFY_RC" -eq 0 ]; then
  echo ">>> DONE: 再生成＋整合検証 PASS。"
else
  echo ">>> DONE(with FAIL): 検証で CORE 系列の不一致あり。上のログを確認。"
fi
echo "=================================================================="
