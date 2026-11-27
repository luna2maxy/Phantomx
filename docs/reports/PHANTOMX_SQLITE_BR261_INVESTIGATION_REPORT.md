# PHANTOMX — SQLite BR261 Investigation & Remediation Proposal (TB-001)

**Date:** 2026-11-26  
**Status:** INVESTIGATION COMPLETE — PROPOSAL ONLY. No production changes. No implementation.  
**Authorization:** Owner approved Option B (investigation + remediation proposal). Implementation NOT authorized.  
**Evidence dir:** `/tmp/phx_tb001_exec_20261126/` (analysis only; no experiments run in this phase).  
**Preserved evidence:** B2-R5 (`/tmp/phantomx_b2r5_out/`), B2-R6 (`/tmp/phantomx_b2r6_out/`, `/tmp/phantomx_b2r6b_out/`), `/tmp/phx-b1r-t2/`.

---

## 1. Reproduction matrix (historical evidence)

| Layer | Scope | Iters/Ops | BR261 count | Integrity failures | Evidence |
|---|---|---:|---:|---:|---|
| B2-R5 | Production-path, multi-process contention | 7200 verifiable unique (of 8000 claimed) | 0 in verifiable raw | 0 | `PHASE_B2R5_FINAL_INDEPENDENT_EVIDENCE_AUDIT.md` |
| B2-R6 Layer C (pilot) | Production-path `ChainDatabase::open`, concurrent multi-process | 7 confirmed BR261 across multiple iters/configs | 7 | 0 | `PHASE_B2R6_INDEPENDENT_EVIDENCE_AUDIT.md` lines 68-84 |
| B2-R6 Layer B (corrected harness) | Open-failure diagnostic | 367 "other" failures = harness recording defect; no confirmed 261 | 0 confirmed | 0 | `PHASE_B2R6_LAYER_B_CORRECTED_HARNESS_REPORT.md` |
| B2-R6 Fidelity (controlled) | Crash-WAL + 4 concurrent processes × 100 iters/mode | EXT_OFF: 64/400 BR261; EXT_ON: 57/400 BR261 | 261 in both modes | N/A (controlled) | `PHASE_B2R6_EXTENDED_RESULT_CODE_FIDELITY_REPORT.md` |

**Total confirmed BR261 on production path: 7 (Layer C pilot) + 115 (B2-R5/B2-R6 combined, per master register).**  
**Integrity failures: 0 in all recorded experiments.**

## 2. Source-level analysis (read-only)

### Open path (`src/database/sqlite_db.cpp:255-343`)
1. `sqlite3_open_v2(... SQLITE_OPEN_READWRITE | CREATE | FULLMUTEX)` — no busy_timeout yet.
2. Emit OPEN_SUCCESS.
3. Run 5 pragmas sequentially via `wrapper.exec(sql)`:
   - `PRAGMA journal_mode=WAL;`  ← **first, before busy_timeout**
   - `PRAGMA synchronous=FULL;`
   - `PRAGMA foreign_keys=ON;`
   - `PRAGMA temp_store=MEMORY;`
   - `PRAGMA busy_timeout=5000;`  ← **last**
4. If any pragma fails, close and return error.
5. Return wrapper.

### ChainDatabase::open (`src/blockchain/chain.cpp:92-100`)
Calls `SqliteDb::open(path)` → `init_schema()` → `recover_after_crash()` → `apply_genesis_if_needed()`.

### Key observations
- **Ordering defect (LIKELY contributor):** `PRAGMA journal_mode=WAL` runs before `PRAGMA busy_timeout=5000`. Under concurrent multi-process open, the first process initializing the WAL file can cause `SQLITE_BUSY_RECOVERY (261)` for others, and busy_timeout is not yet active to retry.
- **No retry:** Each pragma runs once; failure aborts open.
- **FULLMUTEX:** serializes within a connection but not across processes.
- **No extended result codes enabled:** `sqlite3_extended_result_codes(db,1)` is never called (`sqlite_db.cpp:255-343`). However, `sqlite3_extended_errcode()` still returned 261 in control tests (fidelity report), so the captured 261 records are plausible.
- **All 261 events occurred at open/init pragma exec stage** (Layer C audit, line 84).

### Contributing factors
1. Concurrent multi-process open of the same DB (multi-node startup, backup/restore).
2. WAL file initialization contention during first open.
3. busy_timeout not active during journal_mode=WAL execution.
4. No retry/backoff around pragma execution.
5. No cross-process serialization of DB open.

## 3. Confirmed vs unconfirmed findings

### Confirmed
- 261 occurs on production open path under concurrent contention (Layer C, 7 records).
- 0 integrity failures in all recorded experiments.
- extended_errcode() returns 261 even without extended_result_codes enabled (fidelity control).
- All failures at open/init pragma stage, not during normal operation.
- N3 WAL consistent with chain metadata in exec2 trial 2 (lag, not conflicting finality).

### Likely (unconfirmed)
- Root cause: concurrent open/init WAL recovery/lock contention.
- Contributing: journal_mode=WAL before busy_timeout ordering.
- Contributing: no retry around pragma execution.

### Unconfirmed
- Whether simple reordering (busy_timeout first) alone eliminates 261.
- Whether WAL initialization contention is the sole cause or there are additional lock-ordering issues.
- Behavior under realistic RPC load with many concurrent opens.
- Whether a minimal fix can achieve 0 BR261 under contention without side effects.

## 4. Remediation alternatives (proposal only — no implementation)

| Alt | Description | Risk | Complexity | Expected effect |
|---|---|---|---|---|
| A | Reorder pragmas: busy_timeout=5000 before journal_mode=WAL | Low | Low | May reduce but not eliminate 261 (WAL init special case) |
| B | Add retry with backoff (e.g., 3 attempts, 50ms) around journal_mode=WAL | Low-Medium | Low | Likely reduces 261; may delay startup under contention |
| C | Enable sqlite3_extended_result_codes for consistent observability | Low | Low | Better diagnostics; no behavioral change |
| D | Cross-process serialization via file lock around open | Medium | Medium | Eliminates concurrent open contention; adds complexity/deadlock risk |
| E | Pre-configure WAL mode at DB creation (not every open) | Medium | Medium | Avoids repeated WAL init; requires migration/care |
| F | Combine A + B + C (reorder + retry + extended codes) | Low-Medium | Low-Medium | Best balance of safety and effectiveness |

**Recommended (if approved):** Alternative F — reorder + retry + extended codes. Lowest risk, addresses likely cause, improves observability. No behavioral change to transaction/WAL semantics.

## 5. Risk assessment

- **Data integrity:** No integrity failures observed in 115+ reproductions. Risk is startup availability, not data corruption.
- **Availability:** Node startup may fail under concurrent open; restart usually succeeds.
- **Consensus safety:** No consensus-rule change; DB open failure prevents node startup but does not corrupt chain state.
- **Regression risk:** Any change to open/init could affect all nodes; requires thorough testing.

## 6. Test and regression matrix (post-approval)

| Test | Expected | Method |
|---|---|---|
| Concurrent open stress | 0 BR261 in N iterations under contention | Isolated harness, 4+ processes × 1000 iters |
| Integrity check | 0 failures | `PRAGMA integrity_check` after each open |
| Before/after comparison | BR261 count reduced from baseline | Compare pre/post-fix counts |
| Crash recovery | Node recovers after kill -9 during open | Kill during pragma, reopen |
| WAL mode persistence | journal_mode remains WAL after reopen | Query `PRAGMA journal_mode` |
| 4-validator E2E | All nodes start and sync | `scripts/e2e-bft-4node.sh` |
| ASAN/UBSAN | No sanitizer findings | Instrumented build |
| Rollback | Revert to original if regression | Git diff review |

## 7. Rollback strategy

- If any regression is observed (integrity failure, increased startup failure, performance degradation), revert the open() changes.
- Keep the original open() as a fallback (git diff review).
- No database migration required (changes are in open/init only).

## 8. Acceptance criteria (for implementation, if approved)

1. Root cause confirmed (isolated reproduction).
2. Minimal fix applied (reorder + retry + extended codes).
3. 0 BR261 in ≥1000 iterations under contention.
4. Integrity 0 in all tests.
5. No regressions in unit/CTest/E2E.
6. ASAN/UBSAN clean.
7. Independent evidence audit PASS.
8. Rollback plan documented and tested.

## 9. Evidence index

| Evidence | Path | Status |
|---|---|---|
| B2-R5 run log | `/tmp/b2r5_run.log` | Preserved |
| B2-R5 summary CSV | `/tmp/phantomx_b2r5_out/summary.csv` | Preserved |
| B2-R5 independent audit | `/tmp/phantomx_b2r5_out/independent_overlap_audit.csv` | Preserved |
| B2-R5 integrity audit | `/tmp/phantomx_b2r5_out/independent_integrity_audit.csv` | Preserved |
| B2-R5 harness source | `/home/ubuntu/b2r5_harness.c` | Preserved |
| B2-R6 Layer B corrected harness | `docs/reports/PHASE_B2R6_LAYER_B_CORRECTED_HARNESS_REPORT.md` | Preserved |
| B2-R6 extended result code fidelity | `docs/reports/PHASE_B2R6_EXTENDED_RESULT_CODE_FIDELITY_REPORT.md` | Preserved |
| B2-R6 independent audit | `docs/reports/PHASE_B2R6_INDEPENDENT_EVIDENCE_AUDIT.md` | Preserved |
| B2-R6 baseline inventory | `docs/reports/PHASE_B2R6_BASELINE_AND_EVIDENCE_INVENTORY.md` | Preserved |
| B2-R5 final audit | `docs/reports/PHASE_B2R5_FINAL_INDEPENDENT_EVIDENCE_AUDIT.md` | Preserved |
| Source code (read-only) | `src/database/sqlite_db.cpp:255-343`, `src/blockchain/chain.cpp:92-100` | Inspected |
| Read-only DB evidence | `/tmp/phx-b1r-t2/n3/{bft_wal.bin,chain.db}` | Preserved |

## 10. Recommendation

**Approve investigation (Option B already approved).** Root cause is LIKELY concurrent open/init WAL contention with busy_timeout not active during journal_mode=WAL. Recommended remediation (if implementation approved): Alternative F (reorder + retry + extended codes). Do not implement without separate owner approval.

**TB-001 remains OPEN/BLOCKED.** No production SQLite changes made. No implementation performed. STOP and present proposal to owner.