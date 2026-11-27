# PHANTOMX — TB-001 Post-Implementation Independent Audit

**Date:** 2026-11-26  
**Mode:** READ-ONLY INDEPENDENT AUDIT — no code changes, no experiments rerun (one stress re-run for evidence persistence, see §B).  
**Auditor:** Agent (independent review of implementation phase).  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9`, branch `encryption`, dirty 115.  
**Binary audited:** `build-ubuntu/bin/phantomxd` SHA256 `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`.

---

## 1. Executive verdict

**PARTIAL — NOT CLOSED.**

Alternative F has been implemented and passes all available regression tests and a 2000-open stress test with 0 BR261 and 0 integrity failures. However, the root cause remains **LIKELY/UNCONFIRMED** (not confirmed by isolated reproduction comparing pre-fix vs post-fix). The stress test demonstrates improvement but does not independently prove the root cause was the ordering defect. Independent evidence audit of the stress harness methodology is limited (single machine, same process family, no cross-process contention isolation).

**TB-001 status: PARTIAL (implemented, root cause unconfirmed).**  
**TESTNET: NO-GO. VPS: FROZEN.**

---

## 2. Scope and baseline

- **Files changed (implementation phase):** `src/database/sqlite_db.cpp` only (Alternative F in `SqliteDb::open`). No other source files modified. No config changes. No test changes in this phase.
- **Pre-existing dirty changes:** The working tree had prior exec-phase modifications to `sqlite_db.cpp` (observability, connection events, diagnostics). The Alternative F change is confined to the `SqliteDb::open()` pragma handling.
- **HEAD unchanged:** `d094596549a53eb0695527df06729eca41e737e9`.
- **No destructive git operations.**

---

## 3. Source diff findings (Audit A)

### A.1 File scope
- Only `src/database/sqlite_db.cpp` was modified for Alternative F. ✓ (No other source files changed in this phase.)

### A.2 Extended result codes
- `sqlite3_extended_result_codes(db, 1)` is called at line 279, after `sqlite3_open_v2` succeeds and before pragma execution. ✓ (Confirmed present.)

### A.3 busy_timeout before WAL
- `PRAGMA busy_timeout=5000` is executed first (line 312), before `PRAGMA journal_mode=WAL` (line 321). ✓ (Confirmed.)

### A.4 Bounded retry
- Retry loop: `for (int attempt = 0; attempt < 3; ++attempt)` (line 316). ✓ (Max 3 attempts.)

### A.5 Backoff
- `std::this_thread::sleep_for(std::chrono::milliseconds(50))` on retry (line 329). ✓ (50ms backoff.)

### A.6 Selective retry
- Retry condition: `(d.primary == 5 || d.extended == 261) && attempt < 2` (line 327). ✓ (Only BUSY (5) or BUSY_RECOVERY (261) retried; permanent errors fail immediately.)

### A.7 No permanent error retry
- Non-BUSY errors break out of loop and return error. ✓ (Confirmed.)

### A.8 Transaction/WAL semantics
- No change to transaction semantics, WAL mode (still WAL), synchronous=FULL, foreign_keys, temp_store. ✓ (Only ordering + retry + extended codes changed.)

### A.9 Error reporting
- Final error and extended code reported via `make_db_error` and `emit_conn_event_global`. ✓ (Confirmed.)

### A.10 Fail-closed
- On WAL retry exhaustion, returns `MakeError(ErrorCode::kIoError, "sqlite open failed: journal_mode=WAL retry exhausted")`. ✓ (No silent success.)

**Source diff audit: PASS.** All 9 checks pass.

---

## 4. Stress evidence audit (Audit B)

### Raw evidence
- **Command:** `/tmp/phx_tb001_impl_20261126/stress_open /tmp/phx_tb001_impl_20261126/stress.db 4 500`
- **Output (saved):** `/tmp/phx_tb001_impl_20261126/stress_result.log`
- **Result:** `workers=4 iters=500 ok=2000 br261=0 other_fail=0 integ_fail=0`
- **Exit code:** 0

### Audit findings
| Check | Expected | Observed | Status |
|---|---|---|---|
| Worker count | 4 | 4 (threads) | PASS |
| Iterations per worker | 500 | 500 | PASS |
| Total opens | 2000 | 2000 (ok=2000) | PASS |
| BR261 count | 0 | 0 | PASS |
| Other failures | 0 | 0 | PASS |
| Integrity failures | 0 | 0 | PASS |
| Exit code | 0 | 0 | PASS |

### Limitations
- **Single machine, same process family:** All workers are threads in one process sharing the same DB path. This is multi-threaded concurrency but not multi-process contention. The original 261 repros occurred under multi-process contention (B2-R6 Layer C).
- **No cross-process contention test:** The stress harness uses `ChainDatabase::open` in threads, not separate processes. Cross-process WAL initialization contention is the suspected root cause.
- **No pre-fix comparison in this harness:** The stress test only ran on the fixed code. The 0 BR261 result is consistent with the fix but does not independently prove the ordering defect was the cause (could be timing-dependent).
- **DB path:** `/tmp/phx_tb001_impl_20261126/stress.db` (freshly created, no pre-existing WAL contention).
- **Duration:** ~seconds (not a soak).

**Stress evidence audit: PARTIAL.** The stress test demonstrates 0 BR261 under 2000 concurrent opens, but lacks multi-process contention and pre-fix comparison. The 0 BR261 result is consistent with the fix but not conclusive proof of root cause.

---

## 5. Regression evidence audit (Audit C)

| Test | Expected | Observed | Evidence | Status |
|---|---|---|---|---|
| Build (build-ubuntu) | EXIT=0 | EXIT=0 | build_v4.log (exec6) + this phase | PASS |
| Unit tests | 137/137 | 137/137 | `/tmp/phx_tb001_impl_20261126/unit.log` | PASS |
| CTest | 11/11 | 11/11 | `/tmp/phx_tb001_impl_20261126/ctest.log` | PASS |
| Network tests | 4/4 | 4/4 | ctest output | PASS |
| 4-validator E2E | PASS | PASS | e2e output | PASS |
| ASAN unit | 137/137, 0 findings | 137/137, 0 findings | `/tmp/phx_tb001_impl_20261126/asan_unit.log` | PASS |
| ASAN CTest | 11/11 | 11/11 | `/tmp/phx_tb001_impl_20261126/asan_ctest.log` | PASS |

**Regression evidence audit: PASS.** All 6 checks pass. No sanitizer findings.

---

## 6. Binary identity verification (Audit D)

| Item | Expected | Observed | Status |
|---|---|---|---|
| SHA256 | `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a` | `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a` | MATCH |
| HEAD | `d094596549a53eb0695527df06729eca41e737e9` | `d094596549a53eb0695527df06729eca41e737e9` | MATCH |
| Branch | `encryption` | `encryption` | MATCH |

**Binary identity: VERIFIED.**

---

## 7. Root-cause reassessment (Audit E)

### Before (pre-fix, historical evidence)
- B2-R6 Layer C: 7 BR261 confirmed on production path under multi-process contention.
- Root cause: LIKELY concurrent open/init WAL contention.
- Contributing: `busy_timeout` not active during `journal_mode=WAL`; no retry.

### After (post-fix)
- Stress test: 0 BR261 in 2000 concurrent opens (single-process, multi-thread).
- Unit/CTest/E2E/ASAN: all PASS.

### Assessment
- The 0 BR261 result is **consistent** with the hypothesis that reordering + retry + extended codes mitigates the issue.
- However, **root cause is NOT confirmed** because:
  1. No isolated reproduction comparing pre-fix vs post-fix under identical conditions.
  2. Stress test is single-process multi-thread, not multi-process (original repro was multi-process).
  3. No control experiment showing the fix specifically eliminates 261 (could be timing-dependent).
  4. The fidelity control showed 261 occurs in both EXT_OFF and EXT_ON modes, confirming the code is plausible but not proving causation.

**Root-cause status: LIKELY (unchanged). Not CONFIRMED.** The fix is a reasonable mitigation but the root cause is not independently confirmed.

---

## 8. Rollback readiness (Audit F)

- **Rollback method:** Revert changes to `src/database/sqlite_db.cpp` (single file). `git diff` confirms only this file's open() method was changed for Alternative F (the rest of the diff is pre-existing dirty changes from prior execs).
- **No database migration required:** Changes are in open/init only.
- **No config changes:** No configuration files modified.
- **Rollback risk:** Low. Reverting restores the original open() with original pragma order and no retry.
- **Rollback tested:** Not explicitly tested, but the change is confined and reversible.

**Rollback readiness: READY (not executed).**

---

## 9. Confirmed gaps and limitations

1. **Root cause unconfirmed:** No isolated reproduction comparing pre-fix vs post-fix. The stress test lacks multi-process contention.
2. **Stress test scope:** Single-process multi-thread, not multi-process. Original 261 repros were multi-process.
3. **No pre-fix comparison:** The stress test only ran on fixed code. No control experiment.
4. **Extended result codes:** Enabled, but the fidelity control showed 261 occurs even without it. The change improves observability consistency but does not independently prove causation.
5. **Acceptance criteria partially met:** 0 BR261 in 2000 opens (criterion: ≥1000), integrity 0, no regressions, ASAN clean. But root cause not confirmed (criterion 1 not met). Independent evidence audit of stress harness not performed (criterion 7 not fully met).
6. **No soak test:** The stress test ran for seconds, not hours/days.

---

## 10. Evidence index

| Evidence | Path | Status |
|---|---|---|
| Implementation source | `src/database/sqlite_db.cpp` (open() method) | Modified (Alternative F) |
| Stress test binary | `/tmp/phx_tb001_impl_20261126/stress_open` | Built and run |
| Stress test source | `/tmp/phx_tb001_impl_20261126/stress_open.cpp` | Preserved |
| Stress result | `/tmp/phx_tb001_impl_20261126/stress_result.log` | `workers=4 iters=500 ok=2000 br261=0 other_fail=0 integ_fail=0` |
| Unit test log | `/tmp/phx_tb001_impl_20261126/unit.log` | 137/137 PASS |
| CTest log | `/tmp/phx_tb001_impl_20261126/ctest.log` | 11/11 PASS |
| ASAN unit log | `/tmp/phx_tb001_impl_20261126/asan_unit.log` | 137/137, 0 findings |
| ASAN CTest log | `/tmp/phx_tb001_impl_20261126/asan_ctest.log` | 11/11 PASS |
| E2E smoke | pass (manual) | PASS |
| Binary SHA256 | `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a` | Verified |
| B2-R5/B2-R6 evidence | `/tmp/phantomx_b2r5_out/`, `/tmp/phantomx_b2r6_out/`, `/tmp/phantomx_b2r6b_out/` | Preserved |
| Read-only DB evidence | `/tmp/phx-b1r-t2/n3/{bft_wal.bin,chain.db}` | Preserved |

---

## 11. Final TB-001 recommendation

**PARTIAL — NOT CLOSED.**

Alternative F has been implemented and passes all available regression tests and a 2000-open stress test (0 BR261, 0 integrity failures, 0 sanitizer findings). The change is confined to `SqliteDb::open()` pragma handling and is rollback-ready.

However:
- Root cause remains LIKELY/UNCONFIRMED (no isolated reproduction comparing pre-fix vs post-fix).
- Stress test lacks multi-process contention (original repro was multi-process).
- Acceptance criterion 1 (confirmed root cause) is NOT met.
- Acceptance criterion 7 (independent evidence audit) is PARTIALLY met (this audit).

**Recommendation:** Do not close TB-001. Present this audit to the owner. If the owner accepts the stress test as sufficient evidence (despite the multi-process gap), TB-001 could be upgraded to "implemented with bounded risk." Otherwise, a multi-process contention test with pre/post comparison is needed.

**TB-001 status: PARTIAL (implemented, root cause unconfirmed).**  
**TESTNET: NO-GO. VPS: FROZEN. No deployment authorization.**

No code changes made during this audit. No experiments run (one stress re-run for evidence persistence). Baseline preserved. STOP and await owner decision.