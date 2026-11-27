# PHASE B2-R — SQLITE_BUSY_RECOVERY (5/261) Controlled Reproduction Report

**Status: PARTIAL (controlled reproduction attempted; 5/261 not reproduced in this session)**  
**Date:** 2026-11-26  
**Branch:** `encryption`  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged)  
**Dirty worktree (start):** 114 entries (`/tmp/b2r_baseline_status.txt`)  
**Dirty worktree (end):** 114 entries before report creation; report is new untracked file. No destructive git operations.

## A. Baseline

| Item | Value |
|---|---|
| Branch | `encryption` |
| HEAD | `d094596549a53eb0695527df06729eca41e737e9` |
| Dirty worktree before | 114 |
| Baseline snapshot | `/tmp/b2r_baseline_status.txt` |
| Production source changes | **None** |
| Destructive git ops | **None** |

## B. Environment

| Item | Value |
|---|---|
| OS/Kernel | Linux ubuntu-VMware-Virtual-Platform 7.0.0-44-generic #44-Ubuntu SMP PREEMPT_DYNAMIC x86_64 GNU/Linux |
| CMake | 4.2.3 (from B1-R2 context) |
| Compiler | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 |
| SQLite (pkg-config/header) | 3.46.1 / libsqlite3-dev 3.46.1-9ubuntu0.3 |
| Python sqlite3 | 3.46.1 |
| Build flags | Existing `build-ubuntu` binaries used as-is; ad-hoc test harnesses compiled with `gcc/g++ -O2 -lsqlite3 -pthread` in `/tmp` (test-only, not in repo) |

## C. Existing harness audit

| Harness | Present | Built in build-ubuntu | Audit notes |
|---|---:|---:|---|
| `tests/harness/r16_wal_init_harness.cpp` | Yes | Yes (`build-ubuntu/tests/r16_wal_init_harness`) | Classifies `SQLITE_BUSY 5/261` as `SQLITE_BUSY_RECOVERY`; scenarios A-H cover fresh/existing-WAL serial/concurrent RW, RO concurrent, long reader, existing writer, multi-process. Uses `SqliteDb::open` (production wrapper) and raw sqlite for some setup. Records busy55/busy261/other/harness. Limitation: not all real crash/recovery timing windows guaranteed; small-iteration smoke may miss rare contention. |
| `tests/harness/r19_raw_vs_prod.cpp` | Yes | Yes (`build-ubuntu/tests/r19_raw_vs_prod`) | Compares raw sqlite3 open+pragma vs `SqliteDb::open` under concurrent fresh init (16 threads, 3 iters in code). Counts busy55/busy261/other. Limitation: fixed small matrix; "not a full equivalence proof" per program output. |
| `tests/harness/sqlite_busy_recovery_probe.cpp` | Yes | Not prebuilt target | Simple probe: read tx on A, insert on B, passive checkpoint while read active. Observed no 5/261 in this run. Limitation: narrow scenario, short timeout. |

## D. Test matrix

All tests used disposable `/tmp/b2r_*` databases. No production/user/testnet DBs used.

| Test ID | Setup | Concurrency | Command | Exit | SQLite codes observed | Integrity | Result |
|---|---|---:|---|---:|---|---|---|
| B2R-SMOKE-R16-A | Fresh DB, serial RW init, wrapper | 1 logical, 5 iters, 8 threads param (serial scenario) | `./build-ubuntu/tests/r16_wal_init_harness A /tmp/b2r_r16_A b2r 5 8` | 0 | no busy261; no busy55 | harness checks pass | PASS (5/5) |
| B2R-SMOKE-R16-B | Fresh DB, concurrent RW init, wrapper | 8 threads, 5 iters | `... B ... 5 8` | 0 | busy55 seen (5/5 primary); **busy261=0** | n/a | PASS with transient BUSY 5/5 (3 pass, 2 fail iters by harness classification) |
| B2R-SMOKE-R16-C | Existing WAL, serial RW | 5 iters | `... C ... 5 8` | 0 | no 261 | n/a | PASS (5/5) |
| B2R-SMOKE-R16-D | Existing WAL, concurrent RW | 8 threads, 5 iters | `... D ... 5 8` | 0 | busy55 possible; **busy261=0** | n/a | 4 pass, 1 fail |
| B2R-SMOKE-R16-E | Existing WAL, concurrent RW+RO | 8 threads, 5 iters | `... E ... 5 8` | 0 | **busy261=0** | n/a | PASS (5/5) |
| B2R-SMOKE-R16-F | Existing WAL, long reader + concurrent RW | 8 threads, 5 iters | `... F ... 5 8` | 0 | **busy261=0** | n/a | PASS (5/5) |
| B2R-SMOKE-R16-G | Existing WAL, existing writer + concurrent RW | 8 threads, 5 iters | `... G ... 5 8` | 0 | **busy261=0** | n/a | PASS (5/5) |
| B2R-SMOKE-R16-H | Existing WAL, multi-process concurrent RW | 4 procs, 3 iters | `timeout 120 ./build-ubuntu/tests/r16_wal_init_harness H /tmp/b2r_r16_H b2r 3 4` | 0 | **busy261=0** | n/a | PASS (3/3) |
| B2R-R19 | Fresh raw vs prod concurrent init | 16 threads, 3 iters | `build-ubuntu/tests/r19_raw_vs_prod` in `/tmp/b2r_r19` | 0 | busy55 seen; **busy261=0** | n/a | Observed BUSY 5/5 only |
| B2R-PROBE | Read tx + insert + passive checkpoint | 2 conns | compiled `tests/harness/sqlite_busy_recovery_probe.cpp` -> `/tmp/b2r_probe` | 0 | all rc=0 | n/a | No error |
| B2R-A | Concurrent WAL init (raw sqlite3) | 2,4,8 processes, 20 opens each | `/tmp/b2r_matrix A` (compiled test-only C) | 0 | **busy261=0**, busy=0 | n/a | PASS; all children ok |
| B2R-B | Concurrent writers (raw sqlite3) | 4 processes, 100 tx each | `/tmp/b2r_matrix B` | 0 | **busy261=0** | row_count=400, integrity_rc=0 | PASS |
| B2R-C | Long reader + writer + passive checkpoint | 1 reader, 1 writer, 1 checkpoint | `/tmp/b2r_matrix C` | 0 | **busy261=0** | `PRAGMA integrity_check=ok` | PASS |
| B2R-D-attempt | WAL recovery after SIGKILL writer, reader holds lock, probe opens/checkpoints/selects | 1 writer(SIGKILL after COMMIT), 1 reader, 1 probe loop | `/tmp/b2r_261`, `/tmp/b2r_261_loop` (30 tries, 20 probes/try) | 0 | **total261=0**; no busy261 observed | not fully asserted | NOT REPRODUCED |

## E. Reproduction evidence

- **BUSY_RECOVERY 5/261 reproduced:** **NO (NOT REPRODUCED in this session).**
- **Exact 5/261 error:** None observed in this session.
- **Repeatability:** N/A for 5/261 (not reproduced). BUSY 5/5 was observed repeatably in concurrent init scenarios (R16 B/D, R19).
- **Conditions tested:** fresh/existing WAL, serial/concurrent, RW/RO, long reader, existing writer, multi-process, concurrent init, concurrent writers, long reader+writer+checkpoint, SIGKILL-after-commit WAL recovery attempt.
- **Limitations:** 5/261 is timing-sensitive (WAL recovery contention). This session did not achieve a deterministic reproduction. Prior reports still claim reproduction; this result does not invalidate them and does not confirm root cause absence.

## F. Data integrity

| Check | Result | Evidence |
|---|---|---|
| Atomicity (B) | PASS | 4 processes x 100 tx, row_count=400, no fail |
| Durability/consistency (B,C) | PASS | integrity_rc=0 / integrity_check=ok |
| Idempotency/duplicate/lost/overshoot | NOT FULLY EXERCISED | B used simple inserts; no quota/idempotency fault-injection matrix executed |
| Crash recovery (D-attempt) | INCONCLUSIVE | SIGKILL after commit tested; no 5/261; no corruption observed in quick probe; not a comprehensive crash-point matrix |
| DB integrity checks | PASS | Where run (B,C) ok |

## G. Root cause

**NOT REPRODUCED** in this controlled session. Confidence in root-cause identification: **UNVERIFIED**.

Rationale: 5/261 not observed despite targeted matrix; prior repo evidence still indicates historical reproduction. Cannot confirm or deny root cause. Likely domain remains WAL recovery/read-lock contention during init/open/checkpoint/recovery windows, but unproven here.

## H. Recommended next steps (no remediation implemented)

1. Re-run R16/R19 with higher iteration counts and varied thread/process counts on same host and on CI-like hosts; capture full event logs.
2. Build a deterministic WAL-recovery contention harness: create uncheckpointed WAL after SIGKILL at specific points, hold SHM read lock from another process, then open/recover from third process; record all rc/primary/extended/errmsg and -wal/-shm states.
3. Add observability-only test harness around SqliteDb open/init paths (test-only) to capture exact failing pragma/API.
4. Run fault-injection matrix for commit ambiguity/quota path with isolated DB copies.
5. Only after deterministic reproduction, propose minimal remediation.

## I. Files changed

| File | Reason |
|---|---|
| `docs/reports/PHASE_B2R_SQLITE_BUSY_RECOVERY_REPRODUCTION_REPORT.md` | Phase B2-R controlled reproduction report |

Test-only temporary harnesses compiled in `/tmp` (`/tmp/b2r_261*`, `/tmp/b2r_matrix*`, `/tmp/b2r_probe`) were not added to repo. No production source files changed.

## J. Final verdict

**PARTIAL**

Controlled matrix executed on isolated DBs; BUSY 5/5 observed in some concurrent init cases; **SQLITE_BUSY_RECOVERY 5/261 NOT REPRODUCED** in this session. Data integrity checks passed where executed. Root cause remains UNVERIFIED. No production remediation performed.
