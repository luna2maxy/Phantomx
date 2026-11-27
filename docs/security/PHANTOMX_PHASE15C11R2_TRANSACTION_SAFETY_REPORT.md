# PHANTOMX Phase 15C.11-R2 Transaction Safety Report

**Status:** PARTIAL  
**Date:** 2026-04-30 (workspace execution)  
**Baseline commit:** `d094596` (`encryption`)  
**Worktree:** dirty, preserved.

## 1. Baseline
- Prior 15C.11-R1: per-identity 92/100, global 97/100; intermittent SQLite lock contention; extended error classification/commit ambiguity not conclusive; cross-process quota creation/failure injection missing.
- Baseline tests this phase start: 121/121 unit PASS, 11/11 CTest PASS.

## 2. SQLite error instrumentation
- `src/database/sqlite_db.cpp` now includes `primary`, `extended`, `rc` in sqlite exec errors and primary/extended in `db_error`. 
- Observed failure captured: `io_error: sqlite exec: database is locked [primary=5 extended=261 rc=5]` (SQLITE_BUSY / extended 261). 
- Full classification matrix (BUSY_TIMEOUT/LOCKED/LOCKED_SHAREDCACHE/CONSTRAINT/IOERR/FULL/READONLY/CORRUPT/NOTADB) **NOT VERIFIED** by controlled injection.

## 3. Transaction state machine audit
`ChainDatabase::put_verifier_challenge_with_quota`:
1. BEGIN IMMEDIATE tx. Failure: retry only transient busy/locked before tx effects; else fail.
2. Cleanup/count/quota/insert statements. Failure: rollback; retry only transient busy/locked; non-transient fails closed.
3. COMMIT. On error, check whether `challenge_id` exists to detect committed-despite-error. If exists, return success; if busy/locked, return `challenge quota commit ambiguous/busy` fail-closed; otherwise return commit error.
4. ROLLBACK on any non-retry failure path.

No success returned before COMMIT unless post-commit existence check confirms record. **PARTIAL** - commit ambiguity handled conservatively, but not fully proven by fault injection.

## 4. Root cause evidence
- Observed root symptom: SQLITE_BUSY (primary=5, extended=261) during concurrent independent-connection quota transactions. 
- Likely contributors: concurrent BEGIN IMMEDIATE/write contention across 32 independent connections on same SQLite DB; busy_timeout alone insufficient under burst.
- Root cause **not conclusively proven** to a single code path; instrumentation improved but full causal trace per failure not captured. **PARTIAL/INVESTIGATING**.

## 5. Commit ambiguity findings
- Blind retry after commit removed. On commit error: existence check by `challenge_id`; busy/locked becomes fail-closed ambiguous error. 
- Not proven by injected commit-failure tests. **NOT VERIFIED**.

## 6. Controlled lock tests
- **NOT IMPLEMENTED**. No dedicated two-connection controlled write-lock/busy-timeout/retry-after-rollback harness added.

## 7. Failure injection
- **NOT IMPLEMENTED / NOT VERIFIED**. No SQLite-native controlled injection for insert/cleanup/read-only/unavailable/constraint/rollback/commit/disk-full/corrupt.

## 8. Cross-process creation
- **NOT IMPLEMENTED**. `tests/harness/challenge_quota_process_race.cpp` not created; no 2-process/4-process per-identity/global quota process evidence.

## 9. Quota thread regression
### Per-identity (7 active, 32 threads, independent connections, 100 iterations)
- **99 PASS / 1 FAIL**. Failure: iter 97, `ok=1 reject=30 err=1 global=8 identity=8`, err `primary=5 extended=261 rc=5`. No overshoot observed.
- Acceptance 100/100 without DB error: **NOT MET**.

### Global (4095 active, 32 threads, distinct identities, 100 iterations)
- **97 PASS / 3 FAIL**. Failures: iter 26/63/82, DB errors with `primary=5 extended=261`; no overshoot observed (`global=4096`).
- Acceptance 100/100 without DB error: **NOT MET**.

## 10. Final builds/tests
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS**
- `./build-ubuntu/tests/phantomx_unit_tests` — **121 passed, 0 failed, 121 total** (prior baseline; unchanged by harness-only runs)
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS** (0.75s)
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS**

## 11. Files changed
- `src/database/sqlite_db.cpp` — extended SQLite error codes in exec/db_error messages.
- `src/blockchain/chain.cpp` — busy/locked retry by primary code, commit ambiguity existence check/fail-closed, backoff.
- `tests/harness/challenge_quota_race.cpp` — open-db error capture.
- `docs/security/PHANTOMX_PHASE15C11R2_TRANSACTION_SAFETY_REPORT.md` — this report.

## 12. Remaining risks
- Intermittent SQLITE_BUSY (5/261) still causes acceptance failure; no overshoot observed but DB errors violate acceptance.
- Root cause not conclusively proven.
- Commit ambiguity handling not fault-injection verified.
- Controlled lock/failure injection/cross-process quota creation missing.
- ASAN/UBSAN/TSan not run.
- Registry BLOCKED; PX-CONS-0001 INVESTIGATING.

## 13. Acceptance criteria status
| Criterion | Status |
|---|---|
| Root cause lock contention proven | PARTIAL |
| Error codes classified accurately | PARTIAL |
| Transaction lifecycle safe | PARTIAL |
| Commit ambiguity handled without blind retry | IMPLEMENTED/PARTIAL |
| Controlled lock tests pass | NOT IMPLEMENTED |
| Reproducible failure handling passes | NOT IMPLEMENTED |
| Cross-process quota creation tested | NOT IMPLEMENTED |
| Thread quota tests 100/100 no DB error | NOT MET (99/100, 97/100) |
| Final unit/CTest/daemon PASS | PASS |
| Evidence report available | IMPLEMENTED |

**Verdict: Phase 15C.11-R2 PARTIAL. Mainnet/Production testnet NO-GO.**
