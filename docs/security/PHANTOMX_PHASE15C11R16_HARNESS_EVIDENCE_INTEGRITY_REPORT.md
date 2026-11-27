# PHANTOMX Phase 15C.11-R16 Harness Reconstruction Report

**Status:** PARTIAL  
**Date:** 2026-11-26 (workspace execution)  
**Baseline:** `d094596` (`encryption`)  
**Worktree:** dirty, preserved. Pre-R16 inventory: 106 entries (`/tmp/r15_git_status.txt` / `/tmp/r14_git_status.txt` evidence preserved). No reset/clean/revert/destructive ops.

## 0. Objective / scope
R16 implemented and smoke-tested a replacement WAL initialization harness. This is **test infrastructure only**. No production SQLite fix was implemented or authorized.

## 1. Preservation
- HEAD `d094596`, branch `encryption`, dirty worktree preserved.
- Inspected R13/R14/R15 artifacts. R13 harness preserved: `tests/harness/r13_wal_init_matrix.cpp`.
- No genesis/canonical config/network ID/monetary/reward/block/consensus/PoP/verifier lifecycle/quota/registry changes.
- No production journal_mode/synchronous/busy_timeout/transaction policy/connection ownership changes.
- No Phase 15C.12.

## 2. R13 harness defects reproduced (inspection)
Confirmed in `tests/harness/r13_wal_init_matrix.cpp`: ambiguous fresh/existing-WAL initial state, E mapped to D, H missing, no 5/5 vs 5/261 separation, incomplete SQLite/DB-state metadata, missing retry/final API structured fields, incomplete close/reopen/persistence checks, no harness-failure classification, missing iteration identity in failure events, weak file barrier. **REPRODUCED (inspection)**.

## 3. New harness architecture
- New file: `tests/harness/r16_wal_init_harness.cpp`.
- Production init paths use `phantomx::db::SqliteDb::open` / `open_readonly`.
- Raw SQLite comparison: **NOT IMPLEMENTED**.
- Structured outputs per scenario: `<id>.summary.json`, `<id>.events.csv`, `<id>.iterations.csv`.
- Setup errors classified as `HARNESS_FAILURE`; SQLite errors classified separately.

## 4. Scenario-to-function mapping / A-H implementation

| Scenario | Implementation | Initial state assertion | Role/concurrency/process assertion | Smoke (R16) | Status |
|---|---|---|---|---|---|
| A Serial RW fresh DB | serial loop, `open_rw_worker` | clean DB/WAL/SHM asserted | serial loop | 2 iter/8 threads: **PASS 2/2** | IMPLEMENTED, SMOKE TESTED |
| B Concurrent RW fresh DB | concurrent threads + in-process Barrier | clean DB asserted | barrier arrival/release, total workers | 2 iter/8: **FAIL 0/2, SQLITE_BUSY 5/5** | IMPLEMENTED, SMOKE TESTED |
| C Serial RW existing WAL | `prepare_existing_wal_db` + WAL mode assert | journal_mode==wal asserted before test | serial | 2 iter/8: **PASS 2/2** | IMPLEMENTED, SMOKE TESTED |
| D Concurrent RW existing WAL | same prep as C + barrier | WAL asserted | concurrent barrier | 2 iter/8: **PASS 2/2** | IMPLEMENTED, SMOKE TESTED |
| E Concurrent RO+RW while RW init | actual `open_readonly` + `open` workers split | existing WAL asserted | saw_ro/saw_rw events, overlap note | 2 iter/8: **FAIL 1/2, SQLITE_BUSY 5/5, ro_rw_overlap=1** | IMPLEMENTED, SMOKE TESTED (overlap proof limited) |
| F Long-lived reader + concurrent RW | raw sqlite reader BEGIN+SELECT active, then RW init workers | existing WAL asserted | reader setup checked; active-during-init proof limited | 2 iter/8: **PASS 2/2** | IMPLEMENTED, SMOKE TESTED |
| G Existing writer/txn + concurrent RW | raw sqlite BEGIN IMMEDIATE+INSERT | existing WAL asserted | writer setup checked; active-during-init proof limited | 2 iter/8: **PASS 2/2** | IMPLEMENTED, SMOKE TESTED |
| H Multi-process RW init same DB | forked child processes, distinct PID check, per-child output file | existing WAL asserted | distinct PIDs asserted; child observer events not parent-correlated | 2 iter/4 procs: **PASS 2/2** after auto-fix | IMPLEMENTED, SMOKE TESTED |

## 5. Event schema / correlation
Events include: `run_id, scenario_id, iteration_id, process_id, thread_id, connection_id, open_attempt_id, lifecycle_generation, event_sequence, operation_sequence, database_role, initialization_stage, operation_name, api_name, sqlite_return_code, primary_result_code, extended_result_code, original_sqlite_error_message, retry_attempt, final_api_result, elapsed_monotonic_ns`.

Completeness: **PARTIAL**.
- Present: run/scenario/iteration IDs, conn/open_attempt/generation, event/operation seq, role/stage/op/api/rc/primary/extended/msg/elapsed.
- Gaps: retry_attempt/final_api_result not wired to real retry logic (defaults); original error captured via observer at failure point but no proof against all subsequent SQLite calls in every path; H child process events not captured in parent CSV; no uniqueness/correlation negative tests.

## 6. SQLite error classification
Implemented classes: `SQLITE_BUSY_5_5`, `SQLITE_BUSY_RECOVERY_5_261`, `SQLITE_BUSY_OTHER`, `SQLITE_LOCKED`, `SQLITE_IOERR`, `SQLITE_CANTOPEN`, `SQLITE_OTHER`, `WRAPPER_ERROR`, `FILESYSTEM_ERROR`, `HARNESS_FAILURE`. Iteration summary separates busy55/busy261/busy_other/sqlite_other/harness. **IMPLEMENTED/PARTIAL** (classification in worker outcome also parses error text for primary/extended when event not available; observer events remain authoritative for captured init events).

## 7. Database state and persistence validation
Per iteration captures before/after: db/wal/shm existence+size, journal_mode, schema_version (before/after recorded in CSV fields). SQLite version/compile options recorded in summary JSON. Post checks: integrity_check, reopen via `SqliteDb::open`, schema/data marker verification for existing-WAL/reader/writer scenarios; fresh scenarios verify reopen/readability. **PARTIAL**: close-result detail limited to reopen/integrity side effects; partial-write detection and per-failure deep persistence matrix not complete.

## 8. Observer integrity tests
**NOT IMPLEMENTED**. Required deterministic tests A-J not created: success event, failure event, original error preservation, observer disabled, concurrent invocation, callback lifetime during close, reentrancy policy, callback exception/failure policy, no deadlock, event sequence uniqueness/correlation completeness, no unintended serialization.

## 9. Raw-vs-production comparison
**NOT IMPLEMENTED / NOT RUN**.

## 10. Harness correctness assertions / negative tests
**NOT IMPLEMENTED**. Missing required negative misconfiguration tests (wrong scenario dispatch, missing RO in E, same PID for H, bad initial state, missing sync participants, missing correlation IDs, incomplete iteration result, close/reopen/integrity failure). Existing runtime assertions are partial (fresh clean state, WAL prestate, E ro/rw event presence, H distinct PIDs).

## 11. Smoke-test results
Small smoke run: `/tmp/r16_smoke4` (1 iter, 8 threads; H 4 procs) and targeted reruns. Results: A PASS, B SQLITE_BUSY 5/5, C PASS, D PASS, E SQLITE_BUSY 5/5 with ro_rw_overlap=1, F PASS, G PASS, H PASS after auto-fix. No `SQLITE_BUSY_RECOVERY 5/261` observed in this smoke sample (**NOT REPRODUCED** in smoke; not evidence of absence).

## 12. Acceptance readiness
**NOT READY**. Gated prerequisites unmet: negative mapping/setup tests missing; E overlap proof limited to timestamp window note; F/G active-state-during-init proof limited; H child events not correlated; observer integrity tests A-J missing; raw-vs-production missing; retry/final API/original-error guarantees incomplete. Full 100-iter A-H and quota acceptance campaigns **NOT RUN / DEFERRED**.

## 13. Deferred security blockers
- Connection ownership matrix A–E: **NOT RUN**
- Cleanup/create/consume/count lifecycle race: **NOT IMPLEMENTED**
- Transaction-stage fault injection: **NOT IMPLEMENTED**
- Commit ambiguity fault injection: **NOT IMPLEMENTED**
- Cross-process quota creation acceptance: **NOT RUN**
- ASAN/UBSAN/TSan: **NOT RUN**
- R8-R12 required historical reports missing: **NOT IMPLEMENTED**

## 14. Final regression (final R16 source state)
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS** (exit 0)
- `./build-ubuntu/tests/phantomx_unit_tests` — **123 passed, 0 failed, 123 total**
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS**, 0 failed
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS** (exit 0)
- ASAN/UBSAN/TSan — **NOT RUN** (not configured/executed in this session)

## 15. AUTO-FIX SUMMARY

| Field | Required evidence |
|---|---|
| Errors detected | 1) `r16_wal_init_harness.cpp` compile error: `IterCtx` copy with `std::mutex` deleted (g++ exit nonzero). 2) Smoke A/HARNESS_FAILURE: `reopen_verify` left prepared statements alive causing `sqlite3_close` BUSY 5/5 (`io_error: sqlite close: unable to close due to unfinalized statements...`). 3) H smoke classified `SQLITE_OTHER` because child process output files were empty; child used `_exit()` without flushing `std::ofstream`. |
| Root cause | 1) **IDENTIFIED** - struct contained non-copyable mutex and was copied. 2) **IDENTIFIED** - RAII statements not scoped before close. 3) **IDENTIFIED** - buffered output lost on `_exit()`. |
| Files modified | `tests/harness/r16_wal_init_harness.cpp` (new harness, auto-fixed during R16); `docs/security/PHANTOMX_PHASE15C11R16_HARNESS_EVIDENCE_INTEGRITY_REPORT.md` (new report). |
| Repair | 1) Refactored `run_iteration` to take explicit params; removed copy. 2) Scoped prepared statements in `reopen_verify` with inner block. 3) Flushed/closed child output before `_exit`. |
| Attempts | 1) 1 attempt. 2) 1 attempt. 3) 1 attempt. |
| Original test | 1) build FAIL. 2) A smoke FAIL/HARNESS_FAILURE. 3) H smoke FAIL/SQLITE_OTHER (harness artifact). |
| Post-fix test | 1) build PASS. 2) A smoke PASS 2/2. 3) H smoke PASS 2/2. |
| Regression | Unit build PASS; unit tests 123/123 PASS; ctest 11/11 PASS; phantomxd build PASS. ASAN/UBSAN/TSan NOT RUN. |
| Remaining issues | Observer integrity tests A-J NOT IMPLEMENTED; raw-vs-production NOT IMPLEMENTED; negative harness misconfig tests NOT IMPLEMENTED; E/F/G proof strength limited; H child event correlation missing; 100-iter A-H and quota acceptance NOT RUN; root cause NOT PROVEN. |

## 16. Files changed (R16)
- `tests/harness/r16_wal_init_harness.cpp` — new test-only R16 WAL init harness.
- `docs/security/PHANTOMX_PHASE15C11R16_HARNESS_EVIDENCE_INTEGRITY_REPORT.md` — this report.

## 17. Remaining limitations
- No production fix. No causal proof.
- Harness not acceptance-ready.
- A-H 100-iteration controlled matrix not run.
- Quota acceptance not run.
- Required observer integrity and raw comparison tests not implemented.
- Missing historical R8-R12 reports remain missing.

## 18. Final verdict
- Phase 15C.11: **PARTIAL**
- SQLite root cause: **NOT PROVEN**
- Challenge quota concurrency: **NOT ACCEPTED**
- Consensus-authoritative verifier registry: **BLOCKED**
- Phase 15C.12: **BLOCKED**
- Production testnet: **NO-GO**
- Mainnet: **NO-GO**
