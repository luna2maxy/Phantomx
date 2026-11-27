# PHANTOMX Phase 15C.11-R14 SQLite WAL Causal Isolation Report

**Status:** PARTIAL / NOT PROVEN  
**Date:** 2026-04-30 (workspace execution)  
**Baseline:** `d094596` (`encryption`)  
**Worktree:** dirty, preserved. `git status --short` = 106 entries before R14 work (captured `/tmp/r14_git_status.txt`). No reset/clean/revert/destructive ops.

## 1. Preservation
- Preserved all existing dirty modifications and prior reports/harnesses.
- No changes to genesis, canonical config, network ID, monetary rules, reward economics, block format, consensus semantics, PoP, verifier lifecycle, challenge quota semantics, or registry design.
- No production WAL/synchronous/busy_timeout/transaction policy/connection ownership changes.
- No Phase 15C.12 work.
- No production fix applied.

## 2. R13 harness validation (code inspection)

Inspected `tests/harness/r13_wal_init_matrix.cpp` (179 lines). Findings:

| Check | Result | Evidence |
|---|---|---|
| Serial mode opens one-by-one | **PARTIAL/DEFECT** | Serial branch creates thread per connection and joins immediately; functional but not a clean serial loop and has confusing comment. |
| Concurrent mode uses barrier before initialization | **PARTIAL** | File barrier + sleep poll; not a robust in-process barrier, but synchronizes start approximately. |
| All threads use same DB when required | **PASS** | `db` path shared by scenario. |
| Fresh DB vs existing WAL DB different initial state | **DEFECT** | `preexisting_wal || !fresh` creates DB then `if (fresh)` removes it again; fresh/existing-WAL logic is confusing and can remove prepared state. |
| Existing WAL DB closed/reopened before experiment | **PARTIAL** | Init opens/closes via SqliteDb; explicit reopen check not recorded. |
| No leftover results between iterations | **PARTIAL** | DB/wal/shm removed per iter; per-iter JSON written; events accumulated only for failing iterations. |
| Each iteration independent identity/result | **PARTIAL** | Iter JSON includes iter; event rows have conn/open_attempt/generation but no iter column in fail_events CSV. |
| Harness does not change production semantics | **PASS** (intent) | Uses production `SqliteDb::open`; observer is test-side. |
| Scenario E implemented | **DEFECT** | `E_concurrent_ro_rw` mapped to same args as D (no RO concurrent init path). |
| Scenario H implemented | **NOT IMPLEMENTED** | missing. |
| Separates SQLITE_BUSY (5/5) vs BUSY_RECOVERY (5/261) | **DEFECT** | summary does not separate classes. |
| Records SQLite version/compile options | **NOT IMPLEMENTED** | not written by harness. |
| Records before/after journal mode/WAL/SHM sizes | **PARTIAL** | per-iter JSON records after-state only. |
| Records monotonic sequence/operation sequence | **PARTIAL** | observer has mono_seq/op_seq; CSV has them but no wall-clock monotonic timestamp field as specified. |
| Integrity_check/close/reopen/persistence/schema verification | **PARTIAL** | integrity_check after iter; no explicit close/reopen/schema/persistence verification. |
| Retry count/final API result | **NOT IMPLEMENTED** | not structured. |
| Harness failure vs SQLite failure classification | **NOT IMPLEMENTED** | errors stored as strings. |

**Conclusion:** R13 harness is useful for limited observation but **NOT VALID** as complete R14 acceptance/evidence harness without fixes.

## 3. Caller audit
**PARTIAL / NOT VERIFIED** (not exhaustive; no full runtime ownership proof).

| Source | Prod/Test | Role | Mode | WAL PRAGMA | Notes |
|---|---|---|---|---|---|
| `src/database/sqlite_db.cpp:SqliteDb::open` | Prod | generic RW | RW/CREATE/FULLMUTEX | Yes, op seq 1 | every open runs 5 PRAGMAs individually |
| `src/database/sqlite_db.cpp:SqliteDb::open_readonly` | Prod | generic RO | RO/FULLMUTEX | No | temp_store,busy_timeout=2000 |
| `src/database/sqlite_db.cpp:backup_to` dest open | Prod | backup dest | RW/CREATE | No init PRAGMAs | not chain DB role |
| `src/blockchain/chain.cpp:ChainDatabase::open` + restore/snapshot/backup-integrity | Prod | chain | RW via SqliteDb::open | via factory | operation-local/caller-owned |
| `src/aggregator/aggregator.cpp` | Prod | aggregator | RW via SqliteDb::open | via factory | NOT VERIFIED ownership/frequency |
| `apps/phantomx-qt/WalletService.cpp` | Prod app | wallet | RO via SqliteDb::open_readonly | No | NOT VERIFIED |
| `tests/harness/*` raw/probe | Test | probe | raw sqlite3 | test-controlled | not production |

SQLite env: `sqlite3` CLI unavailable; Python `sqlite3.sqlite_version=3.46.1`. Compile options previously captured in R13 (THREADSAFE=1, DEFAULT_SYNCHRONOUS=2, WAL defaults present).

## 4. A–H experiment matrix
**NOT RUN in R14**. R13 limited evidence retained as historical (not R14 acceptance):

| Scenario | R13 runs | R13 result | R14 required 100-iter | R14 status |
|---|---:|---|---:|---|
| A Serial RW fresh | 10 | 10/0 | 100 | NOT RUN |
| B Concurrent RW fresh | 10 | 9/1, 5/5 busy | 100 | NOT RUN |
| C Serial RW existing WAL | 10 | 10/0 | 100 | NOT RUN |
| D Concurrent RW existing WAL | 10 | 8/2, incl 5/261 | 100 | NOT RUN |
| E Concurrent RO while RW init | 0 | not implemented | 100 | NOT RUN |
| F Long-lived reader + concurrent RW | 10 | 10/0 | 100 | NOT RUN |
| G Existing writer/txn + concurrent RW | 10 | 10/0 | 100 | NOT RUN |
| H Multi-process RW init same DB | 0 | not implemented | 100 | NOT RUN |

## 5. Raw-vs-production comparison
**NOT IMPLEMENTED / NOT RUN**. No raw SQLite comparison harness created/executed in R14.

## 6. SQLITE_BUSY vs SQLITE_BUSY_RECOVERY
From R13 limited runs only (not R14 acceptance):
- B concurrent fresh RW: primary=5 extended=5 (`SQLITE_BUSY`) observed.
- D concurrent existing WAL RW: primary=5 extended=5 and primary=5 extended=261 (`SQLITE_BUSY_RECOVERY`) observed.
- Both at `PRAGMA journal_mode=WAL`, op seq 1, sqlite3_exec, RW, connection observer captured.
R14 100-iter separated classification: **NOT RUN**.

## 7. Hypotheses and causal isolation
**NOT PROVEN**. H1-H6 not executed as controlled differential experiments in R14. R13 gives indications only; no causal criteria satisfied.

## 8. Error capture integrity
**PARTIAL**. Existing observer captures immediate rc/primary/extended/errmsg in SqliteDb exec/open paths; R10/R11 added per-PRAGMA events. Required deterministic observer tests (success/failure/original-code preservation/observer disabled/no deadlock/no behavior change/lifetime/concurrency) **NOT IMPLEMENTED**.

## 9. Fault injection
**NOT IMPLEMENTED**. No test-only init-stage fault injection (open/journal_mode/synchronous/foreign_keys/temp_store/busy_timeout/init completion; before-call/simulated/wrapper/cleanup/reopen). Production daemon isolation not applicable because not implemented.

## 10. Integrity/reopen/persistence
**PARTIAL**. R13 harness ran integrity_check in executed iterations and reported ok; no per-failure close/reopen/schema/data/quota-overshoot/partial-write detailed matrix. R14 required per-iteration checks: **NOT RUN**.

## 11. Quota acceptance
**NOT ACCEPTED**.
- Per-identity 100/100: **NOT RUN**
- Global 100/100: **NOT RUN**
- 2-process ×100: **NOT RUN**
- 4-process ×100: **NOT RUN**
- Zero DB errors/zero overshoot/integrity/reopen: **NOT VERIFIED**

## 12. Deferred blockers
- Connection ownership matrix A–E: **NOT RUN**
- Cleanup/create/consume/count lifecycle race: **NOT IMPLEMENTED**
- Transaction-stage fault injection: **NOT IMPLEMENTED**
- Commit ambiguity fault injection: **NOT IMPLEMENTED**
- ASAN: **NOT RUN**
- UBSAN: **NOT RUN**
- TSan: **NOT RUN**
- Cross-process quota creation acceptance: **NOT RUN**

## 13. Final regression (R14 source state)
Executed in R14 session (no R14 code changes; R13 report/harness already present in dirty tree):
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS**
- `./build-ubuntu/tests/phantomx_unit_tests` — **123 passed, 0 failed, 123 total**
- `ctest --test-dir build-ubuntu --output-on-failure` — **NOT RUN**
- `cmake --build build-ubuntu --target phantomxd -j6` — **NOT RUN**
- ASAN/UBSAN/TSan — **NOT RUN**

## 14. Files changed
- `docs/security/PHANTOMX_PHASE15C11R14_SQLITE_WAL_CAUSAL_ISOLATION_REPORT.md` — **new** (this report). No production code changes.

## 15. Root-cause conclusion
**ROOT CAUSE NOT PROVEN**. R11/R13 localized failures to `PRAGMA journal_mode=WAL` during concurrent RW initialization (including 5/261 in existing-WAL case), but R14 did not execute required controlled 100-iter A-H matrix, raw-vs-production comparison, hypothesis isolation, fault injection, or acceptance campaigns. No causal mechanism is proven.

## 16. Final verdict
- Phase 15C.11: **PARTIAL**
- SQLite root cause: **NOT PROVEN**
- Challenge quota concurrency: **NOT ACCEPTED**
- Consensus-authoritative verifier registry: **BLOCKED**
- Phase 15C.12: **BLOCKED**
- Production testnet: **NO-GO**
- Mainnet: **NO-GO**
