# PHASE B2-R6 — Evidence Index (Non-Consensus, Read-Only Index)

**Date:** 2026-11-26  
**Scope:** Documentation/index only. No production source/config/SQLite behavior changes. No new experiments.  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9`, branch `encryption`, worktree preserved.

## Reports

| Report | Purpose | Status |
|---|---|---|
| `docs/reports/PHASE_B2R6_BASELINE_AND_EVIDENCE_INVENTORY.md` | Phase 1 baseline + B2-R5 evidence preservation | COMPLETE |
| `docs/reports/PHASE_B2R6_PRODUCTION_SQLITE_PATH_AUDIT.md` | Phase 2 production SqliteDb/ChainDatabase/RPC path audit | COMPLETE |
| `docs/reports/PHASE_B2R6_REPRODUCTION_REPORT.md` | Phase 3 pilot reproduction (A/B/C) | PARTIAL (B harness defect noted) |
| `docs/reports/PHASE_B2R6_INDEPENDENT_EVIDENCE_AUDIT.md` | Phase 3 independent audit | PARTIAL |
| `docs/reports/PHASE_B2R6_LAYER_B_CORRECTED_HARNESS_REPORT.md` | Phase 3B corrected Layer B rerun | COMPLETE |
| `docs/reports/PHASE_B2R6_LAYER_C_ESCALATION_REPORT.md` | Phase 3B Layer C escalation | COMPLETE |
| `docs/reports/PHASE_B2R6_EXTENDED_RESULT_CODE_FIDELITY_REPORT.md` | sqlite3_extended_errcode fidelity control | COMPLETE |
| `docs/reports/PHASE_B2R6_ROOT_CAUSE_ISOLATION_REPORT.md` | Root-cause isolation | PARTIAL/LIKELY |
| `docs/reports/PHASE_B2R6_PHASE3B_INDEPENDENT_EVIDENCE_AUDIT.md` | Phase 3B independent audit | COMPLETE |

## Key raw evidence

| Artifact | Path | Notes |
|---|---|---|
| B2-R5 preserved out | `/tmp/phantomx_b2r5_out/` | Untouched |
| Phase 3 out | `/tmp/phantomx_b2r6_out/` | Untouched; includes original 7 C pilot BR261 |
| Phase 3B out | `/tmp/phantomx_b2r6b_out/` | B fixed + C escalation |
| Defective B harness preserved | `/tmp/phantomx_b2r6b_preserved/b2r6_layerb_defective.cpp` | Preserved |
| Raw control log | `/tmp/b2r6_raw_run.log` | Layer A |
| B fixed log | `/tmp/b2r6b_layerb_fixed_run.log` | Layer B corrected |
| C escalation log | `/tmp/b2r6b_layerc_esc_run.log` | Layer C esc |
| Fidelity control | `/tmp/b2r6b_fidelity2.c`, `/tmp/b2r6b_fidelity2.log` | Extended code fidelity |
| Isolation test | `/tmp/b2r6b_iso.c`, `/tmp/b2r6b_iso.log` | Open/PRAGMA contention |

## Confirmed BR261 totals (independent raw JSONL scan)

| Source | BR261 | Integrity fail |
|---|---:|---:|
| C pilot (preserved) | 7 | 0 |
| C escalation | 97 | 0 |
| B fixed | 11 | 0 |
| **Total** | **115** | **0** |

All confirmed: `op=open`, `api=sqlite3_exec`, `primary=5`, `extended=261`, `rc=5`, message contains "database is locked".

## Verdict summary

- **Reproduction:** REPRODUCED (raw-evidence confirmed, repeatable, integrity OK).
- **Root cause:** LIKELY (UNCONFIRMED) — concurrent open/init PRAGMA contention on `SqliteDb/ChainDatabase` open path; WAL crash recovery not necessary per isolation. Not confirmed to remediation standard.
- **Production remediation:** NOT AUTHORIZED / NOT PERFORMED.
- **TESTNET GO:** NO-GO. **VPS:** FROZEN.