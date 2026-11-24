#!/usr/bin/env bash
# Print the git commit of every dependency clone below a volePSI "out" directory, e.g.
#   deps/record_dep_versions.sh deps/volepsi/out
# volePSI's dependency manager clones libOTe (and, through libOTe, cryptoTools, coproto, macoro,
# libsodium, boost, bitpolymul, ...) into <volepsi>/out/<name>. Run this on the machine that built
# the benchmark binaries to record the exact dependency versions next to the results.
set -uo pipefail
OUT=${1:?usage: record_dep_versions.sh <volepsi>/out}
find "$OUT" -maxdepth 4 -name .git | sort | while read -r g; do
  d=$(dirname "$g")
  printf '%s\t%s\t%s\n' "$(git -C "$d" rev-parse HEAD 2>/dev/null)" \
    "$(git -C "$d" config --get remote.origin.url 2>/dev/null)" "${d#"$OUT"/}"
done
