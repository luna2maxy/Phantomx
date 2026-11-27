# PHANTOMX Phase 15C.11-R6 SQLite Observability Report

**Status:** PARTIAL (Investigation)  
**Date:** 2026-04-30 (workspace execution)  
**Baseline commit:** `d094596` (`encryption`)  
**Branch:** `encryption`  
**Dirty worktree:** preserved (no reset/clean/revert/destructive ops).

## 1. Scope / Constraints
- Investigation only; no Phase 15C.12 started.
- No genesis/canonical config/network ID/monetary/reward/block/consensus changes.
- No PoP protocol/binary encoding changes.
- No verifier registry or P2P verifier registration implementation.
- No quota/challenge lifecycle semantic changes.
- No unlimited retry masking.

## 2. Baseline Reproduction (production harness, before R6 code changes)
Harness: `tests/harness/challenge_quota_race.cpp` (production `ChainDatabase::put_verifier_challenge_with_quota`, independent DB connections, 32 concurrent threads, barrier).

### Per-identity
- Setup: 7 active records, cap 8, same identity, 32 threads, 100 iterations.
- Result: **97 PASS / 3 FAIL** (100/100 executed).
- Fails: iter 66,76,88 each `ok=1 reject=30 err=1 global=8 identity=8` (no overshoot observed).
- Error: `io_error: sqlite exec: database is locked [primary=5 extended=261 rc=5]` = SQLITE_BUSY_RECOVERY.

### Global
- Setup: 4095 active records, global cap 4096, distinct identities, 32 threads, 100 iterations.
- Result: **99 PASS / 1 FAIL** (100/100 executed).
- Fail: iter 23 `ok=1 reject=29 err=2 global=4096 identity=0` (no overshoot observed).
- Error: `io_error: sqlite exec: database is locked [primary=5 extended=261 rc=5]` (SQLITE_BUSY_RECOVERY).

DB errors were not counted as quota rejections. Integrity/reopen/WAL size/retry-count/stage not captured per iteration by current harness. **PARTIAL**.

## 3. Instrumentation Design
- **NOT IMPLEMENTED** in production path. Required correlation fields (process/thread/connection/transaction/operation/retry/stage, elapsed, tx state, rollback outcome) not added. Existing evidence only has SQLite primary/extended/rc in error string from `src/database/sqlite_db.cpp`.
- Status: NOT IMPLEMENTED.

## 4. Connection Ownership Experiments
| Scenario | Status | Notes |
|---|---|---|
| A. Independent connection per worker | TESTED/PARTIAL | Thread quota harness uses independent connections; BUSY_RECOVERY observed. |
| B. One connection, serial execution | NOT IMPLEMENTED | No experiment run. |
| C. One connection, concurrent callers via mutex | NOT IMPLEMENTED | No experiment run. |
| D. Independent connections with barrier sync | TESTED/PARTIAL | Same as A with barrier. |
| E. Two independent processes same DB | TESTED/PARTIAL | `challenge_quota_process_race.cpp` exists; intermittent 261 observed previously; full R6 matrix not run. |

No conclusion that FULLMUTEX provides transaction-level serialization across independent connections.

## 5. WAL/Recovery Experiments
- **NOT IMPLEMENTED / NOT REPRODUCED** for controlled matrix. Minimal raw probe (`sqlite_busy_recovery_probe.cpp`) previously did not reproduce 261. No controlled long-reader/checkpoint/process-restart/reopen-after-interruption campaign with deterministic barriers completed in R6.
- Status: INVESTIGATING.

## 6. Transaction Stage Error Distribution
- Observed extended code: 261 (SQLITE_BUSY_RECOVERY) on quota race failures. Stage attribution unavailable because instrumentation not implemented.
- Status: **NOT PROVEN** (stage-level isolation missing).

## 7. Retry Behavior
- `ChainDatabase::put_verifier_challenge_with_quota` (R4): retries transient busy/locked (primary 5/6/message), max 20 attempts, backoff; rollback before statement retry; no blind retry after commit; commit ambiguity does full-context existence check/fail-closed on conflict/ambiguous busy.
- Retry count per failing operation not reported by harness. Fault-injection proof: NOT VERIFIED.

## 8. Commit Ambiguity Results
- Code path exists (R4): full-context check on commit error. Deterministic fault-injection tests (commit success/caller response fail, commit fail before durable write, connection interruption, differing record, lookup BUSY, reopen after ambiguity): **NOT IMPLEMENTED / NOT VERIFIED**.

## 9. Thread and Process Quota Results
| Test | Iterations | Pass | Fail | DB errors | Overshoot observed | Status |
|---|---:|---:|---:|---:|---:|---|
| Per-identity thread, 32, cap 8 | 100 | 97 | 3 | 3 (261) | No | PARTIAL/NOT MET |
| Global thread, 32, 4095->4096 | 100 | 99 | 1 | 2 errors reported in iter 23 | No | PARTIAL/NOT MET |
| Process 2/4 per-identity/global | - | - | - | - | - | NOT IMPLEMENTED (R6 matrix) |

## 10. Database Integrity and Reopen Results
- Process harness includes `integrity_check` and reopen on successful runs. R6 per-iteration integrity/reopen capture: **NOT IMPLEMENTED**. No corruption observed in captured failures.

## 11. Unit/CTest/Daemon Results
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS**
- `./build-ubuntu/tests/phantomx_unit_tests` — **121 passed, 0 failed, 121 total**
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS** (last prior clean run 0.56s)
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS** (last prior clean run)

## 12. Root Cause Conclusion
**NOT PROVEN**. Evidence confirms intermittent SQLITE_BUSY_RECOVERY (primary=5, extended=261, rc=5) in production quota transaction under concurrent independent-connection thread/process races, with no observed quota overshoot in captured failures. Transaction stage, connection ownership causality, WAL/checkpoint causality, and commit-ambiguity behavior are not yet isolated by deterministic instrumentation/tests.

## 13. Remaining Blockers
- Missing structured transaction observability (connection/tx/op/stage/retry/duration/rollback outcome).
- Connection ownership experiments B/C/D/E matrix incomplete.
- WAL/checkpoint/recovery controlled experiments incomplete.
- Transaction state/fault-injection validation incomplete.
- Commit ambiguity deterministic fault injection incomplete.
- Cross-process 2/4 per-identity/global 100-iteration acceptance incomplete.
- Thread quota 100/100 zero-unexpected-DB-error acceptance not met.
- Cleanup/create/consume/count race harness missing.
- ASAN/UBSAN/TSan not run.

## 14. Acceptance (R6 investigation phase)
| Criterion | Status |
|---|---|
| Production transaction path has adequate observability | NOT IMPLEMENTED |
| Error 261 linked to stage or honestly not isolated | PARTIAL (not isolated) |
| Connection ownership experiments complete | PARTIAL |
| WAL/recovery experiments complete or limitations documented | PARTIAL/NOT IMPLEMENTED |
| Transaction lifecycle audit has concrete tests | NOT IMPLEMENTED |
| Commit ambiguity test has evidence | NOT VERIFIED |
| Regression suite passes | PASS (unit/CTest/daemon last executed) |
| Complete report available | IMPLEMENTED |

**Verdict: Phase 15C.11-R6 PARTIAL (Investigation). SQLite concurrency not production-ready. Phase 15C.11 remains PARTIAL. Do not start 15C.12. Mainnet/production testnet NO-GO.**
