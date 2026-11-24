#!/usr/bin/env bash
# Regenerate the paper's tables and summary ratios from the shipped logs/CSVs. No benchmark is run.
#
#   ./reproduce_tables.sh [OUTDIR]        (default OUTDIR = ./regenerated; it is deleted first)
#
# Needs only python3 (standard library). Every step writes into OUTDIR and is then compared with
# the shipped copy; the script prints one line per check and exits non-zero if any check fails.
#
#  step  paper item                                    input                                   generator
#  1     Table "vs otFPSI" (main), Fig. vsot data,     results/campaign3_20261023_2103         paper/make_c3_tables.py
#        Table "real data" (main), breakdown (supp.)   (+ campaign2_20261023_1344/ot_cifar)    + paper/shade_tables.py
#  2     Tables "scale" and "ablations" (supp.)        results/campaign2_20261023_1344         paper/make_campaign_tables.py
#  3     Table "paired" (main + supp.), 53-115x etc.   experiments_r6/e2_paired/runs           experiments_r6/aggregate.py e2
#                                                                                              + experiments_r6/paired_exact.py
#  4     Attribution numbers (main "Source of gain")   experiments_r6/e1_attribution/runs      tools/jsonl_until.py +
#                                                                                              experiments_r6/aggregate.py e1
#  5     Build A/B ratios, key-check control, CPython  results/campaign5_20261025_0013         paper/make_c5_paired.py
#        and CIFAR repetitions (supp.)
#  6     Earlier paired campaign (supp. 92-121x)       results/campaign4_20261024_0922         paper/make_c4_paired.py
#  7     Load-injection tables (DP+rounded columns)    analysis/injection/results/summary_*    analysis/injection/make_tables.py
set -uo pipefail
A=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$A/regenerated}
PY=${PYTHON:-python3}
rm -rf "$OUT"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
fails=0
ok()   { echo "PASS  $*"; }
bad()  { echo "FAIL  $*"; fails=$((fails + 1)); }
same() { # same <generated> <shipped> <label>
  if cmp -s "$1" "$2"; then ok "$3"; else bad "$3 (diff $1 $2)"; fi
}
want() { # want <file> <fixed string> <label>
  if grep -qF -- "$2" "$1"; then ok "$3"; else bad "$3 (missing '$2' in $1)"; fi
}

# 1 + 2: campaign 3 and campaign 2 tables (LaTeX bodies included by the paper via \tabinput)
mkdir -p "$OUT/paper" && cd "$OUT/paper" || exit 1
"$PY" -I "$A/paper/make_c3_tables.py" "$A/results/campaign3_20261023_2103" > c3.log 2>&1 \
  && ok "make_c3_tables.py validation (run counts, FN=FP=0, otFPSI verified)" || bad "make_c3_tables.py (see $OUT/paper/c3.log)"
"$PY" -I "$A/paper/make_campaign_tables.py" "$A/results/campaign2_20261023_1344" > c2.log 2>&1 \
  && ok "make_campaign_tables.py on campaign 2" || bad "make_campaign_tables.py (see $OUT/paper/c2.log)"
"$PY" -I "$A/paper/shade_tables.py" tables > shade.log 2>&1 && ok "shade_tables.py" || bad "shade_tables.py"
for f in "$A"/paper/tables_reference/c2_*.tex "$A"/paper/tables_reference/c3_*.tex; do
  b=$(basename "$f")
  same "tables/$b" "$f" "paper table $b identical to the one used in the paper"
done

# 3: E2 paired campaign
mkdir -p "$OUT/experiments_r6/e2_paired/runs" "$OUT/experiments_r6/e1_attribution/runs"
cp "$A/experiments_r6/e2_paired/runs/summary.jsonl" "$OUT/experiments_r6/e2_paired/runs/"
R6="$OUT/experiments_r6" "$PY" -I "$A/experiments_r6/aggregate.py" e2 > "$OUT/experiments_r6/e2.log" 2>&1 || bad "aggregate.py e2"
same "$OUT/experiments_r6/e2_paired/tables.md" "$A/experiments_r6/e2_paired/tables.md" "E2 tables.md regenerated from runs/summary.jsonl"
same "$OUT/experiments_r6/e2_paired/results.csv" "$A/experiments_r6/e2_paired/results.csv" "E2 results.csv regenerated from runs/summary.jsonl"
"$PY" -I "$A/experiments_r6/paired_exact.py" "$OUT/experiments_r6/e2_paired/results.csv" > "$OUT/experiments_r6/paired_exact.txt" 2>&1 || bad "paired_exact.py"
P="$OUT/experiments_r6/paired_exact.txt"
want "$P" "delta=4 exact  per-seed 123.331 78.468 68.143 80.549 124.485 | median 80.549 [68.143, 124.485]" "E2 exact delta=4 median 80.55x"
want "$P" "delta=4 Ufree  per-seed 108.846 55.566 56.722 52.738 114.491 | median 56.722 [52.738, 114.491]" "E2 U-free delta=4 range 52.74-114.49x"
want "$P" "delta=8 Ufree  per-seed 13.351 12.823 13.745 27.228 27.030 | median 13.745 [12.823, 27.228]" "E2 U-free delta=8 range 12.82-27.23x"
want "$P" "delta=4 pow2   per-seed 123.295 64.223 61.193 66.471 125.305 | median 66.471 [61.193, 125.305]" "E2 rounded delta=4"
want "$P" "delta=8 exact  per-seed 25.683 24.756 24.271 31.772 31.734 | median 25.683 [24.271, 31.772]" "E2 exact delta=8"
want "$P" "delta=8 pow2   per-seed 14.409 13.668 15.628 33.450 30.704 | median 15.628 [13.668, 33.450]" "E2 rounded delta=8"

# 4: E1 attribution. The shipped tables.md/results.csv (and the paper's attribution numbers) were
# aggregated at 09:10:03 PST while the runner was still appending rounds (last run ended 09:18:55).
# 4a reproduces that snapshot from the records that had ended by then; the shipped tables.md was
# written by aggregate.py before its last two "same binary" rows were added, so it must equal the
# regenerated file up to its own length. 4b aggregates all records (informational, differs).
"$PY" -I "$A/tools/jsonl_until.py" "$A/experiments_r6/e1_attribution/runs/summary.jsonl" \
  "$OUT/experiments_r6/e1_attribution/runs/summary.jsonl" "2026-11-12 09:10:03" > "$OUT/experiments_r6/e1_cut.log" 2>&1
R6="$OUT/experiments_r6" "$PY" -I "$A/experiments_r6/aggregate.py" e1 > "$OUT/experiments_r6/e1.log" 2>&1 || bad "aggregate.py e1"
same "$OUT/experiments_r6/e1_attribution/results.csv" "$A/experiments_r6/e1_attribution/results.csv" "E1 results.csv (snapshot 09:10:03) regenerated"
E1S="$A/experiments_r6/e1_attribution/tables.md"
if cmp -s -n "$(wc -c < "$E1S")" "$OUT/experiments_r6/e1_attribution/tables.md" "$E1S"; then
  ok "E1 tables.md (snapshot 09:10:03) regenerated; shipped file is a prefix of it"
else
  bad "E1 tables.md snapshot"
fi
mkdir -p "$OUT/experiments_r6_e1_all/e1_attribution/runs"
cp "$A/experiments_r6/e1_attribution/runs/summary.jsonl" "$OUT/experiments_r6_e1_all/e1_attribution/runs/"
R6="$OUT/experiments_r6_e1_all" "$PY" -I "$A/experiments_r6/aggregate.py" e1 > /dev/null 2>&1 \
  && echo "INFO  E1 over all 44 records (incl. rounds finished after the snapshot): $OUT/experiments_r6_e1_all/e1_attribution/tables.md"

# 5 + 6: campaign 5 and campaign 4 summaries (printed; compared with the shipped expected output)
mkdir -p "$OUT/summaries" && cd "$OUT/summaries" || exit 1
"$PY" -I "$A/paper/make_c5_paired.py" "$A/results/campaign5_20261025_0013" > c5.txt 2>&1 || bad "make_c5_paired.py"
"$PY" -I "$A/paper/make_c4_paired.py" "$A/results/campaign4_20261024_0922" > c4.txt 2>&1 || bad "make_c4_paired.py"
same c5.txt "$A/paper/expected/c5.txt" "campaign 5 summary (build A/B ratios 1.01-1.05, key-check control 1.51x/1.47x)"
same c4.txt "$A/paper/expected/c4.txt" "campaign 4 summary"

# 7: load-injection tables
mkdir -p "$OUT/analysis_injection/results"
cp "$A/analysis/injection/make_tables.py" "$OUT/analysis_injection/"
cp "$A"/analysis/injection/results/summary_*.csv "$A/analysis/injection/results/cost.csv" \
   "$A"/analysis/injection/results/*.meta.txt "$OUT/analysis_injection/results/"
"$PY" -I "$OUT/analysis_injection/make_tables.py" > "$OUT/analysis_injection/make_tables.log" 2>&1 || bad "injection make_tables.py"
same "$OUT/analysis_injection/results/tables.md" "$A/analysis/injection/results/tables.md" "injection tables.md"

echo
if [ "$fails" -eq 0 ]; then echo "all checks passed; output in $OUT"; else echo "$fails check(s) failed; output in $OUT"; fi
exit $((fails > 0))
