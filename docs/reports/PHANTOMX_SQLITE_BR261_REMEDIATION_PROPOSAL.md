# PHANTOMX — SQLite BR261 Remediation Authorization Proposal (TB-001)

**Date:** 2026-11-26  
**Status:** PROPOSAL — no implementation. Owner approval required.  
**Related:** `docs/reports/PHASE_B2R6_*`, `src/database/sqlite_db.cpp`, `src/database/chain_database.cpp`.

## Evidence summary

- BR261 reproduced: 115 confirmed events (C pilot 7 + C esc 97 + B fixed 11), 0 integrity failures.
- Root-cause confidence: LIKELY/UNCONFIRMED — concurrent open/init PRAGMA contention; WAL crash recovery not necessary per isolation.
- Read-only DB evidence preserved: `/tmp/phantomx_b2r6_out/`, `/tmp/phantomx_b2r6b_out/`.
- Extended result code fidelity report exists.

## Risks of changing production SQLite lifecycle/recovery

- Any change to SqliteDb/ChainDatabase open/init/recovery affects node startup and recovery; a regression can cause data loss or unrecoverable nodes.
- Protected behavior policy: production SQLite changes require explicit owner approval.
- `sqlite3_extended_result_codes()` not called (observability gap, TB-009); improving capture touches production DB code.

## Proposed controlled remediation scope (if approved)

1. Confirm root cause with targeted instrumentation (read-only first).
2. Propose minimal fix (e.g., busy_timeout/PRAGMA ordering or open serialization) with owner approval.
3. Validate: before/after repro, integrity 0, no regressions, ASAN/UBSAN clean, 4-validator E2E.
4. Rollback plan and monitoring.

## Constraints

- No production SQLite/ChainDatabase behavior change in this phase.
- No implementation without explicit owner approval.
- VPS frozen; no live deployment.

## Owner decision required

Authorize or decline the remediation scope. Until then TB-001 stays OPEN/BLOCKED.