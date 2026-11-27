# PHANTOMX Phase 15C.11-R17 Harness Correctness Report

**Status:** PARTIAL / NOT ACCEPTANCE-READY  
**Date:** 2026-11-26 (workspace execution)  
**Baseline:** `d094596` (`encryption`)  
**Branch:** `encryption`  
**Dirty worktree:** preserved (pre-R17 106 entries recorded in `/tmp/r17_git_status.txt`; R16/R17 harness/report changes added, uncommitted).

## 1. Status R17
**PARTIAL**. R17 improved harness evidence/correlation and fixed R16 harness defects, but required runtime overlap proofs, observer integrity A-J, negative tests, raw-vs-production comparison, and acceptance-gating smoke matrix are incomplete. No production SQLite fix. No Phase 15C.12.

## 2. Baseline and worktree preservation
- `git rev-parse HEAD` = `d094596549a53eb0695527df06729eca41e737e9`.
- `git branch --show-current` = `encryption`.
- `git status --short` captured before R17 work: 106 entries (`/tmp/r17_git_status.txt`).
- Preserved R13/R16 harnesses and prior reports. No destructive git commands.
- No changes to genesis/canonical config/network ID/monetary/reward/block/consensus/PoP/verifier lifecycle/quota semantics/registry design. No production SQLite journal/synchronous/busy_timeout/tx policy/connection ownership changes.

## 3. Daftar file berubah (R17 delta)
- `tests/harness/r16_wal_init_harness.cpp` — extended event schema (retry_decision/backoff/rollback/final_sqlite_error), E RO/RW event presence + overlap note, H child process event capture/import, distinct PID check, auto-fixes.
- `docs/security/PHANTOMX_PHASE15C11R17_HARNESS_CORRECTNESS_REPORT.md` — this report.

## 4. Audit R16
Inspected `tests/harness/r16_wal_init_harness.cpp` (718+ lines after R17). Findings:
- **Thread lifecycle:** concurrent workers joined; barrier has timeout/abort; partial startup classified as HARNESS_FAILURE in barrier timeout path. **PARTIAL**.
- **Process lifecycle:** H forks children, waitpid, distinct PID check, child output files; child event CSV imported. **PARTIAL** (IPC barrier not robust; child observer not same correlation model as parent).
- **Connection ownership:** uses production `SqliteDb::open/open_readonly` per worker; close checked. **PARTIAL**.
- **Barrier/synchronization:** in-process atomic barrier for threads; no process barrier for H. **PARTIAL**.
- **SQLite statement lifetime:** reopen_verify scoped statements before close (R16 auto-fix). **IMPROVED**.
- **DB close/reopen:** integrity_check + reopen_verify after iteration. **PARTIAL**.
- **Error capture:** observer captures rc/primary/extended/msg/api/stage at emit time; worker outcome also parses message when needed. **PARTIAL** (not fully proven original-error-before-all-subsequent-SQLite-calls).
- **Retry behavior:** no production retry policy changed; event fields exist but are defaults (`none`) because no runtime retry path instrumented in harnessed open path. **NOT VERIFIED**.
- **Observer callback lifetime:** global observer set/cleared per iteration; no deterministic lifetime/reentrancy/deadlock tests. **NOT VERIFIED**.
- **CSV/JSON serialization:** structured CSV/JSON; child CSV imported as raw audit lines. **PARTIAL**.
- **Cleanup on exception/failure:** best-effort; no exception-hardening test. **PARTIAL**.
- **Exit status/classification:** PASS/SQLITE_BUSY/SQLITE_BUSY_RECOVERY/SQLITE_BUSY_MIXED/SQLITE_OTHER/HARNESS_FAILURE separated. **PARTIAL**.

## 5. Validasi runtime overlap A–H

| Scenario | Required proof | R17 evidence | Status |
|---|---|---|---|
| A Serial fresh RW | no overlap, begin/end intervals per op | smoke PASS 2/2 previous; no per-op interval overlap assertion | **NOT VERIFIED** |
| B Concurrent fresh RW | real RW overlap, conn/op intervals | smoke showed SQLITE_BUSY 5/5 and concurrent workers; no explicit overlap assertion | **PARTIAL** |
| C Serial existing WAL | WAL prestate asserted, serial ops | WAL prestate asserted in harness; seriality not interval-proven | **PARTIAL** |
| D Concurrent existing WAL | WAL prestate + overlap | WAL prestate asserted; overlap not explicitly proven | **PARTIAL** |
| E Concurrent RO+RW | actual RO+RW, READONLY/READWRITE, overlap, flags/PID/conn/interval evidence | uses `open_readonly`+`open`; saw_ro/saw_rw recorded; `ro_rw_overlap=1` note from monotonic event windows; not full audited interval proof per connection/flags dump | **PARTIAL** |
| F Long-lived reader | reader txn active during RW init, BEGIN/active/RW begin-end/commit-rollback ordered | reader BEGIN+SELECT step setup; no active-during-init proof/assertion | **PARTIAL** |
| G Existing writer txn | writer txn active during RW init, ordered evidence | BEGIN IMMEDIATE+INSERT setup; no active-during-init proof/assertion | **PARTIAL** |
| H Multi-process | different PIDs, same DB, overlap, IPC/barrier verifiable, child events collected in parent, no missing child events unclassified | distinct PID asserted; same DB path; child events imported as raw CSV lines; no robust process barrier/overlap proof | **PARTIAL** |

## 6. Negative test results
**NOT IMPLEMENTED / NOT RUN**. Required misconfigurations (E no RO/RW/no overlap, F no active reader, G no active writer, H same PID/different DB/no child event, D not existing WAL, A non-serial, B serial) not created.

## 7. Observer integrity A–J
**NOT IMPLEMENTED / NOT RUN**.
- A success capture: existing R10 test covers ordering; not R17-dedicated full matrix.
- B failure capture: **NOT RUN**.
- C original SQLite error preservation: **NOT VERIFIED**.
- D observer disabled semantic equivalence: **NOT RUN**.
- E concurrent callback invocation: **NOT RUN**.
- F callback lifetime during close: **NOT RUN**.
- G reentrancy: **NOT RUN**.
- H exception policy: **NOT RUN**.
- I sequence uniqueness/correlation completeness: **NOT VERIFIED**.
- J no unintended serialization/deadlock: **NOT VERIFIED**.

## 8. Retry/final API instrumentation
**PARTIAL**. Event schema extended with `retry_attempt,retry_decision,backoff_us,rollback_result,final_api_result,final_sqlite_error`. Current values are defaults because harness observes connection init events; production init path has no retry loop instrumented for these PRAGMA operations. Fields are not fabricated from runtime retries. **NOT VERIFIED** as actual runtime retry evidence.

## 9. Raw-vs-production comparison
**NOT IMPLEMENTED / NOT RUN**. No comparable raw SQLite harness. **NOT COMPARABLE**.

## 10. Database integrity and reopen evidence
**PARTIAL**. Harness captures before/after DB/WAL/SHM/journal/schema, runs integrity_check, reopen verify with schema/data-marker checks where applicable. Smoke evidence: A/C/D/F/G/H PASS, B/E had SQLITE_BUSY 5/5 but integrity/reopen checks passed in recorded iterations. Full failure-path cleanup/partial-write matrix not complete.

## 11. AUTO-FIX SUMMARY

| Field | Required evidence |
|---|---|
| Errors detected | 1) R16 compile defect previously fixed in R16: IterCtx copy/deleted mutex. 2) R16 smoke reopen_verify unfinalized statement -> sqlite3_close BUSY 5/5. 3) R16/R17 H child output files empty due to `_exit()` without flush. 4) R17 child event import initially missing; H iterations had `saw_rw=0` in parent observer. |
| Root cause | 1) **IDENTIFIED** - non-copyable mutex in copied struct. 2) **IDENTIFIED** - Statement RAII outlived close. 3) **IDENTIFIED** - buffered std::ofstream lost on `_exit()`. 4) **IDENTIFIED** - parent observer cannot see child-process events; child events not persisted/imported. |
| Files modified | `tests/harness/r16_wal_init_harness.cpp`, `docs/security/PHANTOMX_PHASE15C11R16_HARNESS_EVIDENCE_INTEGRITY_REPORT.md`, `docs/security/PHANTOMX_PHASE15C11R17_HARNESS_CORRECTNESS_REPORT.md` |
| Repair | 1) Refactor run_iteration params. 2) Scope statements before close. 3) Flush/close before `_exit()`. 4) Child writes event CSV; parent imports raw child event lines and flags missing child events as HARNESS_FAILURE. |
| Attempts | 1 attempt each (4 defects). |
| Original test | 1) build FAIL. 2) smoke A HARNESS_FAILURE. 3) H empty proc outputs. 4) H parent events missing. |
| Post-fix test | 1) build PASS. 2) A smoke PASS. 3) H proc outputs present. 4) H smoke PASS 1/4, child events imported (`/tmp/r17_smoke/H/H.events.csv` 69 lines). |
| Regression | build unit PASS; unit tests 123/123 PASS; ctest 11/11 PASS; phantomxd PASS. ASAN/UBSAN/TSan NOT RUN. |
| Remaining issues | A-J observer tests, negative harness tests, raw-vs-production, full A-H runtime overlap proofs, real retry instrumentation, 100-iter acceptance campaigns NOT DONE. |

## 12. Final regression
Executed on final R17 source state:
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS** (exit 0)
- `./build-ubuntu/tests/phantomx_unit_tests` — **123 passed, 0 failed, 123 total**
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS, 0 failed** (0.55s)
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS** (exit 0)

## 13. Sanitizer results
- ASAN: **NOT RUN** (no ASAN-configured build executed in this session).
- UBSAN: **NOT RUN**.
- TSan: **NOT RUN**.

## 14. Remaining blockers
- Runtime overlap proofs A-H incomplete/unaudited.
- Negative harness tests missing.
- Observer integrity A-J missing.
- Retry/final API runtime instrumentation missing (fields present, defaults only).
- Raw-vs-production comparison missing / NOT COMPARABLE.
- F/G active-transaction-during-init proof incomplete.
- H process barrier/overlap proof incomplete; child events imported as raw lines, not normalized full schema.
- 100-iter A-H and quota acceptance campaigns not run.
- R8-R12 historical reports missing.
- SQLite root cause not proven.

## 15. Root-cause assessment
**NOT PROVEN / INVESTIGATING**. Known evidence still localizes reproduced 5/261 to `PRAGMA journal_mode=WAL` concurrent RW init (R11/R13). R17 did not produce causal proof or production fix.

## 16. Production/testnet/mainnet verdict
- Production testnet: **NO-GO**
- Mainnet: **NO-GO**

## 17. Phase 15C.12 readiness
**BLOCKED**. R17 acceptance criteria not met; harness not acceptance-ready; root cause not proven; challenge quota concurrency not accepted; consensus-authoritative registry BLOCKED.
