# PHASE B2-R6 — Phase 3B Independent Evidence Audit

**Date:** 2026-11-26  
**Status:** COMPLETE (audit of Phase 3B evidence)  
**Mode:** STRICT AUDIT — NO IMPLEMENTATION / NO REMEDIATION

## 1. Baseline and preservation

- HEAD `d094596549a53eb0695527df06729eca41e737e9`, branch `encryption`, dirty 114 (preserved).
- B2-R5 evidence untouched (`/tmp/phantomx_b2r5_out/`).
- Phase 3 original evidence preserved (`/tmp/phantomx_b2r6_out/`, including 7 original C pilot BR261 records).
- Defective Layer B harness preserved (`/tmp/phantomx_b2r6b_preserved/b2r6_layerb_defective.cpp`).

## 2. Independent scans

### Layer B fixed (new)
Source: `/tmp/phantomx_b2r6b_out/*B_fresh_*.jsonl`, `layerb_fixed_summary.csv`.  
Scan: 14080 ops, 10100 ok, 3980 fail, busy5 3969, **br261 11**, other 0. Integ_fail 0. Matches summary.

### Layer C escalation (new)
Source: `/tmp/phantomx_b2r6b_out/escC__C_quota_*.jsonl`, `layerc_esc_summary.csv`.  
Scan: 35455 ops, 35346 ok, 109 fail, busy5 12, **br261 97**, other 0. Integ_fail 0. Matches summary.

### Layer C pilot (preserved, re-audited)
3540 ops, 3528 ok, 12 fail, busy5 5, **br261 7**, other 0. Preserved.

### BR261 totals (independently confirmed)

| Source | BR261 | Integ fail | Evidence |
|---|---:|---:|---|
| C pilot (preserved) | 7 | 0 | `/tmp/phantomx_b2r6_out/pilot__C_quota_*.jsonl` |
| C escalation | 97 | 0 | `/tmp/phantomx_b2r6b_out/escC__C_quota_*.jsonl` |
| B fixed | 11 | 0 | `/tmp/phantomx_b2r6b_out/*B_fresh_*.jsonl` |
| **Total confirmed** | **115** | **0** | raw JSONL with pid/mono_ns/api/primary/extended/msg |

All confirmed records: `op=open`, `api=sqlite3_exec`, `primary=5`, `extended=261`, `rc=5`, `database is locked`.

## 3. Evidence quality

| Check | Result |
|---|---|
| Defective harness preserved | PASS |
| New harness separate | PASS (`/tmp/b2r6_layerb_fixed.cpp`) |
| Disjoint IDs | PASS (B: 2000-2099,3000-3899; C esc: 5000-5999,7000-7999; C pilot 0-99 preserved) |
| Per-failure diagnostics | PASS (B fixed includes rc/primary/extended/api/msg/wal/pid/mono_ns) |
| 261 raw evidence auditable | PASS |
| Fidelity control | PASS (`/tmp/b2r6b_fidelity2.c/.log`) |
| Isolation test | PASS (`/tmp/b2r6b_iso.c/.log`) |
| No production changes | PASS |

## 4. Verdict

- **Reproduction:** REPRODUCED (confirmed, multi-config, pilot+escalation).
- **BR261:** 115 confirmed raw-evidence occurrences, 0 integrity failures.
- **Root cause:** LIKELY (UNCONFIRMED) — concurrent open/init PRAGMA contention on production SqliteDb/ChainDatabase open path; WAL crash recovery not necessary per isolation test.
- **Overall:** PARTIAL completion of investigation; reproduction and isolation evidence strong; not a confirmed fix. No production remediation.