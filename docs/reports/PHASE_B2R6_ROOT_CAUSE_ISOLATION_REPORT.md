# PHASE B2-R6 — Root-Cause Isolation Report

**Date:** 2026-11-26  
**Status:** PARTIAL (evidence supports likely cause; not fully confirmed)  
**Scope:** Phase 3B item 3 — isolate concurrent open, PRAGMA, WAL init, WAL recovery after termination, concurrent readers/writers, checkpoint interaction. No assumption that open error proves WAL recovery.

## 1. Observed failure signature (reproduced)

- Error: `io_error: sqlite exec: database is locked [primary=5 extended=261 rc=5 api=sqlite3_exec]`
- Location: `ChainDatabase::open -> SqliteDb::open -> PRAGMA journal_mode=WAL exec` (`src/database/sqlite_db.cpp:278-304`, `src/blockchain/chain.cpp:89-105`).
- Reproduced in: Layer C pilot+esc (104 BR261/38995 ops), Layer B fixed (11 BR261/14080 ops), integ_fail 0.

## 2. Isolation tests

| Test | Setup | Result | Interpretation |
|---|---|---|---|
| Concurrent open on pre-initialized WAL DB (no crash) | 500 iters x 4 procs, open + `PRAGMA journal_mode=WAL` only | ok=1882, busy5=70, br261=48, other=0 (`/tmp/b2r6b_iso.log`) | **261 reproducible without crash/WAL recovery**. Points to concurrent open/PRAGMA/init lock contention. |
| Raw crash-WAL + noshm contention (B2-R4/R6A) | crash-wal, concurrent openers | 0 BR261 in raw control A/B2-R4; fidelity control saw 261 in crash-wal scenario | 261 can appear in recovery contexts too; not exclusive. |
| Layer C pre-init DB | DB pre-initialized by ChainDatabase open/close before worker contention | BR261 still occurs on worker opens | Not schema creation-only; occurs on subsequent concurrent opens. |

## 3. Mechanism assessment

**Likely contributing mechanism:** concurrent processes opening the same WAL DB and executing initialization PRAGMA (`journal_mode=WAL`) contend on SQLite WAL locks; SQLite returns BUSY/BUSY_RECOVERY in some contention/recovery states. The observed 261 during normal concurrent opens (no crash) suggests **open/init contention** is sufficient; WAL crash recovery is not required to explain all observations.

**Not confirmed:** exact SQLite internal lock state, whether SHM/WAL recovery checkpoint metadata was involved in specific 261 events, and whether production `busy_timeout=5000` behavior differs under realistic RPC load. No code-path fix validated.

## 4. Alternative causes considered

| Cause | Status |
|---|---|
| WAL crash recovery required | Ruled out as necessary (iso test reproduced 261 without crash). Not ruled out as contributing in crash scenarios. |
| Concurrent PRAGMA journal_mode on already-WAL DB | **Likely contributor** (observed in iso). |
| Checkpoint contention | UNVERIFIED (not isolated). |
| Quota tx contention | Not the observed failing op (failures were open/init). |
| Extended code capture artifact | Mitigated by fidelity report. |

## 5. Verdict

**Root cause: LIKELY (UNCONFIRMED).** Strong evidence for concurrent open/init PRAGMA contention producing SQLITE_BUSY_RECOVERY 261 in production `SqliteDb/ChainDatabase` open path. Causal mechanism not fully confirmed to level required for production remediation. No speculative fix.