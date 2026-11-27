# PHANTOMX Phase 15C.11-R19 Observer and Causal Validation Report

**Status:** PARTIAL / NOT ACCEPTANCE-READY  
**Date:** 2026-11-26 (workspace execution)  
**Baseline:** `d094596549a53eb0695527df06729eca41e737e9` (`encryption`)  
**Branch:** `encryption`  
**Dirty worktree:** preserved (pre-R19 106 entries in `/tmp/r19_git_status.txt`; R16-R19 changes uncommitted). No destructive git commands.

## 1. Baseline/worktree
- HEAD/branch captured. Dirty worktree preserved. R13/R16/R17/R18 artifacts preserved.
- No genesis/canonical config/network ID/monetary/reward/block/consensus/PoP/verifier lifecycle/quota/registry changes.
- No production SQLite journal/synchronous/busy_timeout/tx policy/connection ownership changes. No Phase 15C.12.

## 2. Files changed (R19 delta)
- `tests/harness/r16_wal_init_harness.cpp` — F/G active-transaction monitor proof (`sqlite3_txn_state` during init window), raw-txn mutex, R18 overlap/assertion improvements retained.
- `tests/harness/r19_observer_integrity.cpp` — new lightweight deterministic observer integrity harness (A-J basic checks).
- `tests/harness/r19_raw_vs_prod.cpp` — new limited raw-vs-production comparison harness.
- `docs/security/PHANTOMX_PHASE15C11R19_OBSERVER_AND_CAUSAL_VALIDATION_REPORT.md` — this report.

## 3. Observer integrity A–J
Executed: `./build-ubuntu/tests/r19_observer_integrity` (exit 0, `observer_integrity failures=0`).

| Test | Result | Evidence | Status |
|---|---|---|---|
| A Success event capture | PASS | CONNECTION_READY/INITIALIZATION_BEGIN observed | PARTIAL (basic) |
| B Failure event capture | PASS | OPEN_FAILURE on missing readonly DB | PARTIAL |
| C Original SQLite error preservation | PASS | error string contains primary/extended | PARTIAL (not full pre-rollback/close overwrite matrix) |
| D Disabled observer semantic equivalence | PASS | same open/exec behavior with/without observer | PARTIAL (limited workload) |
| E Concurrent callback invocation | PASS | 8 threads opened DBs, callbacks invoked | PARTIAL (no event-loss stress proof) |
| F Callback lifetime during close | PASS | no crash, events observed | PARTIAL |
| G Reentrancy | PASS | simple callback no deadlock | PARTIAL (no SqliteDb-reentrant callback test) |
| H Callback exception policy | PASS | throwing observer did not fail open | PARTIAL (policy is catch-and-continue; not fully specified) |
| I Sequence uniqueness/correlation completeness | PASS | monotonic_seq unique, conn_id present | PARTIAL |
| J No unintended serialization/deadlock | PASS | bounded completion <5s with 50us callback delay | PARTIAL (not proof of no serialization) |

Overall A-J: **PARTIAL / NOT VERIFIED** as complete suite. Missing: bounded overflow/memory policy, destruction/lifetime stress, exception/reentrancy semantic matrix, concurrent event-loss proof, enabled-vs-disabled transaction outcome equivalence matrix.

## 4. Negative test matrix
Executed: `./build-ubuntu/tests/r16_wal_init_harness --selftest /tmp/r18_neg` (exit 0, failures=0).

| Case | Result | Status |
|---|---|---|
| D without existing WAL precondition | PASS (detected delete mode) | PASS (limited) |
| E without RO split (threads=1) | PASS, HARNESS_FAILURE | PASS (limited) |
| All other required negatives (E no RO/RW/diff DB/no overlap/wrong flags; F/G missing/inactive/completed/diff DB; H same PID/diff DB/missing child event/diff IDs/no overlap; A overlap; B no concurrency; C overlap/no WAL; D no WAL/no overlap) | NOT IMPLEMENTED | NOT RUN |

Negative matrix: **NOT COMPLETE**.

## 5. Runtime overlap A–H
Smoke: `./build-ubuntu/tests/r16_wal_init_harness <S> /tmp/r18_smoke3/<S> r18smoke3 1 <thr>`.

| Scenario | Result | Evidence | Status |
|---|---|---|---|
| A Serial fresh RW | PASS | overlap_pairs=0 | PARTIAL |
| B Concurrent fresh RW | SQLITE_BUSY 5/5 | overlap_pairs=16 | PARTIAL |
| C Serial existing WAL | PASS | overlap_pairs=0, WAL prestate | PARTIAL |
| D Concurrent existing WAL | PASS | overlap observed, WAL prestate | PARTIAL |
| E Concurrent RO+RW | PASS | overlap_pairs=28, ro_rw_overlap=1, saw_ro/saw_rw | PARTIAL |
| F Long-lived reader | PASS | reader_txn_active_pre=1, reader_active_during_init=1 | PARTIAL (monitor-based proof) |
| G Existing writer | PASS | writer_txn_active_pre=1, writer_active_during_init=1 | PARTIAL (monitor-based proof) |
| H Multi-process | SQLITE_BUSY_RECOVERY 5/261 observed | child_overlap_pairs=4, overlap_pairs=4, CLOCK_MONOTONIC cross-process, integrity/reopen OK | PARTIAL (reproduced; not causal) |

## 6. F/G active transaction proof
**PARTIAL / IMPROVED**. R19 adds `sqlite3_txn_state` pre-check and background monitor sampling during init window, guarded by mutex. Evidence: F/G smoke show `*_txn_active_pre=1` and `*_active_during_init=1`. Limitation: monitor is sampling-based, not a synchronized proof at exact RW init begin/end boundaries; does not capture BEGIN/COMMIT timestamps as structured transaction events in CSV.

## 7. Retry/error instrumentation
**PARTIAL**. Event schema has retry/final fields; values default because no real retry loop is instrumented in observed init PRAGMA path. Observer captures original rc/primary/extended/msg at emit point. No production retry policy changed. **NOT VERIFIED** as runtime retry evidence.

## 8. Raw-vs-production comparison
Executed: `./build-ubuntu/tests/r19_raw_vs_prod` (exit 0, 3 iter, 16 threads, fresh concurrent).

Observed:
- fresh_raw_concurrent: ok=2 fail=14 busy55=14 busy261=0; ok=2 fail=14 busy55=14; ok=15 fail=1 busy55=0 **busy261=1**.
- fresh_prod_concurrent: ok=12 fail=4 busy55=4; ok=5 fail=11 busy55=11; ok=12 fail=4 busy55=4.

Interpretation: **LIMITED / NOT COMPARABLE as full causal proof**. Same host/SQLite/PRAGMA sequence/open flags; but wrapper/observer/diag/RAII/timing differences exist. Raw path also reproduced 5/261 in one sample, suggesting issue is not exclusively a production-wrapper artifact; **NOT PROVEN**.

## 9. Failure-path integrity
**NOT COMPLETE**. No deterministic open/PRAGMA/stmt/BEGIN/COMMIT/ROLLBACK/close failure matrix. Existing harness checks integrity_check/reopen/schema/data markers; R18 H 5/261 iteration had integrity/reopen OK. Partial application state checks **NOT COMPLETE**.

## 10. AUTO-FIX SUMMARY

| Field | Required evidence |
|---|---|
| Errors detected | 1) R18 selftest compile arg mismatch. 2) H cross-process overlap false failure due to process-local elapsed ns comparison. 3) R19 observer integrity harness nodiscard close warnings (non-blocking). |
| Root cause | 1) **IDENTIFIED** wrong type. 2) **IDENTIFIED** incomparable process-local clocks. 3) **IDENTIFIED** unused close Result. |
| Files modified | `tests/harness/r16_wal_init_harness.cpp`, `tests/harness/r19_observer_integrity.cpp`, `tests/harness/r19_raw_vs_prod.cpp`, `docs/security/PHANTOMX_PHASE15C11R19_OBSERVER_AND_CAUSAL_VALIDATION_REPORT.md` |
| Repair | 1) Pass ScenarioSpec. 2) Use CLOCK_MONOTONIC for child event intervals. 3) Not fixed in report; warnings only, test PASS. |
| Attempts | 1 per defect (2 code defects). |
| Original test | 1) g++ FAIL. 2) H HARNESS_FAILURE overlap_not_observed. 3) warnings. |
| Post-fix test | 1) build PASS, selftest PASS. 2) H overlap proven, 5/261 reproduced with integrity OK. 3) observer test PASS. |
| Regression | unit build PASS; 123/123 unit PASS; ctest 11/11 PASS; phantomxd PASS. ASAN/UBSAN/TSan NOT RUN. |
| Remaining issues | Observer A-J depth incomplete; negative matrix incomplete; F/G proof sampling-based; retry runtime not wired; raw-vs-prod limited; failure-path matrix missing; acceptance campaign NOT RUN; root cause NOT PROVEN. |

## 11. Regression (final R19 source state)
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS** (exit 0)
- `./build-ubuntu/tests/phantomx_unit_tests` — **123 passed, 0 failed, 123 total** (exit 0)
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS** (exit 0)
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS** (exit 0)

## 12. Sanitizer
- ASAN: **NOT RUN**
- UBSAN: **NOT RUN**
- TSan: **NOT RUN**

## 13. SQLite root-cause assessment
**NOT PROVEN / INVESTIGATING**. Reproduced 5/261 in H multi-process concurrent RW init (R18/R19 smoke) with overlap proof and integrity/reopen OK. Limited raw-vs-prod also showed 5/261 in raw fresh-concurrent init once, suggesting possible SQLite/FS/WAL concurrency behavior not exclusive to SqliteDb wrapper; still **NOT COMPARABLE** as full causal proof. No production fix authorized/applied.

## 14. Remaining blockers
- Observer A-J complete deterministic depth.
- Full negative harness matrix.
- F/G synchronized active-txn proof (not sampling-only).
- Real runtime retry/final API instrumentation.
- Comparable raw-vs-production matrix.
- Failure-path integrity matrix.
- 100-iter A-H and quota acceptance campaigns.
- R8-R12 historical reports missing.
- SQLite root cause NOT PROVEN.

## 15. Production/testnet/mainnet verdict
- Production testnet: **NO-GO**
- Mainnet: **NO-GO**

## 16. Phase 15C.12 readiness
**BLOCKED**. R19 gates incomplete; Phase 15C.11 PARTIAL.
