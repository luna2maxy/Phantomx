# PHASE B2-R6 — Extended Result-Code Fidelity Report

**Date:** 2026-11-26  
**Status:** COMPLETE (controlled test)  
**Scope:** Phase 3B item 4 — validate `sqlite3_extended_errcode()` behavior; compare with extended_result_codes enabled; document production limitations. No production SQLite behavior changes.

## 1. Production implementation observation (read-only)

- `SqliteDb::open` does **not** call `sqlite3_extended_result_codes(db,1)` (`src/database/sqlite_db.cpp:255-343`).
- `capture_diag` reads `sqlite3_errcode(db)` and `sqlite3_extended_errcode(db)` (`src/database/sqlite_db.cpp:37-47`).
- `ChainDatabase` quota path records `SqliteDiag` primary/extended — `src/blockchain/chain.cpp:164-399`.

## 2. Controlled fidelity test

Harness: `/tmp/b2r6b_fidelity2.c` (test-only, /tmp). Method: pre-initialized WAL DB, crash-wal+shm removal to create recovery contention, 4 concurrent processes open+`PRAGMA journal_mode=WAL`; compare mode EXT_OFF (no `sqlite3_extended_result_codes`) vs EXT_ON. 100 iterations/mode x 4 workers = 400 samples/mode.

| Mode | code=0 (ok) | code=5 (BUSY) | code=261 (BUSY_RECOVERY) |
|---|---:|---:|---:|
| EXT_OFF | 336 | 0 | 64 |
| EXT_ON | 339 | 4 | 57 |

## 3. Findings

- `sqlite3_extended_errcode()` returned **261** in both EXT_OFF and EXT_ON modes. This confirms extended_errcode() can expose extended code 261 even when `sqlite3_extended_result_codes()` is not enabled.
- This supports the fidelity of Layer B/C 261 records captured via `capture_diag`/`SqliteDiag.extended`.
- Minor distribution differences (5 vs 261 in EXT_ON) indicate mode/timing can affect returned/captured code detail; not evidence of fabrication.

## 4. Production limitation

- Production `SqliteDb` does not enable `sqlite3_extended_result_codes`. While extended_errcode() observed 261 in control, enabling extended result codes would improve API-level observability consistency. **No production change made.** Any change requires separate owner approval after confirmed root cause.

## 5. Verdict

Extended result-code capture for observed 261 events is **PLAUSIBLE/SUPPORTED** by control test; not invalidated. Limit remains documented.