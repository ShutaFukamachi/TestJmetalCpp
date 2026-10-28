#!/bin/bash
# 2時間未満のかたまりを1つずつ流す（呼び出し側で1回に1つ）。引数: キューファイル名（tools/ext1024 内）
T=/c/Users/PC2504/TestProject/TestJmetalCpp/tools
export EXE=${EXE:-NSGA1024c.exe} PAR=${PAR:-3} LOGD=logs/t1028
bash $T/ext1014/run_chunk.sh $T/ext1028/$1
