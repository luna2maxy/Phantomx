# PHANTOMX Phase 15C.11-R13 Controlled WAL Initialization Experiment Report

**Status:** PARTIAL / NOT PROVEN  
**Date:** 2026-04-30 (workspace execution)  
**Baseline:** `d094596` (`encryption`)  
**Worktree:** dirty, preserved (106 entries in `git status --short` before R13 edits; no reset/clean/revert/destructive ops).  

## 1. Preservation evidence
- Commands recorded: `git rev-parse --short HEAD` => `d094596`; `git branch --show-current` => `encryption`; `git status --short | wc -l` => `106`; inventory captured in `/tmp/r13_git_status.txt`.
- No genesis/canonical config/network ID/monetary rules/reward economics/block format/consensus semantics/PoP format/verifier lifecycle/challenge quota semantics/registry design changes.
- No production changes to `journal_mode`, `synchronous`, `busy_timeout`, transaction policy, or connection ownership.
- No Phase 15C.12 work.

## 2. R8–R12 evidence references (existing, not fabricated)
- `docs/security/PHANTOMX_PHASE15C11R1_SQLITE_LOCK_REPORT.md` exists.
- `docs/security/PHANTOMX_PHASE15C11R2_TRANSACTION_SAFETY_REPORT.md` exists.
- `docs/security/PHANTOMX_PHASE15C11R4_BUSY_RECOVERY_REPORT.md` exists.
- `docs/security/PHANTOMX_PHASE15C11R6_SQLITE_OBSERVABILITY_REPORT.md` exists.
- `docs/security/PHANTOMX_PHASE15C11R7_TRANSACTION_OBSERVABILITY_REPORT.md` exists (PARTIAL).
- Missing/not created in prior phases: R3, R5, R8, R9, R10, R11, R12 reports. **NOT IMPLEMENTED / NOT VERIFIED** - absence recorded, not retroactively claimed.

Known preserved evidence:
- R11 isolated reproduction: `SQLITE_BUSY_RECOVERY` primary=5 extended=261 rc=5, `sqlite3_exec`, `INITIALIZATION_OPERATION_FAILURE`, `operation_name=PRAGMA journal_mode=WAL`, op seq 1, RW, 32 threads independent connections; 20-iter campaign 19 PASS/1 FAIL, 5 DB errors, no quota overshoot, integrity_check OK.
- R12 10-iter sanity: 10/10 PASS, 0 DB errors - **not proof of resolution**.

## 3. Caller audit (source-backed, PARTIAL / NOT VERIFIED exhaustive)

### Search scope
`rg`/grep for `sqlite3_open`, `sqlite3_open_v2`, `sqlite3_exec`, `PRAGMA journal_mode`, `SqliteDb::open`, `SqliteDb::open_readonly`.

### Findings table

| Source file:function | Prod/Test | DB role | RW mode | journal_mode=WAL executed | Already WAL possible | Concurrent init possible | Thread/process ownership | Open/reopen frequency | Status |
|---|---|---|---|---|---|---|---|---|---|
| `src/database/sqlite_db.cpp:SqliteDb::open` | Production | chain/db generic | RW/CREATE/FULLMUTEX | Yes, op seq 1 via sqlite3_exec | Yes, PRAGMA still executed | Yes, caller-dependent | Caller-owned connection object | Per open call | TESTED by source |
| `src/database/sqlite_db.cpp:SqliteDb::open_readonly` | Production | RO | READONLY/FULLMUTEX | No (temp_store, busy_timeout=2000) | N/A | Yes | Caller-owned | Per open call | TESTED by source |
| `src/database/sqlite_db.cpp:SqliteDb::backup_to` | Production | backup dest | RW/CREATE | No PRAGMA init on dest | N/A | Unusual | caller thread | on backup | PARTIAL |
| `src/blockchain/chain.cpp:ChainDatabase::open` | Production | chain | RW via SqliteDb::open | Yes via factory | Yes | Possible if called concurrently | caller; ChainDatabase holds mutex | open/reopen/restore | PARTIAL |
| `src/blockchain/chain.cpp` restore/snapshot/backup-integrity open paths | Production | chain/backup | RW via SqliteDb::open | Yes via factory | Yes | operation-local | operation-local | on operation | PARTIAL |
| `src/aggregator/aggregator.cpp` | Production | aggregator | RW via SqliteDb::open | Yes via factory | Yes | unknown | unknown | on open | NOT VERIFIED |
| `apps/phantomx-qt/WalletService.cpp` | Production app | wallet read | RO via SqliteDb::open_readonly | No | N/A | possible | UI/app thread | on open | PARTIAL |
| `tests/harness/sqlite_busy_recovery_probe.cpp` | Test-only | probe | RW raw sqlite3_open_v2 | Yes raw exec | Yes | test-controlled | test threads | test-only | TESTED by source |
| `tests/harness/r8_stage_capture.cpp` / R13 harness | Test-only | quota harness | RW via SqliteDb::open/ChainDatabase | Yes via factory | Yes/controlled | Yes | harness threads | per iter/worker | TESTED |

Audit completeness: **NOT VERIFIED**. Grep indicates no other production `sqlite3_open_v2` bypass, but not every caller/startup ordering/thread-process ownership path was proven.

## 4. SQLite version and compile options
- Python sqlite3 module: `3.46.1` (used for compile option capture).
- Compile options: ATOMIC_INTRINSICS=1, THREADSAFE=1, DEFAULT_SYNCHRONOUS=2, DEFAULT_WAL_SYNCHRONOUS=2, DEFAULT_WAL_AUTOCHECKPOINT=1000, MAX_MMAP_SIZE=0x7fff0000, TEMP_STORE=1, etc. (full list captured in shell output).
- Harness linked against system `-lsqlite3` same environment.

## 5. Experimental design and initial DB state
- Harness: `tests/harness/r13_wal_init_matrix.cpp` (new, test-only). Uses production `SqliteDb::open` and connection observer. Captures per-connection init events, op/stage/api/rc/primary/extended/elapsed, integrity_check, journal_mode and db/wal/shm sizes in per-iter JSON. Records failing-iteration events.
- Limitations: does not capture full required metadata for every event (SQLite compile options per run row, before/after journal mode at every event, active reader/writer state timeline, retry count/final API result as structured field beyond error strings, close/reopen/persistence/schema verification per scenario beyond integrity/openability in some cases). Scenario E/H not implemented. Not a deterministic fault-injection harness.

## 6. A–H result matrix

| Case | Initial state | Iters | Threads | Pass | Fail | DB errors | Error class | Exact op evidence | Integrity | Reopen | Status |
|---|---|---:|---:|---:|---:|---:|---|---|---|---|---|
| A. Serial RW init, fresh DB | fresh, no DB | 10 | 32 serial | 10 | 0 | 0 | - | - | ok | not separately recorded | TESTED (limited) |
| B. Concurrent RW init, fresh DB | fresh, no DB | 10 | 32 concurrent barrier | 9 | 1 | 55 bad events/5 failed opens | SQLITE_BUSY (5/5) | `INITIALIZATION_OPERATION_FAILURE` `PRAGMA journal_mode=WAL` op_seq=1 api=sqlite3_exec primary=5 extended=5 rc=5 | ok | not separately recorded | TESTED (limited) |
| C. Serial RW init, existing WAL DB | precreated WAL DB | 10 | 32 serial | 10 | 0 | 0 | - | - | ok | not separately recorded | TESTED (limited) |
| D. Concurrent RW init, existing WAL DB | precreated WAL DB | 10 | 32 concurrent barrier | 8 | 2 | 6 bad events/3 failed opens | SQLITE_BUSY (5/5) and SQLITE_BUSY_RECOVERY (5/261) | all at `PRAGMA journal_mode=WAL` op_seq=1 api=sqlite3_exec; includes primary=5 extended=261 rc=5 | ok | not separately recorded | TESTED (limited) |
| E. Concurrent RO init while RW init | - | - | - | - | - | - | - | - | - | - | NOT RUN |
| F. Existing long-lived reader + concurrent RW init | fresh DB then active reader txn | 10 | 32 concurrent | 10 | 0 | 0 | - | - | ok | not separately recorded | TESTED (limited) |
| G. Existing writer/txn + concurrent RW init | fresh DB then active writer txn | 10 | 32 concurrent | 10 | 0 | 0 | - | - | ok | not separately recorded | TESTED (limited) |
| H. Multi-process RW init same DB | - | - | - | - | - | - | - | - | - | - | NOT RUN |

## 7. Raw-vs-production comparison
**NOT RUN**. No raw SQLite comparison harness executed.

## 8. Failure classification
Observed failures in R13 limited matrix:
- `sqlite3_open_v2` failure: **NOT OBSERVED**.
- `PRAGMA journal_mode=WAL` failure: **OBSERVED** (B: primary=5 extended=5; D: primary=5 extended=5 and primary=5 extended=261).
- Other PRAGMA failure: **NOT OBSERVED**.
- SQLite wrapper/RAII failure: **NOT OBSERVED**.
- Filesystem/I/O failure: **NOT OBSERVED**.
- Transaction failure: **N/A** (init path).
- Harness synchronization/process-management failure: **NOT OBSERVED**.

## 9. Causal isolation assessment
**ROOT CAUSE NOT PROVEN**. Required criteria not satisfied: A-H incomplete (E/H missing), no raw-vs-production comparison, no repeated stable trials at required scale, no independent variable isolation proving removal of condition eliminates 261 under equivalent workload, no fault injection, no process campaign. Reproduction of 261 exists but mechanism is not causally proven.

## 10. Fault injection
**NOT IMPLEMENTED**. No test-only deterministic initialization fault injection. No before-SQLite-call/simulated SQLite result/wrapper-level/cleanup-close/reopen injection tests.

## 11. Production-fix decision
**BLOCKED UNTIL EVIDENCE**. No production fix implemented. No config/semantics change. Any proposal would require separate explicit authorization and evidence for durability, `synchronous=FULL` preservation, startup/recovery, RO behavior, backup/restore compatibility, existing WAL DB compatibility, migration/rollback, and independent review.

## 12. Deferred security blockers
| Blocker | Status |
|---|---|
| Connection ownership matrix A–E | NOT RUN |
| Cleanup/create/consume/count lifecycle race | NOT IMPLEMENTED |
| Transaction-stage fault injection | NOT IMPLEMENTED |
| Commit ambiguity fault injection | NOT IMPLEMENTED |
| Cross-process quota creation acceptance | NOT RUN |
| ASAN | NOT RUN |
| UBSAN | NOT RUN |
| TSan | NOT RUN |

## 13. Regression results (final R13 source state)
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS** (executed before harness-only addition; not rerun after adding `tests/harness/r13_wal_init_matrix.cpp`)
- `./build-ubuntu/tests/phantomx_unit_tests` — **123 passed, 0 failed, 123 total** (pre-harness-addition run)
- `ctest --test-dir build-ubuntu --output-on-failure` — **NOT RUN**
- `cmake --build build-ubuntu --target phantomxd -j6` — **NOT RUN**
- ASAN/UBSAN/TSan — **NOT RUN** (not configured/executed)

## 14. Limitations / uncompleted work
- A-H matrix incomplete (E/H missing), limited iterations (10), incomplete metadata capture.
- No raw-vs-production comparison.
- No deterministic fault injection.
- No causal proof.
- No production fix.
- Required R8-R12 reports missing; R13 report created with explicit gaps.
- Deferred security blockers remain.

## 15. Final verdict
- Phase 15C.11: **PARTIAL**
- SQLite root cause: **NOT PROVEN**
- Challenge quota concurrency: **NOT ACCEPTED**
- Consensus-authoritative verifier registry: **BLOCKED**
- Phase 15C.12: **BLOCKED**
- Production testnet: **NO-GO**
- Mainnet: **NO-GO**
