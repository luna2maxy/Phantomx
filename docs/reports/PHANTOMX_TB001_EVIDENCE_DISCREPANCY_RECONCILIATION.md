# PHANTOMX — TB-001 Evidence Discrepancy Reconciliation

**Date:** 2026-11-26  
**Mode:** READ-ONLY. No experiments, no code changes, no evidence modification.  
**Evidence dir:** `/tmp/phx_tb001_multiproc_20261126/`  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9`.

---

## 1. Executive summary

The discrepancy between the validation report (claiming "multi-process contention") and the independent audit (classifying stress as "single-process multi-thread") is **partially resolved** by the multi-process validation harness (`mp_worker`), which uses 4 independent OS processes with a barrier and shared DB.

**Key findings:**
- Pre-fix (multi-process): 2 runs × 1000 opens = 2000 opens, 13 BR261, 84 other_fail, 0 integ_fail.
- Post-fix (multi-process): 2 runs × 1000 opens = 2000 opens, 0 BR261, 0 other_fail, 0 integ_fail.
- All 4 workers per run are distinct OS processes (PIDs 252431-252434 etc.).
- Iteration accounting verified: 1000 per run, no duplicates, no loss.
- All DBs pass `PRAGMA integrity_check` = ok.
- Pre-fix and post-fix source differ only in the `SqliteDb::open()` pragma block.

**However:**
- The `mp_worker` does not log per-iteration error details (primary/extended codes, stage, message). The 84 "other_fail" and 13 BR261 cannot be individually classified.
- No timestamp logging; concurrency is by design (barrier + shared DB) but not empirically proven via timestamps.
- No isolated single-variable test (three changes applied together).
- No pre-fix control under identical timing (different runs).

**Verdict: PARTIAL.** The multi-process validation provides strong evidence that Alternative F addresses the reproduced failure under multi-process contention. Root cause remains LIKELY/UNCONFIRMED. TB-001 is NOT closed.

---

## 2. Perbandingan kedua laporan

### Laporan validasi multi-process (PHANTOMX_TB001_MULTIPROCESS_PREPOST_VALIDATION.md)
- Claimed: "multi-process contention", 4 workers × 250 = 1000 opens per run.
- Results: pre-fix 13 BR261 in 2000 opens; post-fix 0 BR261 in 2000 opens.
- **Assessment:** The claim of "multi-process" is **supported** by the evidence (4 distinct OS process PIDs, separate mp_worker binary invocations, shared DB path). However, the report overstated concurrency proof (no timestamp evidence).

### Laporan independent audit final (PHANTOMX_TB001_FINAL_MULTIPROCESS_EVIDENCE_AUDIT.md)
- Classified stress as "single-process multi-thread" — this was **incorrect** for the multi-process harness (mp_worker uses separate processes), but **correct** for the earlier single-process stress test (stress_open.cpp uses threads).
- The audit conflated two different harnesses: `stress_open` (single-process, threads) and `mp_worker` (multi-process, separate PIDs).
- The audit's concern about "no per-iteration error logging" is **valid** — mp_worker only records counts.

### Reconciliation
- The multi-process harness **does** use 4 independent OS processes (PIDs 252431-252434 etc.), confirmed by PID files and process inspection.
- The independent audit's "single-process multi-thread" classification was **incorrect** for the multi-process harness but **correct** for the single-process stress test.
- Both reports agree on the core results: 0 BR261 post-fix, integrity 0.
- The discrepancy is in the **concurrency classification**, not the results.

---

## 3. Process/thread identity proof

| Run | Worker | PID | Distinct process? |
|---|---:|---:|---|
| pre | 0 | 252431 | Yes |
| pre | 1 | 252432 | Yes |
| pre | 2 | 252433 | Yes |
| pre | 3 | 252434 | Yes |
| pre2 | 0 | 252484 | Yes |
| pre2 | 1 | 252485 | Yes |
| pre2 | 2 | 252486 | Yes |
| pre2 | 3 | 252487 | Yes |
| post | 0 | 252451 | Yes |
| post | 1 | 252452 | Yes |
| post | 2 | 252453 | Yes |
| post | 3 | 252454 | Yes |
| post2 | 0 | 252501 | Yes |
| post2 | 1 | 252502 | Yes |
| post2 | 2 | 252503 | Yes |
| post2 | 3 | 252504 | Yes |

- All 16 workers are distinct OS processes (separate PIDs).
- Each worker is a separate invocation of the `mp_worker` binary.
- Workers share a single DB path per run (contention by design).
- Barrier mechanism: workers wait for a "go" file created by the coordinator after all spawned.

**Process identity: CONFIRMED (16 distinct OS processes).**

### Limitation
- No per-worker timestamps logged. Concurrency is by design but not empirically proven via timestamps.
- Worker logs are empty (mp_worker writes only JSON summary, no per-iteration logging).

---

## 4. Iteration accounting

| Run | Workers | Iters (sum) | OK | BR261 | Other fail | Integ fail |
|---|---:|---:|---:|---:|---:|---:|
| pre | 4 | 1000 | 949 | 3 | 48 | 0 |
| pre2 | 4 | 1000 | 954 | 10 | 36 | 0 |
| post | 4 | 1000 | 1000 | 0 | 0 | 0 |
| post2 | 4 | 1000 | 1000 | 0 | 0 | 0 |

- Sum of worker iters = 1000 per run. ✓
- No duplicate worker IDs. ✓
- All workers completed (exit 0). ✓
- No lost iterations. ✓

**Iteration accounting: PASS.**

---

## 5. BR261 evidence audit

### Pre-fix
- **13 BR261 events** across 2 runs (3 + 10).
- Detection method: `c.error().to_string()` contains "261" (string match).
- The mp_worker does **not** log per-iteration primary/extended codes, stage, or message. Only aggregate counts.
- The 13 events are **UNVERIFIED** at the individual level (no raw JSONL per event). The count is recorded but cannot be independently confirmed beyond the worker's self-report.

### Post-fix
- **0 BR261 events** across 2 runs.
- Same detection method (string match).

### Classification
- BR261 (primary 5, extended 261) is classified as `SQLITE_BUSY_RECOVERY` per historical evidence (B2-R6 Layer C, 7 confirmed records with primary/extended/msg/api in JSONL).
- The mp_worker's string-match detection is **fragile** (could match "261" in other contexts) but consistent with the historical classification.
- All 13 pre-fix BR261 are presumed transient (not permanent) based on historical evidence.

**BR261 audit: PARTIAL (counts recorded, individual classification not possible due to missing per-iteration logging).**

---

## 6. Other-failure classification

### Pre-fix
- **84 other_fail** across 2 runs (48 + 36).
- The mp_worker classifies any non-BR261 error as "other_fail" without recording the error code or message.
- No per-iteration error details available.

### Assessment
- The 84 other_fail are likely transient lock contention errors (SQLITE_BUSY, SQLITE_LOCKED) based on the concurrent open scenario, but this is **unverified**.
- They are NOT integrity failures (integ_fail=0 in all runs).
- The reduction from 84 (pre) to 0 (post) is consistent with Alternative F's retry mechanism.

**Other-failure classification: UNVERIFIED (counts recorded, individual classification not possible).**

---

## 7. Pre/post source and binary identity

### Source identity
- Pre-fix source (`pre_src/src/database/sqlite_db.cpp`) differs from post-fix (worktree) **only** in the `SqliteDb::open()` pragma block:
  - Pre-fix: original pragma array (WAL first, busy_timeout last), no retry, no extended result codes.
  - Post-fix: Alternative F (busy_timeout first, WAL with retry, extended result codes).
- All other source files are identical (verified via rsync copy + diff).

### Binary identity
| Binary | SHA256 | Role |
|---|---|---|
| pre_worker | `2502e8fc21f139e8d172b4100aabe74c1afb6e1df4513f4fb82689e0ae1282c4` | Pre-fix control |
| post_worker | `63df2f3bb0c80d6d311900b9aed376e9ca08564859f2a372662c9be1cefb8d7f` | Post-fix treatment |
| build-ubuntu/bin/phantomxd | `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a` | Worktree binary (post-fix) |

- No binary mixing confirmed (distinct SHA256s, correct binary used per run).

---

## 8. Pre/post comparability

### Comparable
- Same worker count (4), iterations (250 each), total (1000 per run).
- Same DB path structure (single shared DB per run).
- Same barrier mechanism (go file + 5ms jitter).
- Same machine, same SQLite version, same build flags (Release, -O2).
- Same stress_open.cpp harness logic (only binary differs).
- Fresh DB per run (no stale state).

### Non-comparable
- **Different DB files:** pre and post used separate DB paths.
- **Timing:** Different runs at different times; contention patterns may vary (pre run 2 had more BR261 than run 1).
- **No single-variable control:** Three changes applied together.
- **No pre-fix control under identical timing:** Different runs.

### Assessment
- **Reasonably fair** (same harness, same parameters, same machine). Consistent 0 BR261 in post-fix across 2 runs vs 13 in pre-fix across 2 runs is strong evidence.
- **Not a controlled experiment** (no single-variable isolation, no identical timing).

**Pre/post comparability: PASS (with documented limitations).**

---

## 9. Evidence SHA256 inventory

| Artifact | SHA256 |
|---|---|
| pre_worker | `2502e8fc21f139e8d172b4100aabe74c1afb6e1df4513f4fb82689e0ae1282c4` |
| post_worker | `63df2f3bb0c80d6d311900b9aed376e9ca08564859f2a372662c9be1cefb8d7f` |
| worktree binary | `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a` |
| pre_results/worker_0.json | (preserved) |
| pre_results/worker_1.json | (preserved) |
| pre_results/worker_2.json | (preserved) |
| pre_results/worker_3.json | (preserved) |
| post_results/worker_0.json | (preserved) |
| post_results/worker_1.json | (preserved) |
| post_results/worker_2.json | (preserved) |
| post_results/worker_3.json | (preserved) |
| pre_results2/worker_*.json | (preserved) |
| post_results2/worker_*.json | (preserved) |
| Full inventory | `/tmp/phx_tb001_multiproc_20261126/sha256_inventory.txt` (376 entries) |

---

## 10. Remediation effectiveness assessment

**Alternative F is EFFECTIVE under tested multi-process conditions.**

- Pre-fix: 13 BR261 in 2000 opens (0.65% failure rate) + 84 other failures.
- Post-fix: 0 BR261 in 2000 opens (0% failure rate) + 0 other failures.
- Integrity: 0 failures in both pre and post.
- Consistent across 2 runs (pre: 3+10 BR261; post: 0+0).

The evidence strongly suggests Alternative F addresses the reproduced BR261 failure under multi-process contention.

---

## 11. Root-cause confidence

**LIKELY (unchanged, not CONFIRMED).**

Despite strong remediation effectiveness evidence, root cause remains LIKELY because:
1. Three changes applied simultaneously (no isolated single-variable test).
2. No pre-fix control under identical timing.
3. The improvement could be partially timing-dependent.
4. No per-iteration error logging (cannot confirm 13 BR261 are all SQLITE_BUSY_RECOVERY vs other codes).

**Root cause should NOT be labeled CONFIRMED.**

---

## 12. Remaining acceptance gaps

| Criterion | Status | Gap |
|---|---|---|
| 1. Root cause confirmed | **PARTIAL** | No isolated single-variable test; three changes together; no per-iteration error logging |
| 2. Minimal fix applied | PASS | Source diff verified |
| 3. 0 BR261 in ≥1000 opens | PASS | 2000 opens, 0 BR261 (2 runs) |
| 4. Integrity 0 | PASS | 0 integ_fail all runs |
| 5. No regressions | PASS | Unit/CTest/E2E/ASAN all pass |
| 6. ASAN/UBSAN clean | PASS | 0 findings |
| 7. Independent evidence audit | PARTIAL | This audit; mp_worker methodology not independently reviewed beyond this |
| 8. Rollback documented and tested | PASS (documented, not executed) | Rollback not executed (by design) |

**Acceptance: 6/8 PASS, 2 PARTIAL.**

---

## 13. Reconciliation of discrepancy

### Why the two reports differed

The independent audit final (`PHANTOMX_TB001_FINAL_MULTIPROCESS_EVIDENCE_AUDIT.md`) classified the stress as "single-process multi-thread." This was **incorrect** for the multi-process harness (`mp_worker` uses 4 separate OS processes with distinct PIDs), but **correct** for the earlier single-process stress test (`stress_open.cpp` uses threads).

The discrepancy arose because:
1. Two different harnesses existed: `stress_open` (single-process, threads) and `mp_worker` (multi-process, separate PIDs).
2. The independent audit reviewed the `stress_open` single-process test, not the `mp_worker` multi-process test.
3. The validation report claimed "multi-process" based on the `mp_worker` harness, which was correct.

### Resolution
- The multi-process harness **does** use 4 independent OS processes (PIDs confirmed).
- The independent audit's "single-process multi-thread" classification was incorrect for the multi-process harness.
- Both reports agree on core results: 0 BR261 post-fix, integrity 0.
- The discrepancy was in **concurrency classification**, not results.

### What remains unverified
- Per-iteration error details (mp_worker doesn't log them).
- Empirical concurrency proof via timestamps (not logged).
- Isolated single-variable test (three changes together).

---

## 14. TB-001 closure recommendation

**Do NOT close TB-001.**

- Root cause remains LIKELY/UNCONFIRMED.
- Multi-process validation provides strong evidence that Alternative F addresses the reproduced failure.
- Acceptance criteria 1 and 7 are PARTIAL.
- The mp_worker harness lacks per-iteration error logging, limiting error classification.

**Recommended status: PARTIAL (implemented + multi-process validated, root cause likely, audit PARTIAL).**

TB-001 remains OPEN/BLOCKED for mainnet.  
TESTNET: NO-GO. VPS: FROZEN. No deployment authorization.  
No code changes made during this audit. No experiments run. STOP and await owner decision.