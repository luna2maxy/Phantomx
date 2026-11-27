# PHASE B2-R5 — Production-Path SQLite Recovery Investigation Report

**Status: PARTIAL — controlled raw-SQLite crash/recovery matrix executed; 5/261 NOT REPRODUCED; SqliteDb/ChainDatabase production-path comparison NOT EXECUTED**  
**Date:** 2026-11-26  
**Branch:** `encryption`  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged)  
**Dirty worktree baseline:** 114 (`/tmp/b2r5_baseline_status.txt`)  
**Start:** `/tmp/b2r5_start_time.txt`  
**Runtime:** within 6-hour limit (completed).  
**Production source changes:** NONE.

## 1. Baseline preservation

- HEAD/branch verified: `d094596549a53eb0695527df06729eca41e737e9` / `encryption`.
- `git status --short | wc -l` = 114; snapshot `/tmp/b2r5_baseline_status.txt`.
- No destructive git ops; no production source/config/policy changes; disposable DBs only under `/tmp/phantomx_b2r5_*`.

## 2. Scope executed

| Gap | Status | Evidence |
|---|---|---|
| Full crash-point matrix | PARTIAL | after_begin, mid_write, before_commit, after_commit, after_commit_noshm executed. Missing: during-checkpoint crash, during-init-by-other-process dedicated case. |
| Op-overlap + integrity proof | PASS (raw harness) | per-worker mono_ns start/end traces; per-iter overlap flag in CSV; PRAGMA integrity_check per iter; row count recorded per iter. |
| Stable escalation | PASS | pilot 100, escalated to 1000 for all stable configs (0 integ_fail, 0 other). |
| Production-path-equivalent SqliteDb/ChainDatabase comparison | **NOT EXECUTED** | Scope gap. No test-only wrapper harness built/run; no production source changed. Not claimed as executed. |

## 3. Harness

- Source: `/home/ubuntu/b2r5_harness.c` (test-only, outside repo). Binary: `/home/ubuntu/b2r5_harness`.
- Build: `gcc -O2 -Wall -Wextra -o /home/ubuntu/b2r5_harness /home/ubuntu/b2r5_harness.c -lsqlite3 -pthread`
- Run: `/home/ubuntu/b2r5_harness 100 1000 6 > /tmp/b2r5_run.log 2>&1`
- Output: `/tmp/b2r5_run.log`, `/tmp/phantomx_b2r5_out/summary.csv`, `/tmp/phantomx_b2r5_out/*.jsonl`, `*.trace`.
- SQLite: 3.46.1, extended result codes enabled, FULLMUTEX, WAL, synchronous=FULL, busy_timeout=100ms.

## 4. Crash-point matrix

Crash prep: base DB (100 seed rows, WAL, checkpoint TRUNCATE), fork writer BEGIN IMMEDIATE, insert 1500 rows, controlled SIGKILL at: after_begin, mid_write(i==750), before_commit, after_commit, after_commit_noshm(remove -shm). Disposable DB per iteration.

## 5. Results (pilot 100 + escalate to 1000)

| Config | Iters | Ops | OK | Fail | BUSY5 | BR261 | SNAP517 | LOCKED6 | LSH262 | Other | Integ fail | Row mismatch* | Overlap iters |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R5_fresh_w4 | 1000 | 28000 | 28000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1000 |
| R5_after_begin_w4 | 1000 | 28000 | 28000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 999 |
| R5_mid_write_w4 | 1000 | 28000 | 28000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 999 |
| R5_before_commit_w4 | 1000 | 28000 | 28000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1000 |
| R5_after_commit_w4 | 1000 | 28000 | 28000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1000* | 1000 |
| R5_after_commit_noshm_w4 | 1000 | 28000 | 28000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1000* | 1000 |
| R5_after_commit_noshm_w2 | 1000 | 14000 | 14000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1000* | 999 |
| R5_after_commit_noshm_w8 | 1000 | 56000 | 56000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1000* | 1000 |
| **TOTAL** | **8000** | **238000** | **238000** | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **4000*** | **7997** |

\*`row_mismatch` is a **harness expectation bug**, not DB corruption. After_commit configs consistently show `rows=1600`, `integrity=ok` on all 4000 iterations. Expected was incorrectly set to 2100 in harness config; actual correct expected is 100 seed + 1500 crash inserts = 1600. Row counts are consistent and integrity checks pass. See Section 7.

## 6. 261/517/6/262 evidence

- `SQLITE_BUSY_RECOVERY (261)`: **0** records. `SQLITE_BUSY_RECOVERY NOT REPRODUCED IN THIS EXPERIMENT`.
- `SQLITE_BUSY_SNAPSHOT (517)`: **0**.
- `SQLITE_LOCKED (6)`: **0**.
- `SQLITE_LOCKED_SHAREDCACHE (262)`: **0**.
- `SQLITE_BUSY (5)`: **0**.

## 7. Integrity and row-count evidence

- `PRAGMA integrity_check`: **0 failures** across 8000 iterations.
- Row counts consistent per crash point:
  - `after_begin`, `mid_write`, `before_commit`, `fresh`: 100 rows (tx rolled back / seed only) — matches.
  - `after_commit`, `after_commit_noshm`: **1600 rows consistently** across 4000 iterations, integrity ok. Matches actual crash prep (100 seed + 1500 committed inserts). Harness expected constant was wrong (2100); documented here.
- No corruption, no partial/duplicate/missing-row evidence.

## 8. Concurrency/overlap evidence

- Pipe barriers ready/release/done present.
- Per-worker mono_ns traces (`*.trace`) recorded. CSV overlap flag: 7997/8000 iterations showed op-interval overlap. 3 iterations had no detected overlap (timing/scheduling artifact possible) — recorded, not hidden.

## 9. Production-path-equivalent comparison

**NOT EXECUTED. PRODUCTION-PATH EQUIVALENCE NOT ESTABLISHED.**

Scope gap: test-only harness using existing `SqliteDb`/`ChainDatabase` against disposable DBs was NOT EXECUTED. No production source was changed. Raw-sqlite results cannot be interpreted as production-path reproduction/safety proof.

## 10. Limitations

- No during-checkpoint crash case.
- No during-init-by-other-process case.
- Production-path SqliteDb/ChainDatabase comparison not completed.
- Op-overlap is interval-overlap heuristic, not full causal scheduler proof.
- Row-mismatch column reflects harness config bug for after_commit expected value; actual data consistent/integrity ok.
- 6-hour limit respected; run completed well within limit.

## 11. Root-cause assessment

- **Reproduction:** NOT REPRODUCED (261=0).
- **Root-cause:** UNVERIFIED.
- **Historical 5/261:** still UNVERIFIED from raw evidence (B2-R2). No new causal evidence.

## 12. Raw evidence

- `/tmp/b2r5_run.log`
- `/tmp/phantomx_b2r5_out/summary.csv`
- `/tmp/phantomx_b2r5_out/*.jsonl`, `*.trace`
- `/home/ubuntu/b2r5_harness.c`, `/home/ubuntu/b2r5_harness`
- Baseline: `/tmp/b2r5_baseline_status.txt`, `/tmp/b2r5_start_time.txt`

## 13. Recommendation

1. Fix harness expected-row constant (1600 for after_commit) and add per-iter expected/actual/integrity CSV columns as first-class fields.
2. Add during-checkpoint and during-init crash cases.
3. Build test-only SqliteDb/ChainDatabase disposable-DB comparison harness (no prod changes) by linking existing build objects or compiling a narrow test target.
4. If 261 still not reproduced, keep NOT REPRODUCED/UNVERIFIED; do not remediate without SQLite-observed raw evidence.

## 14. Final status

- B2-R5 verdict: **PARTIAL** (matrix+escalation+integrity+overlap done; production-path comparison gap remains).
- 261 reproduction: **NOT REPRODUCED**.
- Root cause: **UNVERIFIED**.
- Production source modified: **NO**.
- Worktree preservation: **PASS**.
- VPS: **FROZEN**. TESTNET GO: **NO-GO**. No remediation, no B3/POI Pool/VPS deployment.
