#!/bin/bash
# Reproduces every CSV in results/ (pinned to cores 44-47, niced).
set -e
cd "$(dirname "$0")"
PY="taskset -c 44-47 nice -n 10 ../../.venv/bin/python"
DS="U128-n2^10 U128-n2^12 U64-n2^12 code-simhash64 cifar-dhash64-mild"
$PY verify.py 40 > results/verify.log
for d in $DS; do $PY attack_known.py "$d" 2000 100 0; done
for set in shadow shadow2 uniform prior; do
  for d in $DS; do $PY knowledge.py "$d" "$set" 2000 100; done
done
for d in $DS; do $PY knowledge.py "$d" sweep 300 1; done
$PY cost.py
$PY heavy.py > /dev/null
for p in known shadow shadow2 uniform prior sweep; do $PY summarize.py "$p" > /dev/null; done
$PY make_tables.py > /dev/null
echo ALL DONE
