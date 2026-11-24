# E5 — abort/policy tests on the final build (in-process harness)

Date 2026-11-12 (PDT). Host: loopback TCP and AF_UNIX blocked, so fpsi_bench (both parties in one
process over coproto's LocalAsyncSocket, same fpsiSender/fpsiReceiver as net_bench) is used.

## Harness fix (`harness_abort.patch`, applies to impl/src/main.cpp only)
audit_r5 found that fpsi_bench hangs when one party throws: `when_all_ready` waits for both coroutines
and the peer waits forever on a receive. The fix wraps each party in a coroutine `guarded(...)` that
catches the exception, records it, and then `co_await`s `Socket::close()` on that party's end of the
in-memory channel. `LocalAsyncSocket::Sock::close()` marks the peer end `remoteClosed` and completes the
peer's pending send/recv with an error, so the peer throws as well and terminates. main() then prints
`abort: sender: ...` / `abort: receiver: ...` and an `aborted ...` line and exits with status 3.
The data-file header check now prints `abort: data file header does not match -d` (same wording as
net_bench, exit 1) so that the `wrong_dim` pattern of test_policies.sh applies. No library source changed.

## Build and library identity
`cmake -S impl -B impl/build12 -DCMAKE_CXX_COMPILER=/usr/bin/c++` (GCC 11.4, Release default), `make -j4`.
sha256 (`sha256_compare.txt`, script `hash_compare.sh`): all seven library objects (fpsi, HamCpsi, oep,
osn, circuits, mip, ot_threshold) and `libmipfpsi.a` (67172db0...) are bit-identical to audit_r5/build;
net_bench and unit_tests are identical too; only fpsi_bench differs (harness).

## Policy tests (`run_policies.sh`, logs in `logs/`, table `policies.csv`)
Same 11 cases, arguments and expected outcomes as impl/scripts/test_policies.sh (recv_overflow at
m = n = 4096 as there, not the 1024 of audit_r5's in-process copy), with `-indexverify` ground truth,
timeout 300 s, cores 44,45, /usr/bin/time -v. "ok" = exit 0 and false_neg=0 false_pos=0; "abort" =
non-zero exit; the pattern must appear in the log.

| case | expected | exit | pattern found | who aborted (message) | verdict |
|---|---|---|---|---|---|
| recv_overflow | abort | 3 | "B exceeds" | R: B exceeds its public bound; S: remoteClosed | PASS |
| send_overflow_index | abort | 3 | "U exceeds" | S: U exceeds its public bound; R: remoteClosed | PASS |
| send_overflow_fallback | ok | 0 | "fallback=1" | – | PASS |
| bounds_ok | ok | 0 | "fallback=0" | – | PASS |
| dp_plus_bound | abort | 3 | "exclusive" | R: public bounds and DP noise are exclusive policies; S: remoteClosed | PASS |
| cuckoo_retry (3 injected failures) | ok | 0 | – | – | PASS |
| cuckoo_retry_noU (3 injected failures) | ok | 0 | – | – | PASS |
| empty_sender | ok | 0 | "B=0" | – | PASS |
| empty_receiver | ok | 0 | "B=0" | – | PASS |
| wrong_dim | abort | 1 | "header does not match" | harness, before the protocol | PASS |
| cuckoo_hook_control (16 injected failures) | abort | 3 | "failed 16 times" | S: cuckoo hashing failed 16 times; R: remoteClosed | PASS |

**11/11 PASS, no hang** (every abort case terminates in ≤ 0.04 s wall). In every protocol abort both
parties exit with an error (the throwing party with its own message, the peer with
`coproto::code::remoteClosed`). nr_throttled delta 0 for all runs.

Caveat: `wrong_dim` is detected by the harness's file reader before any protocol message, as in
net_bench; it does not exercise the protocol library.
