#!/bin/bash
export PATH=/usr/bin:/bin:$PATH
cd /c/Users/PC2504/TestProject/TestJmetalCpp
bash tools/ext1014/run_queue.sh tools/ext1014/queue_20261020.txt cmake-build-release/logs/t1020/_progress.txt > cmake-build-release/logs/t1020/_launcher.txt 2>&1
