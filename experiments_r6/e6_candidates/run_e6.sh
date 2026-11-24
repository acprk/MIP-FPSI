#!/usr/bin/env bash
# E6: plaintext candidate statistics (not a timing run; kept off cores 44,45 and their HT siblings).
set -uo pipefail
E=<ARTIFACT_ROOT>/experiments_r6/e6_candidates
D=<ARTIFACT_ROOT>/data
CS="taskset -c 0-7 $E/build/cand_stats"
cd "$E"
rm -f cand_stats.csv
$CS -name synth_d128_t4_b5s1 -d 128 -delta 4 -b 5 -s 1 -m 4096 -n 4096 -seed 1 -csv cand_stats.csv -hist hist_synth_d128_t4.csv
$CS -name synth_d128_t16_b9s1 -d 128 -delta 16 -b 9 -s 1 -m 4096 -n 4096 -seed 1 -csv cand_stats.csv -hist hist_synth_d128_t16.csv
$CS -name cpython_simhash64_t3_b4s1 -d 64 -delta 3 -b 4 -s 1 -data $D/code_simhash64.bin -csv cand_stats.csv -hist hist_cpython.csv
$CS -name cifar_dhash64_t4_b6s2 -d 64 -delta 4 -b 6 -s 2 -data $D/cifar_dhash64_mild.bin -csv cand_stats.csv -hist hist_cifar.csv
