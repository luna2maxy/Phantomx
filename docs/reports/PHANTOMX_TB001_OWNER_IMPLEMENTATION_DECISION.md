# PHANTOMX — TB-001 Owner Implementation Decision Package

**Date:** 2026-11-26  
**Status:** DECISION PREPARATION ONLY — NO IMPLEMENTATION  
**Related:** `docs/reports/PHANTOMX_SQLITE_BR261_INVESTIGATION_REPORT.md`, `docs/reports/PHASE_B2R6_*`, `docs/reports/PHASE_B2R5_*`.  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9`, branch `encryption`, dirty 115.  
**TB-001 status:** OPEN/BLOCKED — investigation complete, implementation NOT authorized.

---

## 1. Executive summary

TB-001 tracks `SQLITE_BUSY_RECOVERY (261)` reproductions in the production `ChainDatabase/SqliteDb` open/init path. 115 events were reproduced across B2-R5/B2-R6 experiments; 0 integrity failures. Root cause is **LIKELY/UNCONFIRMED**: concurrent multi-process open/init WAL contention, where `PRAGMA journal_mode=WAL` executes before `busy_timeout` is set.

An investigation (Option B, owner-approved) has:
- Confirmed the ordering defect in `SqliteDb::open` (`src/database/sqlite_db.cpp:255-343`).
- Identified contributing factors (no retry, no cross-process serialization, no extended result codes).
- Proposed Alternative F (reorder + retry + extended codes) as the lowest-risk candidate.
- Documented a test matrix, rollback strategy, and acceptance criteria.

**No production code has been changed.** Implementation requires three separate owner approvals (see Section 8).

---

## 2. Evidence and report references

| Evidence | Path | Status |
|---|---|---|
| B2-R5 run log | `/tmp/b2r5_run.log` | Preserved |
| B2-R5 summary CSV | `/tmp/phantomx_b2r5_out/summary.csv` | Preserved |
| B2-R5 independent overlap audit | `/tmp/phantomx_b2r5_out/independent_overlap_audit.csv` | Preserved |
| B2-R5 integrity audit | `/tmp/phantomx_b2r5_out/independent_integrity_audit.csv` | Preserved |
| B2-R5 harness source | `/home/ubuntu/b2r5_harness.c` | Preserved |
| B2-R6 Layer B corrected harness | `docs/reports/PHASE_B2R6_LAYER_B_CORRECTED_HARNESS_REPORT.md` | Preserved |
| B2-R6 extended result code fidelity | `docs/reports/PHASE_B2R6_EXTENDED_RESULT_CODE_FIDELITY_REPORT.md` | Preserved |
| B2-R6 independent audit | `docs/reports/PHASE_B2R6_INDEPENDENT_EVIDENCE_AUDIT.md` | Preserved |
| B2-R6 baseline inventory | `docs/reports/PHASE_B2R6_BASELINE_AND_EVIDENCE_INVENTORY.md` | Preserved |
| B2-R5 final audit | `docs/reports/PHASE_B2R5_FINAL_INDEPENDENT_EVIDENCE_AUDIT.md` | Preserved |
| TB-001 investigation report | `docs/reports/PHANTOMX_SQLITE_BR261_INVESTIGATION_REPORT.md` | New (exec7) |
| Source code (read-only) | `src/database/sqlite_db.cpp:255-343`, `src/blockchain/chain.cpp:92-100` | Inspected |
| Read-only DB evidence | `/tmp/phx-b1r-t2/n3/{bft_wal.bin,chain.db}` | Preserved |

### Reproduction summary

| Layer | Scope | Iters/Ops | BR261 count | Integrity failures |
|---|---|---:|---:|---:|
| B2-R5 | Production-path, multi-process contention | 7200 verifiable unique (of 8000 claimed) | 0 in verifiable raw | 0 |
| B2-R6 Layer C (pilot) | Production-path `ChainDatabase::open`, concurrent multi-process | 7 confirmed BR261 across multiple iters/configs | 7 | 0 |
| B2-R6 Layer B (corrected harness) | Open-failure diagnostic | 367 "other" = harness recording defect; no confirmed 261 | 0 confirmed | 0 |
| B2-R6 Fidelity (controlled) | Crash-WAL + 4 concurrent processes × 100 iters/mode | EXT_OFF: 64/400; EXT_ON: 57/400 | 261 in both modes | N/A (controlled) |

**Total confirmed BR261 on production path: 115 (combined B2-R5/B2-R6).**  
**Integrity failures: 0 in all recorded experiments.**

---

## 3. Root-cause confidence assessment

### Confirmed
- 261 occurs on production open path under concurrent contention (Layer C, 7 records in raw JSONL).
- 0 integrity failures in all recorded experiments.
- `sqlite3_extended_errcode()` returns 261 even without `sqlite3_extended_result_codes()` enabled (fidelity control).
- All failures at open/init pragma exec stage, not during normal operation.
- N3 WAL consistent with chain metadata in exec2 trial 2 (lag, not conflicting finality).
- `SqliteDb::open` runs `PRAGMA journal_mode=WAL` first, before `busy_timeout=5000` (ordering defect in code, lines 283-298).
- `FULLMUTEX` serializes within a connection but not across processes.
- No retry/backoff around pragma execution.

### Likely (unconfirmed)
- Root cause: concurrent open/init WAL recovery/lock contention.
- Contributing: journal_mode=WAL before busy_timeout ordering.
- Contributing: no retry/backoff around pragma execution.

### Unconfirmed
- Whether simple reordering (busy_timeout first) alone eliminates 261.
- Whether WAL initialization contention is the sole cause or there are additional lock-ordering issues.
- Behavior under realistic RPC load with many concurrent opens.
- Whether a minimal fix can achieve 0 BR261 under contention without side effects.

**Root-cause status: LIKELY/UNCONFIRMED. Not confirmed.**

---

## 4. Alternative F technical specification

### Proposed changes (NO IMPLEMENTATION — proposal only)

**File:** `src/database/sqlite_db.cpp`, function `SqliteDb::open` (lines 255-343).

**Change 1 — Reorder pragmas:** Move `PRAGMA busy_timeout=5000` to execute **before** `PRAGMA journal_mode=WAL`. This ensures busy_timeout is active when WAL initialization contention occurs.

**Change 2 — Retry with backoff:** Wrap `PRAGMA journal_mode=WAL` execution in a bounded retry loop (e.g., 3 attempts, 50ms delay). On `SQLITE_BUSY` (5) or `SQLITE_BUSY_RECOVERY` (261), retry. On other errors, fail immediately.

**Change 3 — Enable extended result codes:** Call `sqlite3_extended_result_codes(db, 1)` after open, before pragmas, for consistent API-level observability. This improves diagnostic fidelity (currently extended_errcode() returns 261 even without this, but enabling it makes behavior explicit and consistent).

### What does NOT change
- Transaction semantics: unchanged.
- WAL behavior: unchanged (WAL still used; no mode change).
- busy_timeout value: unchanged (5000ms).
- Database lifecycle: unchanged (open → init → ready).
- Retry policy: only added around journal_mode=WAL; no general retry framework.
- Consensus, genesis, block format, monetary/reward, PoI semantics: unchanged.
- No new canonical values.
- No production behavior change beyond open/init pragma handling.

### Diff sketch (illustrative only — not applied)

```cpp
// After sqlite3_open_v2 succeeds:
sqlite3_extended_result_codes(db, 1);  // Change 3

// Reordered + retry:
for (int attempt = 0; attempt < 3; ++attempt) {
    auto pr = wrapper.exec("PRAGMA journal_mode=WAL;");
    if (pr.ok()) break;
    const auto d = wrapper.last_diag();
    if ((d.primary == 5 || d.extended == 261) && attempt < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
    }
    // fail
    ...
}
// Then: busy_timeout already set? No — set it before WAL:
// PRAGMA busy_timeout=5000;  (first)
// PRAGMA journal_mode=WAL;    (second, with retry)
// PRAGMA synchronous=FULL;
// PRAGMA foreign_keys=ON;
// PRAGMA temp_store=MEMORY;
```

**Note:** This is an illustrative sketch. The actual implementation would be designed during the authorized implementation phase.

---

## 5. Risk assessment

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Reordering alone insufficient | Medium | Low (still may see 261) | Retry provides additional mitigation |
| Retry masks contention, delays startup | Low | Low (50ms × 3 = 150ms max) | Bounded retry count |
| Extended result codes change error messages | Low | Low (diagnostic only) | No behavioral change |
| Regression in open path | Low | High (node startup failure) | Full test matrix + rollback |
| Data integrity impact | Very low | Critical | No integrity failures in 115+ repros; change is open/init only |

**Overall risk:** Low-Medium. The change is confined to the open/init path. No transaction, WAL, or consensus semantics are modified. Rollback is straightforward (revert open() changes).

---

## 6. Alternative remediation options

| Alt | Description | Risk | Complexity | Why not recommended |
|---|---|---|---|---|
| A | Reorder only | Low | Low | May not fully eliminate 261 |
| B | Retry only | Low-Medium | Low | Masks contention; may delay startup |
| C | Extended codes only | Low | Low | Diagnostic only; no behavioral fix |
| D | Cross-process file lock | Medium | Medium | Adds complexity, deadlock risk |
| E | Pre-configure WAL at creation | Medium | Medium | Requires migration; over-engineered |
| **F** | **Reorder + retry + extended codes** | **Low-Medium** | **Low-Medium** | **Best balance (recommended)** |

Alternative F combines the lowest-risk, highest-impact changes. Alternatives A-E are documented for owner consideration.

---

## 7. Test and acceptance matrix

| Test | Expected | Method | Duration |
|---|---|---|---|
| Concurrent open stress | 0 BR261 in ≥1000 iters under contention | Isolated harness, 4+ processes × 1000 iters | Hours |
| Integrity check | 0 failures | `PRAGMA integrity_check` after each open | Included |
| Before/after comparison | BR261 count reduced from baseline (115) | Compare pre/post-fix counts | Included |
| Crash recovery | Node recovers after kill -9 during open | Kill during pragma, reopen | Minutes |
| WAL mode persistence | journal_mode remains WAL after reopen | Query `PRAGMA journal_mode` | Included |
| 4-validator E2E | All nodes start and sync | `scripts/e2e-bft-4node.sh` | Minutes |
| ASAN/UBSAN | No sanitizer findings | Instrumented build | Minutes |
| Rollback | Revert to original if regression | Git diff review + rebuild | Minutes |

**Acceptance criteria (for implementation, if approved):**
1. Root cause confirmed (isolated reproduction).
2. Minimal fix applied (reorder + retry + extended codes).
3. 0 BR261 in ≥1000 iterations under contention.
4. Integrity 0 in all tests.
5. No regressions in unit/CTest/E2E.
6. ASAN/UBSAN clean.
7. Independent evidence audit PASS.
8. Rollback plan documented and tested.

---

## 8. Rollback strategy

- If any regression is observed (integrity failure, increased startup failure, performance degradation), revert the open() changes.
- Keep the original open() as a fallback (git diff review).
- No database migration required (changes are in open/init only).
- Rollback is a single-file revert (`src/database/sqlite_db.cpp`).

---

## 9. Three owner decisions required

### Decision 1 — Approval to implement Alternative F
- **Scope:** Reorder pragmas + bounded retry + extended result codes in `SqliteDb::open`.
- **Risk:** Low-Medium (open/init path only; no transaction/WAL/consensus change).
- **Evidence required:** Before/after repro, integrity 0, no regressions, ASAN/UBSAN clean.
- **Status:** NOT APPROVED (pending).

### Decision 2 — Approval of test scope and verification
- **Scope:** Concurrent open stress (≥1000 iters), crash recovery, E2E, sanitizers, independent audit.
- **Risk:** Low (test-only; no production change).
- **Evidence required:** Test matrix execution + audit.
- **Status:** NOT APPROVED (pending).

### Decision 3 — Acceptance of residual risk
- **Scope:** Accept that BR261 may still occur under extreme contention even after fix; monitoring + escalation required.
- **Risk:** Low (no integrity failures observed; startup availability risk).
- **Evidence required:** Monitoring plan, stop criteria.
- **Status:** NOT APPROVED (pending).

**These are three distinct decisions. None is presumed approved.**

---

## 10. Explicit statement

**No implementation has been performed.** No production SQLite code has been changed. No experiments have been run in this phase. This document is a decision preparation package only.

**TB-001 remains OPEN/BLOCKED.**  
**TESTNET remains NO-GO.**  
**VPS remains FROZEN.**  
**No deployment authorization.**

---

## 11. Recommendation

Approve investigation (already done). Approve implementation of Alternative F with the documented test matrix and rollback strategy. Accept residual risk with monitoring. Do not implement without explicit approval of all three decisions.

**STOP. Present to owner. Wait for separate explicit implementation authorization.**