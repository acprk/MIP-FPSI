#!/bin/bash
# One two-party run of the Blass-Noubir fuzzy PSI code, single-threaded,
# sender pinned to core 40 and receiver pinned to core 41.
# usage: run_one.sh BUILD_DIR logN_S logN_R ELL T TAG
set -u
BIN="$1"; LNS="$2"; LNR="$3"; ELL="$4"; T="$5"; TAG="$6"
ROOT=<ARTIFACT_ROOT>/baselines_r5/blass_noubir
OUT="$ROOT/logs/$TAG"
CG=/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat
mkdir -p "$OUT"

snapshot() {
  echo "date_utc: $(date -u +%FT%T.%NZ)"
  echo "ps (party/run):"
  ps -C party,run -o pid,psr,etime,args
  echo "ps (threads currently on cpus 40,41,92,93):"
  ps -eLo pid,tid,psr,pcpu,comm | awk 'NR==1 || $3==40 || $3==41 || $3==92 || $3==93'
  echo "loadavg: $(cat /proc/loadavg)"
  grep -E 'nr_periods|nr_throttled|throttled_usec' "$CG"
}

# Do not start while any 'party' process is alive (ours or anyone's); never kill them.
for i in $(seq 1 120); do
  if ! ps -C party -o pid= > /dev/null; then break; fi
  echo "waiting: leftover party processes:"; ps -C party -o pid,psr,etime,args
  sleep 30
done

# Quiet-host gate: start only when the user-1000 cgroup (quota 26 CPUs) has not
# been throttled during a 30 s window and the 1-min load average is < 24.
# Gives up waiting after 90 min and runs anyway (recorded in gate.txt).
GATE_OK=0
for i in $(seq 1 180); do
  A=$(awk '/nr_throttled/{print $2}' "$CG")
  sleep 30
  B=$(awk '/nr_throttled/{print $2}' "$CG")
  L1=$(cut -d' ' -f1 /proc/loadavg)
  echo "$(date -u +%T) gate: throttled_delta_30s=$((B-A)) load1=$L1" >> "$OUT/gate.txt"
  if [ $((B-A)) -eq 0 ] && awk -v l="$L1" 'BEGIN{exit !(l<24)}'; then GATE_OK=1; break; fi
done
echo "gate_ok=$GATE_OK" >> "$OUT/gate.txt"

snapshot > "$OUT/pre.txt"

export OMP_NUM_THREADS=1
# Loopback TCP and AF_UNIX are blocked on this host's sandbox; the patched
# emp NetIO uses two named pipes per channel instead (see patches/).
FIFO="$ROOT/fifo"
rm -rf "$FIFO"; mkdir -p "$FIFO"
mkfifo "$FIFO/p9876.s2c" "$FIFO/p9876.c2s"
export EMP_FIFO_DIR="$FIFO"
echo "cmd: EMP_FIFO_DIR=$FIFO OMP_NUM_THREADS=1 taskset -c 40 $BIN/party $LNS $LNR $ELL $T 1  &  OMP_NUM_THREADS=1 taskset -c 41 $BIN/party $LNS $LNR $ELL $T 2" > "$OUT/cmd.txt"

T0=$(date +%s.%N)
taskset -c 40 /usr/bin/time -v -o "$OUT/time_sender.txt" "$BIN/party" "$LNS" "$LNR" "$ELL" "$T" 1 > "$OUT/sender.txt" 2>&1 &
SPID=$!
taskset -c 41 /usr/bin/time -v -o "$OUT/time_receiver.txt" "$BIN/party" "$LNS" "$LNR" "$ELL" "$T" 2 > "$OUT/receiver.txt" 2>&1
RRC=$?
wait "$SPID"
SRC=$?
T1=$(date +%s.%N)

snapshot > "$OUT/post.txt"

python3 "$ROOT/summarize_run.py" "$OUT" "$T0" "$T1" "$RRC" "$SRC" "$LNS" "$LNR" "$ELL" "$T"
