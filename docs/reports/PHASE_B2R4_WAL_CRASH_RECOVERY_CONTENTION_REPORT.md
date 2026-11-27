# PHASE B2-R4 — WAL Crash Recovery & Contention Final Result Audit

**Overall verdict: NOT REPRODUCED**  
**Root-cause status: UNVERIFIED**  
**Date:** 2026-11-26  
**Mode:** STRICT AUDIT MODE — NO IMPLEMENTATION  
**Branch:** `encryption`  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (verified)  
**Dirty worktree:** 114 entries (verified; `/tmp/b2r4_baseline_status.txt`)  
**Production source modified:** NO  
**Worktree preservation:** PASS

## 1. Executive summary

Audit of B2-R3 and B2-R4 raw evidence found no `SQLITE_BUSY_RECOVERY (primary 5, extended 261)` records. B2-R3 raw scan: 4400 JSONL files, 30367 ops, BUSY5=411, BR261=0, SNAP517=0, LOCKED6=0, LOCKED_SHAREDCACHE262=0, other=0. B2-R4 raw scan: 2600 JSONL files, 17800 ops, BUSY5=0, BR261=0, SNAP517=0, LOCKED6=0, LOCKED_SHAREDCACHE262=0, other=0, INTEG_FAIL=0. R4 actual executed matrix differs from the prompt's illustrative table (6 configs executed, not 8); actual executed configs are reported below. No 261 reproduction; root cause remains UNVERIFIED.

## 2. Baseline and worktree preservation

| Item | Value |
|---|---|
| Current HEAD | `d094596549a53eb0695527df06729eca41e737e9` |
| Current branch | `encryption` |
| `git status --short` count | 114 |
| Baseline snapshot | `/tmp/b2r4_baseline_status.txt` |
| Prior changes preserved | Yes (no destructive git ops) |
| Evidence deleted/overwritten | No |

## 3. B2-R3 audit

**B2-R3 audit verdict: PARTIAL** (raw evidence present and counts verifiable; limitations remain).

| Check | Finding |
|---|---|
| Evidence | `/tmp/b2r3_run.log` present (12 lines), `/tmp/b2r3_out/*.jsonl` 4400 files present, `/home/ubuntu/b2r3_harness.c` present, report `docs/reports/PHASE_B2R3_CONTROLLED_WAL_RECOVERY_REPRODUCTION_REPORT.md` present |
| Configs/iterations | 10 configs x 100 iters = 1000 config-iters, as reported |
| Concurrency | Harness uses pipe barrier ready/release/done (code evidence). JSONL contains per-worker pid/mono_ns/seq/op. Op-level overlap independently proven: **PARTIAL** (barrier present; no separate overlap analysis artifact) |
| Result codes (raw scan) | total=30367 ok=29956 fail=411 busy5=411 br261=0 snap517=0 locked6=0 locked_shared262=0 other=0 |
| SQLite vs harness errors | Failures classified by sqlite rc/primary/extended in JSONL; 411 are BUSY 5/5. No simulated 261 injected. |
| Integrity | Harness includes PRAGMA integrity_check calls and coordinator post-iter check; per-iter integrity summary column absent. No corruption reported. **PARTIAL** |
| Report match | Reported totals match raw scan exactly. |

## 4. B2-R4 configuration matrix (actual executed)

| Configuration | Workers | Reader | Iters expected | Iters completed | Failed/Skipped/Incomplete | Evidence files |
|---|---:|---:|---:|---:|---|---:|
| R4B_crash_noshm_w2 | 2 | 0 | 100 | 100 (0-99) | 0/0/0 | 200 JSONL |
| R4B_crash_noshm_w4 | 4 | 0 | 100 | 100 | 0/0/0 | 400 JSONL |
| R4B_crash_noshm_w8 | 8 | 0 | 100 | 100 | 0/0/0 | 800 JSONL |
| R4B_crash_keep_shm_w4 | 4 | 0 | 100 | 100 | 0/0/0 | 400 JSONL |
| R4C_reader_crash_w2 | 2 | 1 | 100 | 100 | 0/0/0 | 300 JSONL |
| R4C_reader_crash_w4 | 4 | 1 | 100 | 100 | 0/0/0 | 500 JSONL |

**Discrepancy with prompt table:** prompt listed 8 configs (crash noshm 2/4/8, crash shm 2/4/8, reader+crash 2/4). Actual run executed 6 configs; crash-shm w2/w8 not executed. Total executed config-iters = 600, not 800. This is reported as actual; not backfilled.

## 5. SQLite result code distribution

### B2-R3 (raw scan)
| Code | Count |
|---|---:|
| SQLITE_BUSY (5) | 411 |
| SQLITE_BUSY_RECOVERY (261) | 0 |
| SQLITE_BUSY_SNAPSHOT (517) | 0 |
| SQLITE_LOCKED (6) | 0 |
| SQLITE_LOCKED_SHAREDCACHE (262) | 0 |
| Other failures | 0 |
| OK/ROW/DONE | 29956 |
| Total ops (excl ready/done/barrier) | 30367 |

### B2-R4 (raw scan)
| Code | Count |
|---|---:|
| SQLITE_BUSY (5) | 0 |
| SQLITE_BUSY_RECOVERY (261) | 0 |
| SQLITE_BUSY_SNAPSHOT (517) | 0 |
| SQLITE_LOCKED (6) | 0 |
| SQLITE_LOCKED_SHAREDCACHE (262) | 0 |
| Other failures | 0 |
| OK/ROW/DONE | 17800 |
| Total ops (excl ready/done/barrier) | 17800 |

Raw grep for `"extended":261` in R4 and R3 JSONL: **0 records**.

## 6. SQLITE_BUSY_RECOVERY occurrence details

`SQLITE_BUSY_RECOVERY NOT REPRODUCED IN THIS EXPERIMENT`

No occurrence found in B2-R3 or B2-R4 raw JSONL. Therefore no 261 occurrence detail rows exist.

## 7. Crash/recovery validation

| Item | Finding | Status |
|---|---|---|
| Crash method | Controlled child process: BEGIN IMMEDIATE, 2000 inserts, COMMIT, `raise(SIGKILL)`; parent waitpid | VERIFIED in harness code |
| Crash point | Post-commit SIGKILL only (controlled approximation) | PARTIAL (not full crash-point matrix) |
| noshm/shm | noshm removes `-shm` after crash; keep_shm retains it | VERIFIED in harness code |
| WAL recovery actually required | Not proven by independent SQLite trace; assumed from crash-WAL prep | UNVERIFIED |
| Recovery contention occurred | Not proven; no 261/5 errors observed | UNVERIFIED |
| DB consistent after | PRAGMA integrity_check post-iter; `INTEG_FAIL` count 0 | PASS (limited) |

## 8. Concurrency validation

| Item | Finding | Status |
|---|---|---|
| Worker readiness barrier | Pipe ready/release/done present in R4 harness | VERIFIED in code |
| Overlapping op intervals | mono_ns/pid recorded in JSONL; independent overlap analysis not produced | PARTIAL |
| Process exit codes | Harness waits for children; no INTEG_FAIL/stderr error; run completed DONE | PARTIAL |

## 9. Database integrity results

| Run | Integrity check | INTEG_FAIL / corruption | Expected vs actual row count |
|---|---|---:|---|
| B2-R3 | PRAGMA integrity_check in harness | 0 reported | Not summarized as per-iter expected/actual column; **PARTIAL** |
| B2-R4 | PRAGMA integrity_check post-iter | 0 | Not summarized as per-iter expected/actual column; **PARTIAL** |

## 10. Raw SQLite vs production-path comparison

**Classification: RAW SQLITE ONLY / PRODUCTION-PATH EQUIVALENCE NOT ESTABLISHED**

Harness uses raw sqlite3 C API, not `SqliteDb`/`ChainDatabase`. Extended codes enabled in harness (prod does not explicitly enable). No commit ambiguity/quota tx/RPC caller path. Raw non-reproduction cannot be taken as production safety evidence.

## 11. Evidence inventory

| Path | Type | Status |
|---|---|---|
| `/tmp/b2r3_run.log` | B2-R3 summary log | Present |
| `/tmp/b2r3_out/*.jsonl` (4400) | B2-R3 events | Present |
| `/home/ubuntu/b2r3_harness.c` | B2-R3 harness source | Present |
| `/tmp/b2r4_run.log` | B2-R4 summary log | Present |
| `/tmp/phantomx_b2r4_out/*.jsonl` (2600) | B2-R4 events | Present |
| `/tmp/b2r4_harness.c`, `/home/ubuntu/b2r4_harness` | B2-R4 harness/binary | Present |
| `docs/reports/PHASE_B2R3_CONTROLLED_WAL_RECOVERY_REPRODUCTION_REPORT.md` | Report | Present |
| `docs/reports/PHASE_B2R4_WAL_CRASH_RECOVERY_CONTENTION_REPORT.md` | Report | Present (this audited update) |

## 12. Missing or unverifiable evidence

- Raw historical 5/261 logs cited by R18/R19 (`/tmp/r18_smoke*`) missing.
- R4 crash-shm w2/w8 configs not executed.
- Op-level overlap proof not generated.
- Per-iteration expected/actual row-count integrity table not generated.
- WAL recovery necessity not proven by SQLite trace.
- Production-path equivalence not established.

## 13. Root-cause assessment

- **Reproduction status:** NOT REPRODUCED.
- **Root-cause status:** UNVERIFIED.
- **Rationale:** No 261 in B2-R3/B2-R4 raw evidence; historical 261 reports unverified from raw artifacts. No causal mechanism established.

## 14. Limitations

- Not all crash points tested.
- Not all prompt-listed R4 configs executed (6/8 actual).
- 100 iterations/config only; not escalated to 500/1000.
- Raw sqlite only; production wrapper untested.
- Barrier exists but overlap proof incomplete.
- Integrity summary lacks expected/actual row counts.

## 15. Final verdict

- **Overall verdict:** NOT REPRODUCED
- **B2-R3 audit verdict:** PARTIAL
- **B2-R4 audit verdict:** PARTIAL (executed and audited; incomplete matrix/limitations)
- **Root-cause status:** UNVERIFIED

## 16. Recommended next investigation step

Before any remediation: build a test-only harness with full crash-point matrix, per-iteration integrity CSV (expected/actual rows), op-overlap proof, 500-1000 iterations for stable configs, and a SqliteDb/ChainDatabase disposable-DB comparison (no production changes). If 261 remains unreproduced, keep status NOT REPRODUCED/UNVERIFIED and do not claim historical bug invalid or resolved.
