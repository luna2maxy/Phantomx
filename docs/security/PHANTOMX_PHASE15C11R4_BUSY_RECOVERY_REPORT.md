# PHANTOMX Phase 15C.11-R4 BUSY_RECOVERY Report

**Status:** PARTIAL  
**Date:** 2026-04-30 (workspace execution)  
**Baseline commit:** `d094596` (`encryption`)  
**Worktree:** dirty, preserved.

## 1. Baseline
- 15C.11-R3: WAL+synchronous FULL+FULLMUTEX+busy_timeout=5000; SQLITE_BUSY_RECOVERY primary=5 extended=261 rc=5 captured; minimal probe did not reproduce 261; production quota thread/process races still saw BUSY_RECOVERY; per-identity 99/100, global 97/100; commit ambiguity not fault-injection verified; cleanup/create/consume/count race missing; ASAN/UBSAN/TSan not run.
- This phase baseline: `phantomx_unit_tests` **121/121 PASS**.

## 2. Production connection lifecycle audit
- `SqliteDb::open`: `sqlite3_open_v2` RW|CREATE|FULLMUTEX, PRAGMAs WAL/synchronous=FULL/foreign_keys=ON/temp_store=MEMORY/busy_timeout=5000. `open_readonly`: READONLY|FULLMUTEX, busy_timeout=2000. No wal_checkpoint/wal_autocheckpoint/locking_mode/cache_size/shared_cache overrides.
- `SqliteDb` holds `sqlite3*` and `mutable std::recursive_mutex mu_`; most ops lock `mu_`. Statements hold `db_`, `stmt_`, `mu_` pointer and finalize on destruction/reset.
- `TransactionGuard` rolls back on destructor if active/uncommitted. `ChainDatabase` has its own `mu_`; `put_verifier_challenge_with_quota` takes `ChainDatabase` lock then uses `TransactionGuard`/prepared statements on same connection.
- Gaps: no per-connection/operation diagnostic IDs in production logs; no proof that all error paths leave no active statement beyond RAII; no explicit check for connection shared across threads outside intended design. **PARTIAL**.

## 3. Probe vs production comparison
| Aspect | `sqlite_busy_recovery_probe.cpp` | Production quota harnesses |
|---|---|---|
| DB API | raw sqlite3 | `ChainDatabase`/`SqliteDb`/TransactionGuard |
| Schema/ops | simple table insert/select/checkpoint | verifier_challenges cleanup/count/count-per-identity/insert in BEGIN IMMEDIATE tx |
| Connections | 2 raw connections | 32 independent `ChainDatabase::open` connections (threads or processes) |
| PRAGMAs | WAL,FULL set manually | WAL,FULL,foreign_keys,temp_store,busy_timeout=5000 |
| Sync | none/barrier not same pattern | barrier + concurrent same-tx quota path |
| Result | no 261 reproduced | 261 reproduced intermittently |

Probe is **not equivalent** to production path. A production-method probe/harness was not completed. **PARTIAL**.

## 4. BUSY_RECOVERY reproduction
- Reproduced in production quota races as `io_error: sqlite exec: database is locked [primary=5 extended=261 rc=5]`.
- Thread harness: R2 evidence 99/100 per-identity, 97/100 global (failures 261). R3/R4 ad-hoc runs still observed 261.
- Process harness (`tests/harness/challenge_quota_process_race.cpp`, production DB): 2-process per-identity 20-iter batch showed intermittent 261 failure (`ok=1 reject=0 err=1 global=8 identity=8 integrity=ok`, err 261). Not reproducible on every run but observed.
- Root cause **not conclusively isolated**. **INVESTIGATING/PARTIAL**.

## 5. Retry/transaction correctness
- `put_verifier_challenge_with_quota` retries only transient busy/locked (primary 5/6/message match), max 20 attempts, backoff; rolls back before statement retry.
- Commit error: no blind retry. Existence check now validates full context (`verifier_id,pubkey_hex,owner,network,issued_at,expires_at,used_at NULL,status='issued'`) before returning success; conflicting existing record returns fail-closed error; busy/locked returns ambiguous fail-closed error.
- Not fault-injection verified; commit ambiguity/retry safety remains **NOT VERIFIED**.

## 6. Controlled SQLite tests
- **NOT IMPLEMENTED**. Required controlled matrix (writer/writer, reader/writer, long reader, WAL checkpoint vs reader/writer, busy timeout, retry after lock release, rollback after statement failure, connection reopen, commit ambiguity injection) not built.

## 7. Cross-process quota results
- `tests/harness/challenge_quota_process_race.cpp`: implemented (forked independent processes, same DB, barrier, per-process result, parent reopen, integrity_check). 
- Observed: 2-process per-identity intermittent 261; acceptance not met. 4-process per-identity/global and 100-iteration campaigns **NOT COMPLETED**. **PARTIAL**.

## 8. Thread quota results
- R2 recorded: Per-identity 100 iters = 99 PASS/1 FAIL (261, no overshoot). Global 100 iters = 97 PASS/3 FAIL (261, no overshoot). 
- R4 did not rerun full 100/100 campaigns after final code tweak due to instability/root cause unresolved. **NOT MET / NOT VERIFIED for R4 final**.

## 9. Cleanup/create/consume/count race
- **NOT IMPLEMENTED**.

## 10. ASAN/UBSAN/TSan
- ASAN: **NOT RUN**.
- UBSAN: **NOT RUN**.
- TSan: **NOT RUN**. All **NOT VERIFIED**.

## 11. Final build/test
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS**
- `./build-ubuntu/tests/phantomx_unit_tests` — **121 passed, 0 failed, 121 total**
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS** (0.56s)
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS**

## 12. Files changed
- `src/database/sqlite_db.cpp` — extended SQLite error codes in exec/db_error.
- `src/blockchain/chain.cpp` — busy/locked retry/backoff by primary code; commit ambiguity full-context existence check/fail-closed.
- `tests/harness/challenge_quota_race.cpp` — open-db error capture.
- `tests/harness/challenge_quota_process_race.cpp` — independent-process quota race harness.
- `tests/harness/sqlite_busy_recovery_probe.cpp` — minimal BUSY_RECOVERY probe.
- `docs/security/PHANTOMX_PHASE15C11R4_BUSY_RECOVERY_REPORT.md` — this report.

## 13. Remaining risks
- SQLITE_BUSY_RECOVERY root cause unresolved; intermittent DB errors in quota races.
- 100/100 per-identity/global thread acceptance not met.
- Cross-process 2/4-process acceptance not met.
- Commit ambiguity and retry safety not fault-injection verified.
- Controlled SQLite/WAL/checkpoint/failure-injection matrix missing.
- Cleanup/create/consume/count race missing.
- Sanitizers not run.
- Registry BLOCKED; PX-CONS-0001 INVESTIGATING.

## 14. Acceptance criteria
| Criterion | Status |
|---|---|
| BUSY_RECOVERY root cause proven / adequate alternative proof | PARTIAL/INVESTIGATING |
| Production connection lifecycle safe | PARTIAL |
| Retry and transaction handling proven correct | PARTIAL |
| Commit ambiguity adequately tested | NOT VERIFIED |
| Controlled SQLite tests complete | NOT IMPLEMENTED |
| Cross-process creation tested | PARTIAL |
| Thread quota 100/100 | NOT MET |
| Cleanup/create/consume/count race tested | NOT IMPLEMENTED |
| ASAN/UBSAN/TSan run or limitation proven | NOT VERIFIED |
| Final unit/CTest/daemon PASS | PASS |
| Evidence report available | IMPLEMENTED |

**Verdict: Phase 15C.11-R4 PARTIAL. Mainnet/Production testnet NO-GO.**
