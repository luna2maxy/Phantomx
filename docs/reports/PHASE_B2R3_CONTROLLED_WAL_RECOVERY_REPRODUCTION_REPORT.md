# PHASE B2-R3 — Controlled Multi-Process WAL Recovery Reproduction Report

**Status: PARTIAL — controlled harness executed; 5/261 NOT REPRODUCED in this session**  
**Date:** 2026-11-26  
**Branch:** `encryption`  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged)  
**Dirty worktree baseline:** 114 entries (`/tmp/b2r3_baseline_status.txt`)  
**Production source changes:** None. No remediation started.

## 1. Repository baseline

| Item | Value |
|---|---|
| Branch | `encryption` |
| HEAD | `d094596549a53eb0695527df06729eca41e737e9` |
| Dirty worktree | 114 (baseline snapshot `/tmp/b2r3_baseline_status.txt`) |
| Destructive git ops | None |
| Canonical testnet config | Unchanged |

## 2. Environment

| Item | Value |
|---|---|
| OS/kernel | Linux ubuntu-VMware-Virtual-Platform 7.0.0-44-generic #44-Ubuntu SMP PREEMPT_DYNAMIC x86_64 |
| Compiler | g++/gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0 |
| SQLite | 3.46.1 (libsqlite3-dev 3.46.1-9ubuntu0.3; harness reports `sqlite=3.46.1`) |
| Harness build | `gcc -O2 -Wall -Wextra -o /home/ubuntu/b2r3_harness /home/ubuntu/b2r3_harness.c -lsqlite3 -pthread` (exit 0; warnings only) |

## 3. Harness design

Test-only isolated multi-process harness (`/home/ubuntu/b2r3_harness.c`, copy also `/tmp/b2r3_harness.c`).

- **Coordinator:** forks worker processes per iteration, uses per-worker pipe barriers (ready -> release -> done), runs 100 iterations/config by default.
- **Workers:** open same disposable DB, emit JSONL events (`/tmp/b2r3_out/<cfg>_iter<N>_w<K>.jsonl`), record rc/primary/extended/errmsg/op/tx/wal state/timestamps.
- **Isolation:** each iteration uses fresh disposable DB path `/tmp/b2r3_<cfg>_<iter>.db`; old db/wal/shm removed per prepare mode.
- **Extended codes:** `sqlite3_extended_result_codes(db,1)` enabled in harness connections; `sqlite3_extended_errcode()` recorded.
- **Integrity:** `PRAGMA integrity_check` executed in worker flow (A/B) and coordinator best-effort post-iteration integrity check.

## 4. Experiment configurations and results

Command: `/home/ubuntu/b2r3_harness 100 > /tmp/b2r3_run.log 2>&1` (exit 0)

| Config | Workers/Openers | Iters | Ops | OK | Fail | BUSY 5 | BUSY_RECOVERY 261 | BUSY_SNAPSHOT 517 | LOCKED 6 | LOCKED_SHAREDCACHE 262 | Other |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A_fresh_w2 | 2 | 100 | 1626 | 1568 | 58 | 58 | 0 | 0 | 0 | 0 | 0 |
| A_fresh_w4 | 4 | 100 | 3201 | 3068 | 133 | 133 | 0 | 0 | 0 | 0 | 0 |
| A_fresh_w8 | 8 | 100 | 6540 | 6320 | 220 | 220 | 0 | 0 | 0 | 0 | 0 |
| A_existing_wal_w4 | 4 | 100 | 3600 | 3600 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| A_clean_w4 | 4 | 100 | 3600 | 3600 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| B_recovery_w2 | 2 | 100 | 1200 | 1200 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| B_recovery_w4 | 4 | 100 | 2400 | 2400 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| B_recovery_w8 | 8 | 100 | 4800 | 4800 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C_reader_recovery_w2 | 2 openers + 1 reader | 100 | 1300 | 1300 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C_reader_recovery_w4 | 4 openers + 1 reader | 100 | 2100 | 2100 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| **TOTAL** | - | **1000 cfg-iters** | **30367** | **29956** | **411** | **411** | **0** | **0** | **0** | **0** | **0** |

## 5. Reproduction evidence

- **SQLITE_BUSY_RECOVERY (261): NOT REPRODUCED** in this run (count 0).
- **SQLITE_BUSY (5): REPRODUCED** in fresh concurrent init (A_fresh_w2/w4/w8), total 411.
- **BUSY_SNAPSHOT 517 / LOCKED 6 / LOCKED_SHAREDCACHE 262:** not observed.
- **Repeatability:** BUSY 5 observed consistently in A_fresh configs. No 261 observed in 1000 config-iterations / 30367 recorded ops.

## 6. Data integrity

- Harness executed `PRAGMA integrity_check` in worker flows (A/B) and coordinator post-iteration checks on disposable DBs.
- No corruption/integrity failure reported by harness run; run completed with `DONE`.
- Limitation: summary aggregates result codes; per-iteration integrity pass/fail was not emitted as a separate summary column in this harness version (improvement needed for future runs).

## 7. Raw SQLite vs production-path comparison

| Aspect | B2-R3 harness | Production path |
|---|---|---|
| API | Raw sqlite3 C API | `SqliteDb` wrapper + `ChainDatabase` |
| Open flags | `SQLITE_OPEN_READWRITE|CREATE|FULLMUTEX` | Same in SqliteDb open |
| Pragmas | WAL/FULL/foreign_keys/temp_store/busy_timeout in workers | WAL/FULL/foreign_keys/temp_store/busy_timeout=5000 in SqliteDb open |
| Extended codes | enabled (`sqlite3_extended_result_codes`) | not explicitly enabled in production; extended_errcode read |
| Tx model | simple create/insert/select/checkpoint | BEGIN IMMEDIATE quota tx, commit ambiguity reconciliation |
| Recovery prep | controlled SIGKILL after COMMIT + -shm removal (B/C) | real crash/recovery scenarios untested here |

**Conclusion:** Not a production-path reproduction. Raw-harness no-261 does not prove production safety.

## 8. Limitations

- B/C recovery preparation is a controlled approximation (SIGKILL after COMMIT, `-shm` removed); it does not cover all WAL recovery states/timings.
- Harness did not produce a per-iteration integrity CSV column; integrity checks were executed but only aggregate run completion is reported.
- No commit-ambiguity/fault-injection on ChainDatabase quota path.
- No production wrapper instrumentation or RPC lifecycle.
- Timing-sensitive 5/261 may require different crash points, OS/filesystem conditions, or longer campaigns.
- `/tmp` artifacts may be cleaned; harness source preserved in `/home/ubuntu/b2r3_harness.c` (outside repo; test-only).

## 9. Root-cause confidence

**UNVERIFIED / NOT REPRODUCED**. 5/261 not observed in this controlled run. Historical 5/261 remains unverified from raw evidence (B2-R2). No remediation justified by this run alone.

## 10. Recommended next step

1. Improve harness: per-iteration integrity CSV, explicit wal/shm size capture, crash-point matrix (before BEGIN, during write, before COMMIT, after COMMIT before close, after close), and no-shm/with-shm variants.
2. Run higher-iteration campaign (>=1000/ config) on stable storage/host if resource allows.
3. Add production-wrapper comparison harness (test-only, no prod changes) using SqliteDb/ChainDatabase on disposable DBs.
4. Owner decision on temporary local observability instrumentation (separate approval).

## 11. Files changed

| File | Reason |
|---|---|
| `docs/reports/PHASE_B2R3_CONTROLLED_WAL_RECOVERY_REPRODUCTION_REPORT.md` | B2-R3 report |

Test-only harness files outside repo: `/home/ubuntu/b2r3_harness.c`, `/home/ubuntu/b2r3_harness` (compiled binary), `/tmp/b2r3_run.log`, `/tmp/b2r3_out/*.jsonl`. No production source files changed.

## 12. Exact commands

```bash
gcc -O2 -Wall -Wextra -o /home/ubuntu/b2r3_harness /home/ubuntu/b2r3_harness.c -lsqlite3 -pthread
/home/ubuntu/b2r3_harness 100 > /tmp/b2r3_run.log 2>&1
tail -60 /tmp/b2r3_run.log
```
