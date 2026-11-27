# PHANTOMX — TB-001 Final Multi-Process Evidence Audit

**Date:** 2026-11-26  
**Mode:** READ-ONLY INDEPENDENT AUDIT. No code changes, no experiments rerun.  
**Evidence dir:** `/tmp/phx_tb001_multiproc_20261126/`  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9`, dirty 115.  
**Auditor:** Agent (independent review).

---

## 1. Executive verdict

**PARTIAL — NOT CLOSED.**

The multi-process validation provides **stronger evidence** that Alternative F addresses the reproduced BR261 failure under multi-process contention (pre-fix: 13 BR261 in 2000 opens; post-fix: 0 BR261 in 2000 opens; 0 integrity failures both). However, root cause remains **LIKELY/UNCONFIRMED** because:

1. No isolated single-variable test (three changes applied together).
2. No per-iteration error logging (84 "other_fail" pre-fix cannot be classified beyond counts).
3. No empirical concurrency proof via timestamps (barrier design ensures overlap but logs don't record it).
4. No pre-fix control under identical timing (different runs).

**TB-001 status: PARTIAL (implemented + multi-process validated, root cause likely).**  
**TESTNET: NO-GO. VPS: FROZEN.**

---

## 2. Repository/source/binary identity

### Pre-fix (control)
- **Source:** `/tmp/phx_tb001_multiproc_20261126/pre_src/` (rsync copy, Alternative F reverted in `src/database/sqlite_db.cpp`).
- **Binary:** `/tmp/phx_tb001_multiproc_20261126/pre_worker` SHA256 `2502e8fc21f139e8d172b4100aabe74c1afb6e1df4513f4fb82689e0ae1282c4`.
- **Provenance:** Alternative F markers absent from pre-fix source (verified via grep). Original pragma order (WAL first, busy_timeout last), no retry, no extended result codes.
- **Worktree HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged).

### Post-fix (treatment)
- **Source:** `/home/ubuntu/Documents/GitHub/PhantomX-Developed` (current worktree, Alternative F present).
- **Binary:** `/tmp/phx_tb001_multiproc_20261126/post_worker` SHA256 `63df2f3bb0c80d6d311900b9aed376e9ca08564859f2a372662c9be1cefb8d7f`.
- **Worktree binary:** `build-ubuntu/bin/phantomxd` SHA256 `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`.
- **Provenance:** Alternative F markers present (verified via grep: `extended_result_codes`, `Alternative F`, `retry exhausted`).

### No binary mixing
- Pre-fix and post-fix binaries are distinct (different SHA256). ✓
- Each run used the correct binary. ✓

---

## 3. Concurrency proof

### Design
- 4 independent processes per run, each a separate `mp_worker` binary invocation.
- Shared DB path per run (e.g., `preflash_db/test.db`).
- Barrier: workers wait for a "go" file created by the coordinator after all 4 spawned.
- 5ms jitter per worker (`sleep 5 * wid`) to stagger starts while ensuring overlap.
- Each worker runs 250 iterations of `ChainDatabase::open()`.

### Evidence
- Process PIDs recorded in `worker_*.pid` files.
- Worker JSON files confirm 4 distinct workers (worker 0-3) per run.
- Sum of worker iters = 1000 per run (no lost iterations).
- Shared DB path confirmed (single `test.db` per run).

### Limitation
- **No per-iteration timestamps logged.** The `mp_worker` does not record timestamps or PIDs per iteration. Concurrency is by design (barrier + shared DB) but not empirically proven via timestamps.
- **No process overlap proof beyond design.** The barrier ensures simultaneous start, but the actual execution overlap is inferred, not measured.

**Concurrency proof: PARTIAL (design ensures overlap; no timestamp evidence).**

---

## 4. Iteration accounting

### Pre-fix Run 1
| Worker | Iters | OK | BR261 | Other fail | Integ fail |
|---:|---:|---:|---:|---:|---:|
| 0 | 250 | 238 | 0 | 12 | 0 |
| 1 | 250 | 232 | 1 | 17 | 0 |
| 2 | 250 | 238 | 2 | 10 | 0 |
| 3 | 250 | 241 | 0 | 9 | 0 |
| **Total** | **1000** | **949** | **3** | **48** | **0** |

### Pre-fix Run 2
| Worker | Iters | OK | BR261 | Other fail | Integ fail |
|---:|---:|---:|---:|---:|---:|
| 0 | 250 | 237 | 1 | 12 | 0 |
| 1 | 250 | 238 | 3 | 9 | 0 |
| 2 | 250 | 243 | 3 | 4 | 0 |
| 3 | 250 | 236 | 3 | 11 | 0 |
| **Total** | **1000** | **954** | **10** | **36** | **0** |

### Post-fix Run 1
| Worker | Iters | OK | BR261 | Other fail | Integ fail |
|---:|---:|---:|---:|---:|---:|
| 0 | 250 | 250 | 0 | 0 | 0 |
| 1 | 250 | 250 | 0 | 0 | 0 |
| 2 | 250 | 250 | 0 | 0 | 0 |
| 3 | 250 | 250 | 0 | 0 | 0 |
| **Total** | **1000** | **1000** | **0** | **0** | **0** |

### Post-fix Run 2
| Worker | Iters | OK | BR261 | Other fail | Integ fail |
|---:|---:|---:|---:|---:|---:|
| 0 | 250 | 250 | 0 | 0 | 0 |
| 1 | 250 | 250 | 0 | 0 | 0 |
| 2 | 250 | 250 | 0 | 0 | 0 |
| 3 | 250 | 250 | 0 | 0 | 0 |
| **Total** | **1000** | **1000** | **0** | **0** | **0** |

### Iteration accounting
- Pre-fix total: 2000 opens (1000 + 1000). ✓ (no lost iterations)
- Post-fix total: 2000 opens (1000 + 1000). ✓ (no lost iterations)
- No duplicate worker IDs. ✓
- All workers completed (exit 0). ✓
- Sum of worker iters = 1000 per run. ✓

**Iteration accounting: PASS.**

---

## 5. BR261 audit

### Pre-fix
- **13 BR261 events** across 2 runs (3 + 10).
- Distribution: worker 1 (run 1: 1, run 2: 3), worker 2 (run 1: 2, run 2: 3), worker 3 (run 2: 3), worker 0 (run 2: 1).
- All BR261 occurred during `ChainDatabase::open` (per historical evidence, all at open/init pragma stage).

### Post-fix
- **0 BR261 events** across 2 runs (0 + 0).

### Classification
- BR261 (primary 5, extended 261) is classified as **SQLITE_BUSY_RECOVERY** — a transient lock contention error during WAL initialization/recovery.
- All 13 pre-fix BR261 are **transient** (not permanent; retry would likely succeed).
- No BR261 in post-fix indicates the retry mechanism (Alternative F Change 2) and/or reordering (Change 1) mitigated the issue.

### Limitation
- The `mp_worker` does not log per-iteration error details (primary/extended codes, stage, message). Only counts are recorded. Therefore, the 13 BR261 and 84 other_fail cannot be individually classified beyond the aggregate count. The worker's error classification relies on `c.error().to_string()` containing "261" (string match), which is fragile.

**BR261 audit: PARTIAL (counts verified, but no per-event error detail logged).**

---

## 6. Other-failure audit

### Pre-fix
- **84 other_fail** across 2 runs (48 + 36).
- These include non-BR261 open failures (likely SQLITE_BUSY, SQLITE_LOCKED, or other transient errors).
- No classification beyond count (mp_worker doesn't log error details).

### Post-fix
- **0 other_fail** across 2 runs.

### Assessment
- The 84 other_fail are likely transient lock contention errors (SQLITE_BUSY, SQLITE_LOCKED) that occurred under multi-process contention. They are NOT integrity failures (integ_fail=0 in all runs).
- The reduction from 84 to 0 suggests Alternative F's retry mechanism (Change 2) also mitigates non-BR261 transient errors.
- **Cannot classify individually** due to missing per-iteration error logging. Marked as **UNVERIFIED** for individual classification.

**Other-failure audit: UNVERIFIED (counts recorded, individual classification not possible).**

---

## 7. Integrity audit

### Pre-fix
- Integrity failures: 0 (both runs).
- Post-run DB integrity check (Python sqlite3 `PRAGMA integrity_check`): `ok` for all 4 DBs (preflash_db, preflash_db2).

### Post-fix
- Integrity failures: 0 (both runs).
- Post-run DB integrity check: `ok` for all 4 DBs (postflash_db, postflash_db2).

### Additional verification
- All DBs have `tip_height=0` (stress test opens/closes without applying blocks, so chain stays at genesis). This is expected for the stress test (no block application).
- No data corruption or missing records.

**Integrity audit: PASS (0 failures in all runs, PRAGMA integrity_check = ok).**

---

## 8. Pre/post comparability

### Comparable factors
- Same worker count (4), iterations (250 each), total (1000 per run).
- Same DB path structure (single shared DB per run).
- Same barrier mechanism (go file + 5ms jitter).
- Same machine, same SQLite version, same build flags (Release, -O2).
- Same stress_open.cpp harness (only binary differs).

### Non-comparable factors
- **Different DB files:** pre and post used separate DB paths (preflash_db vs postflash_db). Fresh DBs each run, so no stale state.
- **Timing:** Different runs at different times; contention patterns may vary. Run 2 pre-fix had more BR261 (10 vs 3) than Run 1, suggesting timing-dependent.
- **No single-variable control:** Three changes applied together (reorder + retry + extended codes). Cannot attribute improvement to any single change.
- **No pre-fix control on post-fix DB:** Each version tested on its own DB.

### Assessment
- The pre/post comparison is **reasonably fair** (same harness, same parameters, same machine). The consistent 0 BR261 in post-fix across 2 runs (2000 opens) vs 13 BR261 in pre-fix (2000 opens) is strong evidence.
- However, the comparison is **not a controlled experiment** (no single-variable isolation, no identical timing).

**Pre/post comparability: PASS (with documented limitations).**

---

## 9. Evidence SHA256 inventory

| Artifact | SHA256 |
|---|---|
| pre_worker binary | `2502e8fc21f139e8d172b4100aabe74c1afb6e1df4513f4fb82689e0ae1282c4` |
| post_worker binary | `63df2f3bb0c80d6d311900b9aed376e9ca08564859f2a372662c9be1cefb8d7f` |
| worktree binary | `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a` |
| stress_open.cpp | (in `/tmp/phx_tb001_impl_20261126/`) |
| mp_worker.cpp | (in `/tmp/phx_tb001_multiproc_20261126/`) |
| pre_results/worker_*.json | (4 files, preserved) |
| post_results/worker_*.json | (4 files, preserved) |
| pre_results2/worker_*.json | (4 files, preserved) |
| post_results2/worker_*.json | (4 files, preserved) |
| Full inventory | `/tmp/phx_tb001_multiproc_20261126/sha256_inventory.txt` (376 entries) |

---

## 10. Remediation effectiveness assessment

**Alternative F is EFFECTIVE under tested conditions.**

- Pre-fix: 13 BR261 in 2000 opens (0.65% failure rate).
- Post-fix: 0 BR261 in 2000 opens (0% failure rate).
- Other failures: 84 → 0 (significant improvement in startup reliability).
- Integrity: 0 failures in both (no data corruption in either case).
- All regression tests pass (unit 137/137, CTest 11/11, ASAN 0 findings).

The evidence strongly suggests Alternative F addresses the reproduced BR261 failure under multi-process contention.

---

## 11. Root-cause confidence

**LIKELY (unchanged, not CONFIRMED).**

Despite strong remediation effectiveness evidence, root cause remains LIKELY because:
1. Three changes applied simultaneously (reorder + retry + extended codes) — no isolated single-variable test.
2. No pre-fix control under identical timing (different runs).
3. The improvement could be partially timing-dependent.
4. The original repro was 7 BR261 in Layer C pilot; this test reproduced 13 in 2000 opens (different scale).

**Root cause should NOT be labeled CONFIRMED.** The evidence supports "Alternative F addresses the reproduced failure" but not "the root cause is confirmed."

---

## 12. Remaining acceptance gaps

| Criterion | Status | Gap |
|---|---|---|
| 1. Root cause confirmed | **PARTIAL** | No isolated single-variable test; three changes together |
| 2. Minimal fix applied | PASS | Source diff verified |
| 3. 0 BR261 in ≥1000 opens | PASS | 2000 opens, 0 BR261 (2 runs) |
| 4. Integrity 0 | PASS | 0 integ_fail all runs |
| 5. No regressions | PASS | Unit/CTest/E2E/ASAN all pass |
| 6. ASAN/UBSAN clean | PASS | 0 findings |
| 7. Independent evidence audit | PARTIAL | This audit; multi-process harness methodology not independently reviewed |
| 8. Rollback documented and tested | PASS (documented, not executed) | Rollback not executed (by design) |

**Acceptance: 6/8 PASS, 2 PARTIAL.**

---

## 13. TB-001 closure recommendation

**Do NOT close TB-001.**

- Root cause remains LIKELY/UNCONFIRMED.
- Remediation effectiveness is strong (pre-fix reproduces, post-fix eliminates, 2000 opens each).
- Acceptance criteria 1 and 7 are PARTIAL.
- Multi-process validation is a significant improvement over the single-process stress test.
- Remaining gap: isolated single-variable test to confirm which change is most effective (optional, owner decision).

**Recommended status: PARTIAL (implemented + multi-process validated, root cause likely).**

TB-001 remains OPEN/BLOCKED for mainnet.  
TESTNET: NO-GO. VPS: FROZEN. No deployment authorization.  
No code changes made during this audit. STOP and await owner decision.