# PHASE B2-R6 — Layer B Corrected Harness Report

**Date:** 2026-11-26  
**Status:** COMPLETE (pilot+escalation)  
**Scope:** Phase 3B item 1 — correct invalid `db.value()` error-path dereference, preserve defective evidence, rerun Layer B.

## 1. Preservation

| Artifact | Preserved at |
|---|---|
| Defective harness | `/tmp/phantomx_b2r6b_preserved/b2r6_layerb_defective.cpp` |
| Defective run log | `/tmp/phantomx_b2r6b_preserved/b2r6_layerb_run.log` |
| Defective summary | `/tmp/phantomx_b2r6b_preserved/layerb_summary.csv` |
| Original Layer B JSONL/CSV | `/tmp/phantomx_b2r6_out/pilot__B_fresh_*`, `/tmp/phantomx_b2r6_out/layerb_summary.csv` (untouched) |

## 2. Defect corrected

**Defect:** on `SqliteDb::open()` failure, worker emitted `db.value().last_diag()` (UB/dereference of invalid Result value), producing rc/primary/extended=0, msg="".  
**Fix:** on open failure, parse `db.error()` via `Error::to_string()` and extract `primary/extended/rc/api`; record msg and wal state. All failure paths now emit valid diagnostics. New harness: `/tmp/b2r6_layerb_fixed.cpp`. Separate output: `/tmp/phantomx_b2r6b_out/`.

## 3. Rerun matrix (disjoint IDs)

| Run | Config | IDs | Iters | Workers |
|---|---|---:|---:|---:|
| pilotB | B_fresh_w4 | 2000-2099 | 100 | 4 |
| escB | B_fresh_w4 | 3000-3899 | 900 | 4 |
| pilotB | B_fresh_w2 | 2000-2099 | 100 | 2 |
| escB | B_fresh_w2 | 3000-3899 | 900 | 2 |

## 4. Results (from summary + independent scan)

| Run | Cfg | Iters | Ops | OK | Fail | BUSY5 | BR261 | SNAP517 | LOCKED6 | LSH262 | Other | Integ fail |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| pilotB | B_fresh_w4 | 100 | 1016 | 770 | 246 | 246 | 0 | 0 | 0 | 0 | 0 | 0 |
| escB | B_fresh_w4 | 900 | 8836 | 6545 | 2291 | 2280 | 11 | 0 | 0 | 0 | 0 | 0 |
| pilotB | B_fresh_w2 | 100 | 372 | 215 | 157 | 157 | 0 | 0 | 0 | 0 | 0 | 0 |
| escB | B_fresh_w2 | 900 | 3856 | 2570 | 1286 | 1286 | 0 | 0 | 0 | 0 | 0 | 0 |
| **TOTAL** | | **2000** | **14080** | **10100** | **3980** | **3969** | **11** | **0** | **0** | **0** | **0** | **0** |

Independent JSONL scan: 14080 ops, 3980 fail, busy5 3969, br261 11, other 0. Matches summary.

## 5. BR261 records (all independently confirmed)

All 11 on `open`, `sqlite3_exec`, `p=5 e=261 rc=5`, `database is locked`, `escB B_fresh_w4`:

| File | PID | mono_ns |
|---|---:|---:|
| `escB__B_fresh_w4__i3871__w3.jsonl` | 159097 | 27532930696287 |
| `escB__B_fresh_w4__i3837__w3.jsonl` | 158961 | 27532748957182 |
| `escB__B_fresh_w4__i3815__w2.jsonl` | 158872 | 27532621548269 |
| `escB__B_fresh_w4__i3810__w3.jsonl` | 158853 | 27532593707530 |
| `escB__B_fresh_w4__i3767__w2.jsonl` | 158680 | 27532306384374 |
| `escB__B_fresh_w4__i3734__w2.jsonl` | 158548 | 27532046276146 |
| `escB__B_fresh_w4__i3711__w3.jsonl` | 158457 | 27531944169342 |
| `escB__B_fresh_w4__i3678__w3.jsonl` | 158325 | 27531778400551 |
| `escB__B_fresh_w4__i3602__w3.jsonl` | 158021 | 27531356298430 |
| `escB__B_fresh_w4__i3553__w3.jsonl` | 157825 | 27531097045924 |
| `escB__B_fresh_w4__i3035__w1.jsonl` | 155751 | 27528233510007 |

## 6. Verdict

- Harness defect corrected; diagnostics now auditable.
- **BR261 reproduced in corrected Layer B** (11/14080 ops, 1 config, escalation only; integrity 0). 
- Evidence supports concurrent `SqliteDb::open` init/PRAGMA contention producing 261 in production wrapper path. Root cause not fully isolated.