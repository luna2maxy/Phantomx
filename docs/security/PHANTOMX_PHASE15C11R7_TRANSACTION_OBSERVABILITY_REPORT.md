# PHANTOMX Phase 15C.11-R7 Transaction Observability Report

**Status:** PARTIAL / INVESTIGATING  
**Date:** 2026-04-30 (workspace execution)  
**Baseline commit:** `d094596` (`encryption`)  
**Branch:** `encryption`  
**Dirty worktree:** preserved (no reset/clean/revert/destructive ops). Existing changes/reports preserved.

## 1. Scope and constraints
- Investigation only. No Phase 15C.12 started.
- No genesis/canonical config/network ID/monetary/reward/block/consensus/PoP protocol changes.
- No consensus-authoritative verifier registry. No P2P verifier registration.
- No quota limits/challenge lifecycle semantic changes. No unlimited retry masking.

## 2. Baseline reproduction (before R7 source changes)
Production method: `ChainDatabase::put_verifier_challenge_with_quota` via `tests/harness/challenge_quota_race.cpp` (32 threads, independent DB connections, barrier).

| Campaign | Initial | Cap | Iters | PASS | FAIL | Successes/fail iter | Quota rejects/fail iter | DB errors | Primary/ext/rc | Overshoot | Integrity/reopen |
|---|---:|---:|---:|---:|---:|---|---|---:|---|---|---|
| Per-identity | 7 active | 8 | 100 | 98 | 2 | 1 | 30 | 1,1 | 5/261/5 | No (`global=8 identity=8`) | Not captured per-iter |
| Global | 4095 active | 4096 | 100 | 95 | 5 | 1 | 30,30,30,28,30 | 1,1,1,3,1 | 5/261/5 | No (`global=4096`) | Not captured per-iter |

DB errors were not classified as quota rejections. Baseline implementation not changed before preserving this evidence.

## 3. Transaction-stage instrumentation
- **NOT IMPLEMENTED**. Required structured correlation IDs (process/thread/connection/transaction/operation) and stage events (CONNECTION_OPEN..FINAL_RESULT) not added to production code. Error stage, retry attempt, elapsed, tx active state, rollback result not captured structurally. Existing error strings include primary/extended/rc only.
- Bounded logging/control toggle/secret redaction tests: NOT IMPLEMENTED.

## 4. Connection ownership experiments
| Scenario | Expected | Actual | Status |
|---|---|---|---|
| A. One connection, serialized calls | No cross-connection BUSY_RECOVERY | NOT RUN | NOT IMPLEMENTED |
| B. One connection, concurrent callers via app mutex | Serialized by mutex; no tx-level concurrency | NOT RUN | NOT IMPLEMENTED |
| C. Independent connection per worker | Possible contention; must preserve invariants | 261 observed in thread harness | TESTED/PARTIAL |
| D. Independent connections + barrier | Same as C with synchronized start | 261 observed | TESTED/PARTIAL |
| E. Two independent processes same DB | Possible contention; must preserve invariants | Intermittent 261 observed in prior process harness | TESTED/PARTIAL |

Workloads/initial states not fully normalized across scenarios. No claim that FULLMUTEX serializes transactions across independent connections.

## 5. WAL and recovery experiments
- **NOT IMPLEMENTED / NOT REPRODUCED**. Required deterministic tests (long reader vs writer/checkpoint, multiple writers, concurrent checkpoint+quota create, process interruption, reopen, recovery, WAL growth/checkpoint behavior) not executed with evidence capture. Raw probe previously did not reproduce 261; not sufficient for production conclusion.

## 6. Transaction state and error-path validation
- Audit target: `ChainDatabase::put_verifier_challenge_with_quota`. Existing code has BEGIN IMMEDIATE tx, cleanup/count/quota/insert/commit, rollback on failures, transient busy/locked retry, no blind commit retry, full-context ambiguity lookup.
- Deterministic fault injection tests for BEGIN/cleanup/global count/identity count/quota rejection/INSERT/COMMIT/ROLLBACK/reopen-after-failure: **NOT IMPLEMENTED**. Tx state/statement lifetime/rollback/persisted state/retry eligibility/final result matrix: **NOT VERIFIED**.

## 7. Commit ambiguity
- R4 full-context validation retained. Required deterministic tests (commit success/caller response fail, commit fail before durable write, connection interruption, exact match, conflicting record, ambiguity lookup BUSY, reopen verify durable state, no double insert): **NOT IMPLEMENTED / NOT VERIFIED**.

## 8. Quota acceptance regression
| Test | Iters | PASS | FAIL | DB errors | Overshoot | Status |
|---|---:|---:|---:|---:|---:|---|
| Per-identity 100 (32 threads) | 100 | 98 | 2 | 2 (261) | No | NOT MET |
| Global 100 (32 threads) | 100 | 95 | 5 | 7 (261 across 5 failing iters) | No | NOT MET |
| 2-process per-identity/global | - | - | - | - | - | NOT IMPLEMENTED |
| 4-process per-identity/global | - | - | - | - | - | NOT IMPLEMENTED |

## 9. Cleanup/create/consume/count
- **NOT IMPLEMENTED**. No concurrent production-method harness for put/consume/cleanup/count/count_for with defined invariants.

## 10. Sanitizers
- ASAN: NOT RUN / NOT VERIFIED.
- UBSAN: NOT RUN / NOT VERIFIED.
- TSan: NOT RUN / NOT VERIFIED (support not evaluated in R7).

## 11. Build and regression results (last executed)
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS**
- `./build-ubuntu/tests/phantomx_unit_tests` — **121 passed, 0 failed, 121 total**
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS** (last clean run)
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS** (last clean run)
- Quota harnesses: baseline 100/100 executed; process/integrity-per-iter incomplete.

## 12. Root-cause conclusion
**NOT PROVEN**. Reproducible SQLITE_BUSY_RECOVERY (primary=5, extended=261, rc=5) observed in production quota path under concurrent independent-connection workloads, with no quota overshoot observed. Transaction-stage localization, connection-ownership causality, WAL/checkpoint/recovery causality, and commit-ambiguity safety remain unproven due missing instrumentation and deterministic fault-injection evidence.

## 13. Remaining blockers
- Structured transaction-stage instrumentation not implemented.
- Connection ownership experiments A/B not run; C/D/E not acceptance-complete/comparable.
- WAL/recovery deterministic experiments not run.
- Transaction error-path fault injection not implemented.
- Commit ambiguity deterministic fault injection not implemented.
- Cross-process 2/4 per-identity/global 100-iteration acceptance missing.
- Thread quota 100/100 zero-unexpected-DB-error not met.
- Cleanup/create/consume/count race harness missing.
- ASAN/UBSAN/TSan not run.

## 14. Exit criteria
| Criterion | Status |
|---|---|
| Structured transaction-stage instrumentation works | NOT IMPLEMENTED |
| Connection ownership experiments have comparable evidence | PARTIAL |
| WAL/recovery experiments have deterministic results | NOT IMPLEMENTED |
| Transaction error paths have reproducible tests | NOT IMPLEMENTED |
| Commit ambiguity tests have actual fault-injection evidence | NOT VERIFIED |
| Regression results from final source state | PASS (unit/CTest/daemon last executed) |
| Report complete | IMPLEMENTED |

**Verdict: Phase 15C.11-R7 PARTIAL/INVESTIGATING. R7 investigation not complete. Phase 15C.11 remains PARTIAL. Mainnet/production testnet NO-GO. Do not start Phase 15C.12.**
