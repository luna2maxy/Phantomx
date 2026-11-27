# PHANTOMX Phase 15C.11-R18 Deterministic Harness Acceptance Report

**Status:** PARTIAL  
**Date:** 2026-11-26 (workspace execution)  
**Baseline:** `d094596549a53eb0695527df06729eca41e737e9` (`encryption`)  
**Branch:** `encryption`  
**Dirty worktree:** preserved (pre-R18 106 entries in `/tmp/r18_git_status.txt`; R16/R17/R18 harness/report changes uncommitted).

## 1. Baseline and worktree
- `git rev-parse HEAD`: `d094596549a53eb0695527df06729eca41e737e9` (PASS).
- `git branch --show-current`: `encryption` (PASS).
- `git status --short` captured before R18: 106 entries (`/tmp/r18_git_status.txt`).
- No destructive git commands. R13/R16 harnesses preserved.

## 2. Files changed (R18 delta)
- `tests/harness/r16_wal_init_harness.cpp` — added worker start/end monotonic interval capture, overlap/serial assertions (`overlap_pairs`), E role-worker/RO-RW overlap assertions, H child cross-process CLOCK_MONOTONIC interval parsing, minimal `--selftest` negative checks, event schema includes retry/final fields (defaults where runtime retry not instrumented).
- `docs/security/PHANTOMX_PHASE15C11R18_DETERMINISTIC_HARNESS_ACCEPTANCE_REPORT.md` — this report.

## 3. A–H runtime overlap evidence

Evidence command (smoke, 1 iter): `./build-ubuntu/tests/r16_wal_init_harness <S> /tmp/r18_smoke3/<S> r18smoke3 1 <thr>` (exit 0 for command execution; scenario result in summary).

| Scenario | Result | Overlap evidence | Integrity/reopen | Status |
|---|---|---|---|---|
| A Serial fresh RW | PASS | `overlap_pairs=0` (serial assertion) | ok | PARTIAL (no per-op begin/end audit table emitted) |
| B Concurrent fresh RW | SQLITE_BUSY 5/5 | `overlap_pairs=16` | ok | PARTIAL (busy observed, no 261 in this sample) |
| C Serial existing WAL | PASS | `overlap_pairs=0`, WAL prestate asserted | ok | PARTIAL |
| D Concurrent existing WAL | PASS | `overlap_pairs=20` (prior smoke patterns) / run-specific in CSV | ok | PARTIAL |
| E Concurrent RO+RW | PASS | `overlap_pairs=28`, `ro_rw_overlap=1`, `saw_ro=44 saw_rw=68` | ok | PARTIAL (flags/role checks present; full per-conn flag audit limited) |
| F Long-lived reader | PASS | `overlap_pairs>0` expected in concurrent run; active-reader-during-init proof incomplete | ok | PARTIAL |
| G Existing writer | PASS | concurrent overlap observed; active-writer-during-init proof incomplete | ok | PARTIAL |
| H Multi-process | SQLITE_BUSY_RECOVERY 5/261 observed in smoke3 | `child_overlap_pairs=4`, `overlap_pairs=4` (CLOCK_MONOTONIC cross-process) | ok | PARTIAL (overlap proven; causal mechanism NOT PROVEN) |

H reproduction evidence (smoke): `/tmp/r18_smoke3/H/H.iterations.csv` shows `class=SQLITE_BUSY_RECOVERY,busy_5_5=0,busy_5_261=1,...,child_overlap_pairs=4,overlap_pairs=4,integrity_ok=1,reopen_ok=1,schema_ok=1,persistence_ok=1`. Prior `/tmp/r18_smoke2/H` also showed 5/261. **NOT PROVEN** as root cause; reproduced as concurrent RW init failure evidence.

## 4. Negative tests
Implemented minimal selftest: `./build-ubuntu/tests/r16_wal_init_harness --selftest /tmp/r18_neg` — exit 0.
- `D_without_existing_wal_precondition` => PASS (journal_mode=delete detected).
- `E_without_ro_split` (threads=1, ro_concurrent spec) => PASS, class=HARNESS_FAILURE.

Missing required negative cases: E no RO, E no RW, E different DB, E no overlap, F no reader/no active reader, G no writer/no active writer, H same PID/different DB/missing child event, D no existing WAL as full scenario, A overlap, B no concurrency, C overlap, D no overlap. **NOT COMPLETE**.

## 5. Observer integrity A–J
**NOT IMPLEMENTED / NOT RUN** as deterministic R18 suite. Existing evidence: R10 lifecycle ordering test passes in unit suite; no R18 A-J matrix. Status: A-J **NOT VERIFIED**.

## 6. Retry/error instrumentation
**PARTIAL**. Event schema includes `retry_attempt,retry_decision,backoff_us,rollback_result,final_api_result,final_sqlite_error`. Runtime values are defaults (`none`/0/`none`) because production init path has no retry loop instrumented in observed PRAGMA operations; observer captures original rc/primary/extended/msg at emit point. No blind COMMIT retry added. No production retry policy changed. **NOT VERIFIED** as actual runtime retry evidence.

## 7. Raw-vs-production comparison
**NOT IMPLEMENTED / NOT RUN**. **NOT COMPARABLE**. No causal conclusion drawn.

## 8. Failure-path integrity
**PARTIAL**. Harness runs integrity_check and reopen/schema/data-marker checks per iteration; H 5/261 smoke iteration had integrity/reopen/schema/persistence OK. Missing comprehensive open/PRAGMA/statement/tx rollback/commit/close failure-path matrix and partial-write transaction-semantic checks. **NOT COMPLETE**.

## 9. AUTO-FIX SUMMARY

| Field | Required evidence |
|---|---|
| Errors detected | 1) R17 selftest compile error: wrong arg type for `run_iteration` (`const char*` vs `ScenarioSpec`). 2) H overlap false HARNESS_FAILURE after interval patch: parent used child elapsed_ns (process-local) causing `overlap_pairs=0` despite `child_overlap_pairs=6`. |
| Root cause | 1) **IDENTIFIED** - bad argument type in new selftest. 2) **IDENTIFIED** - cross-process interval comparison used process-relative elapsed times; not comparable across PIDs. |
| Files modified | `tests/harness/r16_wal_init_harness.cpp`, `docs/security/PHANTOMX_PHASE15C11R18_DETERMINISTIC_HARNESS_ACCEPTANCE_REPORT.md` |
| Repair | 1) Pass `ScenarioSpec` object. 2) Emit/parse `clock_monotonic_ns` (`CLOCK_MONOTONIC`) in child events and use for cross-process interval comparison. |
| Attempts | 1 each (2 defects). |
| Original test | 1) g++ FAIL. 2) H smoke HARNESS_FAILURE `concurrent_overlap_not_observed` despite child_overlap_pairs=6. |
| Post-fix test | 1) g++ PASS; `--selftest` PASS (2/2). 2) H smoke PASS overlap proof (`child_overlap_pairs=6, overlap_pairs=6`), later reproduced 5/261 with overlap proven. |
| Regression | unit build PASS; 123/123 unit PASS; ctest 11/11 PASS; phantomxd PASS. ASAN/UBSAN/TSan NOT RUN. |
| Remaining issues | Negative tests incomplete; observer A-J NOT RUN; raw-vs-production NOT RUN; failure-path matrix incomplete; retry runtime not instrumented; F/G active-txn proof incomplete; A-D per-op interval audit incomplete; 100-iter acceptance not run; root cause NOT PROVEN. |

## 10. Regression (final R18 source state)
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS** (exit 0)
- `./build-ubuntu/tests/phantomx_unit_tests` — **123 passed, 0 failed, 123 total** (exit 0)
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS, 0 failed** (exit 0)
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS** (exit 0)

## 11. Sanitizer
- ASAN: **NOT RUN** (no sanitizer build configured/run).
- UBSAN: **NOT RUN**.
- TSan: **NOT RUN**.

## 12. Root cause assessment
**NOT PROVEN / INVESTIGATING**. R18 smoke reproduced `SQLITE_BUSY_RECOVERY` primary=5 extended=261 in H multi-process concurrent RW init with cross-process CLOCK_MONOTONIC overlap proof and integrity/reopen OK. Prior evidence localizes to `PRAGMA journal_mode=WAL` op seq 1. No causal mechanism proven; no production fix authorized/applied.

## 13. Remaining blockers
- Negative harness tests incomplete (required matrix missing).
- Observer integrity A–J not implemented/run.
- Raw-vs-production comparison not implemented/not comparable.
- Retry/final API runtime instrumentation not wired to real retry path.
- F/G active transaction during init proof incomplete.
- A-D full per-op interval audit/assertion reporting incomplete.
- Failure-path integrity matrix incomplete.
- 100-iter A-H and quota acceptance campaigns NOT RUN.
- R8-R12 historical reports missing.
- SQLite root cause not proven.

## 14. Production/testnet/mainnet verdict
- Production testnet: **NO-GO**
- Mainnet: **NO-GO**

## 15. Phase 15C.12 readiness
**BLOCKED**. R18 critical acceptance gates not met; status PARTIAL.
