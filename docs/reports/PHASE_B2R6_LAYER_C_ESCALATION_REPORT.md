# PHASE B2-R6 — Layer C Controlled Escalation Report

**Date:** 2026-11-26  
**Status:** COMPLETE (pilot preserved + escalation complete)  
**Scope:** Phase 3B item 2 — ChainDatabase production-path escalation, 1000 iters/config, disjoint immutable IDs.

## 1. Preserved pilot evidence

Original 7 BR261 records preserved in `/tmp/phantomx_b2r6_out/pilot__C_quota_*.jsonl` and `layerc_summary.csv` (untouched). Pilot summary: 200 iters, 3540 ops, ok 3528, fail 12, busy5 5, br261 7, other 0, integ_fail 0.

## 2. Escalation matrix

| Run | Config | IDs | Iters | Workers | Ops/iter (nominal) |
|---|---|---:|---:|---:|---:|
| escC | C_quota_w4 | 5000-5999 | 1000 | 4 | 5 quota_put + open |
| escC | C_quota_w2 | 7000-7999 | 1000 | 2 | 5 quota_put + open |

Harness: `/tmp/b2r6_layerc_esc.cpp`. Output: `/tmp/phantomx_b2r6b_out/escC__C_quota_*.jsonl`, `layerc_esc_summary.csv`. DBs disposable `/tmp/phantomx_b2r6b_escC__*.db`.

## 3. Results

| Config | Iters | Ops | OK | Fail | BUSY5 | BR261 | SNAP517 | LOCKED6 | LSH262 | Other | Integ fail |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C_quota_w4 escC | 1000 | 23660 | 23592 | 68 | 10 | 58 | 0 | 0 | 0 | 0 | 0 |
| C_quota_w2 escC | 1000 | 11795 | 11754 | 41 | 2 | 39 | 0 | 0 | 0 | 0 | 0 |
| **Escalation total** | **2000** | **35455** | **35346** | **109** | **12** | **97** | **0** | **0** | **0** | **0** | **0** |
| Pilot total (preserved) | 200 | 3540 | 3528 | 12 | 5 | 7 | 0 | 0 | 0 | 0 | 0 |
| **Combined C** | **2200** | **38995** | **38874** | **121** | **17** | **104** | **0** | **0** | **0** | **0** | **0** |

## 4. New BR261 independent audit

Independent JSONL scan: 97 BR261 records in escalation, all `op=open`, `api=sqlite3_exec`, `primary=5 extended=261 rc=5`, msg `database is locked`. Each record includes run/cfg/iter/wid/pid/mono_ns/seq/rc/primary/extended/api/msg/wal state. Example records include `escC__C_quota_w2__i7986__w0.jsonl` (pid 167415) ... `escC__C_quota_w4__i5050__w0.jsonl` (pid 161622). Full list extractable from `/tmp/phantomx_b2r6b_out/escC__C_quota_*.jsonl`.

## 5. Verdict

- **BR261 REPRODUCED and confirmed by escalation** in ChainDatabase production-path open/init contention. 104 total BR261 across pilot+escalation (7+97), 0 integ_fail.
- Failures occur at `ChainDatabase::open -> SqliteDb::open -> PRAGMA journal_mode=WAL exec`. Not quota insert/commit ambiguity. Root cause still requires isolation confirmation (see root-cause report).