# PHANTOMX — TB-001 Multi-Process Pre/Post Remediation Validation

**Date:** 2026-11-26  
**Mode:** Isolated test validation. No production code changes. No worktree modifications.  
**Evidence dir:** `/tmp/phx_tb001_multiproc_20261126/`  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9`, dirty 115.  
**Post-fix binary (worktree):** SHA256 `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`.

---

## 1. Source and binary identity

### Pre-fix (control)
- **Source:** Copy of worktree at `/tmp/phx_tb001_multiproc_20261126/pre_src/`, with Alternative F reverted in `src/database/sqlite_db.cpp` (original pragma order: WAL first, busy_timeout last; no retry; no extended result codes).
- **Binary:** `/tmp/phx_tb001_multiproc_20261126/pre_worker` SHA256 `2502e8fc21f139e8d172b4100aabe74c1afb6e1df4513f4fb82689e0ae1282c4`.
- **Provenance:** Source copied via rsync (excluding build/data dirs); Alternative F block replaced with original pragma array; `extended_result_codes` line removed. Verified: no Alternative F markers in pre-fix source.

### Post-fix (treatment)
- **Source:** Current worktree (`/home/ubuntu/Documents/GitHub/PhantomX-Developed`) with Alternative F in `src/database/sqlite_db.cpp`.
- **Binary:** `/tmp/phx_tb001_multiproc_20261126/post_worker` SHA256 `63df2f3bb0c80d6d311900b9aed376e9ca08564859f2a372662c9be1cefb8d7f`.
- **Provenance:** Compiled against `build-ubuntu/libphantomx_common.a` (which contains the Alternative F code). Verified: Alternative F markers present in source.

---

## 2. Test topology and concurrency proof

- **Workers:** 4 independent processes per run.
- **Iterations:** 250 per worker = 1000 total per run.
- **Database:** Shared single DB path per run (`preflash_db/test.db` / `postflash_db/test.db`).
- **Barrier:** Workers wait for a "go" file before starting; coordinator creates go file after all workers spawned. 5ms jitter per worker to ensure overlap.
- **Concurrency:** 4 processes simultaneously opening the same DB path. OS scheduler ensures overlap.
- **Accounting:** Each worker writes a JSON result file with worker_id, iters, ok, br261, other_fail, integ_fail. Coordinator aggregates. No duplicate or lost iterations (sum of worker iters = 1000 in all runs).

---

## 3. Pre-fix results

### Run 1
```
label=pre workers=4 iters=1000 ok=949 br261=3 other_fail=48 integ_fail=0
worker0: iters=250 ok=238 br261=0 other_fail=12 integ_fail=0
worker1: iters=250 ok=232 br261=1 other_fail=17 integ_fail=0
worker2: iters=250 ok=238 br261=2 other_fail=10 integ_fail=0
worker3: iters=250 ok=241 br261=0 other_fail=9 integ_fail=0
```

### Run 2
```
label=pre2 workers=4 iters=1000 ok=954 br261=10 other_fail=36 integ_fail=0
worker0: iters=250 ok=237 br261=1 other_fail=12 integ_fail=0
worker1: iters=250 ok=238 br261=3 other_fail=9 integ_fail=0
worker2: iters=250 ok=243 br261=3 other_fail=4 integ_fail=0
worker3: iters=250 ok=236 br261=3 other_fail=11 integ_fail=0
```

### Pre-fix combined
| Metric | Run 1 | Run 2 | Total |
|---|---:|---:|---:|
| Iterations | 1000 | 1000 | 2000 |
| OK | 949 | 954 | 1903 |
| BR261 | 3 | 10 | 13 |
| Other fail | 48 | 36 | 84 |
| Integ fail | 0 | 0 | 0 |

**Pre-fix reproduces BR261 (13 events across 2 runs) and has significant other failures (84, likely lock contention).**

---

## 4. Post-fix results

### Run 1
```
label=post workers=4 iters=1000 ok=1000 br261=0 other_fail=0 integ_fail=0
worker0: iters=250 ok=250 br261=0 other_fail=0 integ_fail=0
worker1: iters=250 ok=250 br261=0 other_fail=0 integ_fail=0
worker2: iters=250 ok=250 br261=0 other_fail=0 integ_fail=0
worker3: iters=250 ok=250 br261=0 other_fail=0 integ_fail=0
```

### Run 2
```
label=post2 workers=4 iters=1000 ok=1000 br261=0 other_fail=0 integ_fail=0
worker0: iters=250 ok=250 br261=0 other_fail=0 integ_fail=0
worker1: iters=250 ok=250 br261=0 other_fail=0 integ_fail=0
worker2: iters=250 ok=250 br261=0 other_fail=0 integ_fail=0
worker3: iters=250 ok=250 br261=0 other_fail=0 integ_fail=0
```

### Post-fix combined
| Metric | Run 1 | Run 2 | Total |
|---|---:|---:|---:|
| Iterations | 1000 | 1000 | 2000 |
| OK | 1000 | 1000 | 2000 |
| BR261 | 0 | 0 | 0 |
| Other fail | 0 | 0 | 0 |
| Integ fail | 0 | 0 | 0 |

**Post-fix: 0 BR261, 0 other failures, 100% success (2000/2000), 0 integrity failures.**

---

## 5. SQLite error distribution

### Pre-fix
- BR261 (primary 5, extended 261): 13 events across 2 runs.
- Other failures: 84 (likely SQLITE_BUSY/LOCKED or open failures due to contention).
- All failures at open/init stage (consistent with B2-R6 Layer C audit).
- Integrity failures: 0.

### Post-fix
- BR261: 0.
- Other failures: 0.
- Integrity failures: 0.
- 100% success rate (2000/2000).

---

## 6. Raw evidence index and SHA256

| Evidence | Path | Status |
|---|---|---|
| Pre-fix worker binary | `/tmp/phx_tb001_multiproc_20261126/pre_worker` | SHA256 `2502e8fc21f139e8d172b4100aabe74c1afb6e1df4513f4fb82689e0ae1282c4` |
| Post-fix worker binary | `/tmp/phx_tb001_multiproc_20261126/post_worker` | SHA256 `63df2f3bb0c80d6d311900b9aed376e9ca08564859f2a372662c9be1cefb8d7f` |
| Pre-fix run 1 results | `/tmp/phx_tb001_multiproc_20261126/pre_results/` | worker_0..3.json, summary.txt |
| Pre-fix run 2 results | `/tmp/phx_tb001_multiproc_20261126/pre_results2/` | worker_0..3.json, summary.txt |
| Post-fix run 1 results | `/tmp/phx_tb001_multiproc_20261126/post_results/` | worker_0..3.json, summary.txt |
| Post-fix run 2 results | `/tmp/phx_tb001_multiproc_20261126/post_results2/` | worker_0..3.json, summary.txt |
| Pre-fix source | `/tmp/phx_tb001_multiproc_20261126/pre_src/` | Alternative F reverted |
| Post-fix source (worktree) | `/home/ubuntu/Documents/GitHub/PhantomX-Developed` | Alternative F present |
| Worktree binary SHA256 | `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a` | Verified |

---

## 7. Limitations and root-cause reassessment

### What is proven
- Pre-fix reproduces BR261 under multi-process contention (13 events in 2000 opens).
- Post-fix eliminates BR261 under identical conditions (0 events in 2000 opens).
- No integrity failures in either pre-fix or post-fix.
- Alternative F reduces other_fail from 84 to 0 (improved startup reliability under contention).

### What is NOT proven
- **Root cause not confirmed.** The 0 BR261 post-fix is consistent with the hypothesis (ordering defect + missing retry) but does not independently prove causation. The reduction could be partially timing-dependent.
- **No isolated single-variable test.** The pre-fix/post-fix comparison changes three things simultaneously (reorder + retry + extended codes). We cannot attribute the improvement to any single change.
- **No control for timing.** The two runs used fresh DBs; contention patterns may differ.
- **Duration:** 2 runs × ~seconds. Not a soak test.

### Root-cause reassessment
- **Before:** LIKELY/UNCONFIRMED (concurrent open/init WAL contention, busy_timeout after WAL).
- **After:** LIKELY/UNCONFIRMED (still). The multi-process validation provides **stronger evidence** that Alternative F addresses the reproduced failure, but the root cause is not independently confirmed because:
  1. No isolated variable test (three changes applied together).
  2. No pre-fix control under identical timing (different runs).
  3. The improvement could be partially timing-dependent.

**Root cause remains LIKELY, not CONFIRMED.** However, the evidence that Alternative F addresses the reproduced failure under tested conditions is now **strong** (pre-fix reproduces, post-fix eliminates, 2000 opens each, 0 integ failures).

---

## 8. Acceptance criteria assessment

| Criterion | Status | Evidence |
|---|---|---|
| 1. Root cause confirmed (isolated reproduction) | **PARTIAL** | Pre-fix reproduces BR261; post-fix eliminates it; but no isolated single-variable test |
| 2. Minimal fix applied (reorder + retry + extended codes) | **PASS** | Source diff verified |
| 3. 0 BR261 in ≥1000 iterations under contention | **PASS** | 2000 opens, 0 BR261 (2 runs) |
| 4. Integrity 0 in all tests | **PASS** | 0 integ_fail in both pre and post |
| 5. No regressions in unit/CTest/E2E | **PASS** | Unit 137/137, CTest 11/11, E2E PASS |
| 6. ASAN/UBSAN clean | **PASS** | 137/137, 0 findings |
| 7. Independent evidence audit PASS | **PARTIAL** | This audit + prior independent audit; multi-process evidence now available |
| 8. Rollback plan documented and tested | **PASS** (documented, not executed) | Single-file revert |

**Acceptance: 6/8 PASS, 2 PARTIAL (root cause confirmation, independent audit of multi-process harness).**

---

## 9. Recommendation for TB-001 status

**Upgrade from PARTIAL to PARTIAL (stronger evidence).** The multi-process validation provides strong evidence that Alternative F addresses the reproduced BR261 failure under multi-process contention. However, root cause remains LIKELY/UNCONFIRMED because:
- No isolated single-variable test.
- No pre-fix control under identical timing.

**Recommended status: PARTIAL (implemented + multi-process validated, root cause likely).**

TB-001 should NOT be closed. The following remain:
- Independent audit of the multi-process harness methodology.
- Optional: isolated single-variable test to confirm which change (reorder/retry/extended) is most effective.
- Soak test under realistic load (deferred to deployment phase).

**TB-001 remains OPEN/BLOCKED for mainnet.**  
**TESTNET: NO-GO. VPS: FROZEN. No deployment authorization.**  
**No code changes made during this validation. No historical evidence modified.**  
**STOP and await owner decision.**