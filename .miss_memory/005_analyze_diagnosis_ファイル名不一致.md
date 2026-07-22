# 005: analyze_diagnosis.py のファイル名パターンが実際の出力と不一致

## 発生日
2026-05-21

## バグの概要

`analyze_diagnosis.py` を実行してもガントチャートやコスト分析グラフが生成されなかった。
スクリプトが `FUN_MS_*`, `SCHED_MS_*` という古いファイル名を探していたが、
実際の出力ファイルは `FUN_ENC_*`, `SCHED_ENC_*` という名前だった。

## 根本原因

C++ 側の出力ファイル命名規則が変わったが、Python スクリプトが追従していなかった。

```python
# analyze_diagnosis.py（修正前・バグあり）
sched_ms_path = f"SCHED_MS_{prefix}_{cond_tag}_MaxShift"   # 古い名前
fun_ms_path   = f"FUN_MS_{prefix}_{cond_tag}_MaxShift"      # 古い名前
```

## 修正内容

候補リストで新旧両方に対応する形に修正：

```python
sched_ms_candidates = [
    f"SCHED_ENC_{prefix}_{cond_tag}_MaxShift.txt",  # 新しい名前
    f"SCHED_MS_{prefix}_{cond_tag}_MaxShift",        # 古い名前（後方互換）
]
sched_ms_path = next((p for p in sched_ms_candidates if os.path.exists(p)), sched_ms_candidates[0])
```

## 実行方法（正しいコマンド）

`cmake-build-release` ディレクトリから実行する（FUN/SCHED ファイルと同じ場所）：
```powershell
python ../analyze_diagnosis.py j301_1 RR000_RV0
```

## 教訓

- **C++ 側で出力ファイル名を変更したら、対応する Python スクリプトも同時に更新する**
- ファイルパスを 1 箇所で管理するか、候補リストで複数パターンに対応する
- スクリプトの実行ディレクトリに注意（`cmake-build-release/` から実行が必要）
