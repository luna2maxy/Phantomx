# PHASE B2-R6 — Production SQLite Path Audit (Phase 2, Read-Only)

**Status:** COMPLETE (Phase 2 only)  
**Date:** 2026-11-26  
**Mode:** STRICT READ-ONLY CODE INSPECTION — NO EXPERIMENTS, NO PRODUCTION CHANGES  
**Approval:** Owner explicitly approved B2-R6 Phase 1 and Phase 2 only.

## 1. Scope and method

Read-only inspection of current source (HEAD `d094596549a53eb0695527df06729eca41e737e9`, `encryption`, dirty 114 preserved). No files modified. No tests/experiments run.

Inspected files:
- `src/database/sqlite_db.cpp` (471 lines)
- `include/phantomx/database/sqlite_db.hpp` (157 lines)
- `src/blockchain/chain.cpp` (980 lines)
- `include/phantomx/blockchain/chain.hpp` (187 lines)
- `src/rpc/node_handlers.cpp` (3144 lines; targeted quota/register path)

## 2. Production SQLite implementation map

### 2.1 SqliteDb open / pragmas / flags

| Item | Observation | Source |
|---|---|---|
| RW open | `sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr)` | `src/database/sqlite_db.cpp:255-304` (open, line ~267 open_v2) |
| RO open | `sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr)` | `src/database/sqlite_db.cpp:306-343` |
| RW pragmas | `journal_mode=WAL`, `synchronous=FULL`, `foreign_keys=ON`, `temp_store=MEMORY`, `busy_timeout=5000` | `src/database/sqlite_db.cpp:278-283` |
| RO pragmas | `temp_store=MEMORY`, `busy_timeout=2000` | `src/database/sqlite_db.cpp:323-326` |
| Extended result codes | **No `sqlite3_extended_result_codes(db,1)` call found** in src/include/tests. `capture_diag` reads `sqlite3_extended_errcode()` but API may return primary unless extended codes enabled. | `src/database/sqlite_db.cpp:37-47`, grep `extended_result_codes` = no matches |
| Busy handler | No custom `sqlite3_busy_handler`; uses `busy_timeout` pragma only | grep no matches |
| Connection ownership | `SqliteDb` owns `sqlite3* db_`; move-only; destructor calls `close()` | `include/phantomx/database/sqlite_db.hpp:77-139`, `src/database/sqlite_db.cpp:239-253,360-375` |
| Thread safety | `SQLITE_OPEN_FULLMUTEX`; internal `mutable std::recursive_mutex mu_`; most ops take `scoped_lock`. Statement holds `mu_` ptr and locks on step/column/reset. | `sqlite_db.hpp:137-139`, `sqlite_db.cpp:170-237,377-423` |
| Error capture | `capture_diag` captures `rc`, `sqlite3_errcode`, `sqlite3_extended_errcode`, `sqlite3_errmsg` immediately after failing API under lock. `exec` captures before `sqlite3_free(err)`. | `src/database/sqlite_db.cpp:37-47,170-176,377-387` |

### 2.2 Transaction primitives

| Item | Observation | Source |
|---|---|---|
| Begin | `exec("BEGIN IMMEDIATE TRANSACTION;")` | `src/database/sqlite_db.cpp:401` |
| Commit | `exec("COMMIT;")` | `src/database/sqlite_db.cpp:402` |
| Rollback | `exec("ROLLBACK;")` | `src/database/sqlite_db.cpp:403` |
| TransactionGuard | RAII; destructor rolls back if active and not committed; no throw. | `sqlite_db.hpp:141-156`, `sqlite_db.cpp:442-470` |
| Error propagation | `exec` returns `ErrorCode::kIoError` with diag string `[primary=... extended=... rc=... api=...]` | `src/database/sqlite_db.cpp:24-35,377-387` |

### 2.3 ChainDatabase open/init/lifecycle

| Item | Observation | Source |
|---|---|---|
| Open | `SqliteDb::open(path)` → `init_schema()` → `recover_after_crash()` → `apply_genesis_if_needed()` | `src/blockchain/chain.cpp:89-105` |
| Schema init | Creates metadata/blocks/utxos/tx_index/undo/snapshots/verifier_challenges + indexes under `ChainDatabase` mutex; executes DDL via `db_.exec(...)` | `src/blockchain/chain.cpp:107-123` |
| Close | locks `mu_`, calls `db_.close()` | `src/blockchain/chain.cpp:105` |
| Connection scope | Single `db::SqliteDb db_` member owned by `ChainDatabase`; `mu_` guards ChainDatabase operations. | `chain.hpp:180-187` |

### 2.4 Production quota path (historical 261 concern area)

| Item | Observation | Source |
|---|---|---|
| RPC caller | `node_handlers.cpp` registers `verifier_register_challenge`; calls `chain.put_verifier_challenge_with_quota(...)` after building record. Maps errors by message string. | `src/rpc/node_handlers.cpp:1240-1315` (approx), call at ~1301 |
| ChainDatabase quota tx | `put_verifier_challenge_with_quota` takes `*mu_` lock, uses `TransactionGuard`, `BEGIN IMMEDIATE` via `tx.begin()`, cleanup+global count+identity count+insert+commit. Retry loop up to 20 attempts on `SQLITE_BUSY(5)` or `SQLITE_LOCKED(6)` or locked/busy message. Sleep `1 + attempt*2 ms`. | `src/blockchain/chain.cpp:164-399` |
| Busy classification | `is_busy_diag`: primary==5 or primary==6 or message contains "database is locked"/"database is busy". **Does not distinguish extended 261/517/262 except by primary.** | `src/blockchain/chain.cpp:213-218` |
| Commit ambiguity | On commit failure, performs lookup by `challenge_id` and verifies full record equality; returns success only if exact same record committed. No blind retry on ambiguity. | `src/blockchain/chain.cpp:338-388` |
| Diag observer | `QuotaTxDiagEvent` emitted to optional observer; captures primary/extended/rc/api/msg, original and rollback errors, tx state. | `chain.hpp:123-160`, `chain.cpp:179-250` |
| Connection observer | `SqliteDb` has global connection observer; emits open/init/close events. | `sqlite_db.hpp:36-75`, `sqlite_db.cpp:11-99` |

## 3. Observability and reproduction gaps

| Gap | Impact | Classification |
|---|---|---|
| `sqlite3_extended_result_codes()` not enabled | API calls may return primary codes; `extended_errcode()` is read but fidelity of 261/517/262 capture depends on SQLite behavior/mode. Affects evidence quality for 261 reproduction. | **HIGH (observability)** |
| No custom busy handler / no extended-busy telemetry at SqliteDb level | Cannot distinguish BUSY_RECOVERY/SNAPSHOT/LOCKED_SHAREDCACHE from all BUSY unless extended codes reliably exposed. | **HIGH** |
| Quota tx retries on primary 5/6 only | Correct by class but coarse; 261-specific behavior not separately measured. | **MEDIUM** |
| Single in-process ChainDatabase mutex + FULLMUTEX | Serializes same-process access strongly; cross-process/contention reproduction needs separate process DB openers. | **MEDIUM (repro design)** |
| No production-path test-only harness for ChainDatabase/SqliteDb | B2-R5 was raw sqlite only; production equivalence NOT EXECUTED. | **HIGH** |
| Commit ambiguity lookup occurs after commit failure outside tx guard commit state | Code appears careful, but needs production-path stress validation; no evidence of 261 here. | **MEDIUM** |
| Checkpoint policy not explicit in SqliteDb open | WAL autocheckpoint default used (not set). Checkpoint contention scenario underdocumented. | **MEDIUM** |
| Historical 5/261 evidence missing/unverifiable | Cannot confirm original failure context. | **HIGH (historical)** |

## 4. Candidate failure domains (not root-cause claims)

All **UNVERIFIED** — no reproduction evidence in B2-R6 Phase 2:

1. WAL recovery contention on first open/init across concurrent processes.
2. Concurrent initialization/DDL (`init_schema`) on same DB file.
3. Checkpoint interaction with writer/readers during quota tx.
4. Transaction lock contention (`BEGIN IMMEDIATE`) producing BUSY/261 in specific WAL recovery states.
5. Connection lifecycle/open-close races in multi-process tests.
6. Commit ambiguity/error-code handling misclassification (not same as 261).
7. Extended-result-code observability masking (261 occurring but recorded as 5) — **must be ruled out in harness design**.

## 5. Proposed B2-R6 reproduction matrix (Phase 3 — NOT AUTHORIZED)

**Do not execute without separate explicit owner approval.**

### 5.1 Harness layers

| Layer | Purpose | Equivalence |
|---|---|---|
| A. Raw SQLite control | Baseline with extended_result_codes=ON, same pragmas as prod | Raw only |
| B. SqliteDb wrapper harness | Uses `db::SqliteDb` open/begin/exec/prepare on disposable DB; read-only production code usage if possible via test target linking existing objects | Partial production-path |
| C. ChainDatabase quota-path harness | Uses `ChainDatabase::open` + `put_verifier_challenge_with_quota` on disposable DB; observer enabled | Production-path candidate |

### 5.2 Matrix (pilot 100, then escalate to max 1000 per stable config; max runtime 6h)

| ID | Layer | Crash/init point | Workers/procs | WAL/SHM | Checkpoint | Iterations |
|---|---|---|---:|---|---|---:|
| R6-A1 | A | fresh init | 4P | normal | default | 100→1000 |
| R6-A2 | A | post-commit crash WAL, noshm | 4P | noshm | none | 100→1000 |
| R6-A3 | A | post-commit crash WAL, shm kept | 4P | shm | none | 100→1000 |
| R6-A4 | A | mid-checkpoint crash (if feasible) | 4P | varied | PASSIVE/TRUNCATE | 100→1000 |
| R6-B1 | B | SqliteDb::open/init concurrent | 4P | normal | default | 100→1000 |
| R6-B2 | B | crash WAL + concurrent SqliteDb open | 4P | noshm/shm | default | 100→1000 |
| R6-C1 | C | ChainDatabase::open concurrent | 4P | normal | default | 100→1000 |
| R6-C2 | C | quota tx contention (same DB, multi-proc callers) | 4P | normal | default | 100→1000 |
| R6-C3 | C | crash-recovery + quota tx open | 4P | noshm/shm | default | 100→1000 |

All runs: disposable `/tmp/phantomx_b2r6_*`, unique IDs, immutable JSONL/trace, per-iter integrity+expected/actual rows, extended code capture, no overwrite.

### 5.3 Required evidence per iteration

- unique iteration/experiment ID, config, pid/ppid, monotonic start/end, op name, sqlite rc/primary/extended, sqlite3_errmsg, tx state, rollback outcome, retry count, final result.
- WAL/SHM size/state before/after.
- `PRAGMA integrity_check`, expected vs actual rows/counts.
- For crashes: crash point proof (code + runtime state), termination mode, exit status.

### 5.4 Stop conditions

- Any corruption/integrity failure: stop, preserve DB/WAL/SHM/trace.
- Resource exhaustion or host impact: stop.
- Evidence loss/overwrite risk: stop.
- Runtime > 6 hours: stop.

## 6. Exact commands and expected artifacts (Phase 3 proposal only)

**Not executed.** Proposed only after owner approval:

```bash
# Example only - DO NOT RUN without approval
mkdir -p /tmp/phantomx_b2r6_out
# build test-only harness linking existing build-ubuntu objects; no production source changes
# run pilot: /home/ubuntu/b2r6_harness --pilot 100 --max 1000 --max-hours 6 > /tmp/b2r6_run.log 2>&1
```

Expected artifacts (if approved later):
- `/tmp/phantomx_b2r6_out/*.jsonl`, `*.trace`, `summary.csv`, `independent_audit.csv`
- `docs/reports/PHASE_B2R6_BASELINE_AND_EVIDENCE_INVENTORY.md` (this phase, done)
- `docs/reports/PHASE_B2R6_PRODUCTION_SQLITE_PATH_AUDIT.md` (this phase, done)
- `docs/reports/PHASE_B2R6_REPRODUCTION_REPORT.md` (Phase 3, not authorized)
- `docs/reports/PHASE_B2R6_ROOT_CAUSE_REPORT.md` (Phase 5, not authorized)

## 7. Risks and required approvals

| Risk | Mitigation | Approval needed |
|---|---|---|
| Extended result code not enabled in production SqliteDb may mask 261 | Harness must capture both primary/extended and explicitly test with extended_result_codes in control; document gap; do not change production in Phase 3. | Owner review of observability gap |
| Production-path harness may require linking build objects | Use existing build-ubuntu objects; no production source edits. If impossible without source change, report NOT EXECUTED. | Owner approval before build/run |
| Long-running stress can affect host | 6h cap, pilot-first, stop conditions, disposable /tmp only. | Owner approval for runtime/resource envelope |
| False "not reproduced" conclusion | Keep root cause UNVERIFIED; do not claim fix/resolution. | N/A - mandatory |
| Dirty worktree/build object drift | Record build object hashes/timestamps; use isolated build dir if needed without touching source. | Owner approval if new build dir needed |

## 8. Phase 2 compliance

- Production source inspected read-only: **YES**
- Production source/config modified: **NO**
- SQLite behavior changed: **NO**
- Experiments run: **NO**
- B2-R5 evidence touched: **NO**
- Destructive git commands: **NO**
- Worktree preservation: **PASS**

**STOP: Phase 1 and Phase 2 complete. Awaiting independent owner review before any Phase 3 reproduction matrix execution.**