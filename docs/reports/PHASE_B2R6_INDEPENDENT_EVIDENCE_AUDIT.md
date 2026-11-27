# PHASE B2-R6 — Independent Evidence Audit (Phase 3)

**Status:** PARTIAL  
**Date:** 2026-11-26  
**Mode:** STRICT AUDIT — NO IMPLEMENTATION / NO REMEDIATION  
**Scope:** Audit Phase 3 raw evidence only. B2-R5 evidence preserved and untouched.

## 1. Baseline

- HEAD: `d094596549a53eb0695527df06729eca41e737e9`
- Branch: `encryption`
- Dirty worktree: 114
- B2-R5 out dir preserved: `/tmp/phantomx_b2r5_out/` untouched
- B2-R6 out dir: `/tmp/phantomx_b2r6_out/` (separate)

## 2. Evidence inventory

| Artifact | Role | Status |
|---|---|---|
| `/tmp/b2r6_raw_run.log` | Layer A run log | Present |
| `/tmp/phantomx_b2r6_out/raw_summary.csv` | Layer A summary | Present |
| `/tmp/phantomx_b2r6_out/layerb_summary.csv` | Layer B summary | Present |
| `/tmp/phantomx_b2r6_out/layerc_summary.csv` | Layer C summary | Present |
| `/tmp/phantomx_b2r6_out/pilot__C_quota_*.jsonl` | Layer C raw events | Present |
| `/tmp/phantomx_b2r6_out/pilot__B_fresh_*.jsonl` | Layer B raw events | Present (recording defect noted) |
| `/tmp/phantomx_b2r6_out/*__*.jsonl/*.trace` (Layer A) | Layer A raw events/traces | Present |
| `/home/ubuntu/b2r6_raw.c`, `/tmp/b2r6_layerb.cpp`, `/tmp/b2r6_layerc.cpp` | Harness sources | Present |

## 3. Independent scans

### 3.1 Layer A (raw control)

Source: `raw_summary.csv` (3000 rows). Independent aggregation:

| Config/Run | Iters | Ops | OK | Fail | BUSY5 | BR261 | SNAP517 | LOCKED6 | LSH262 | Other | Integ fail |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fresh_w4 pilot | 100 | 2400 | 2400 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| fresh_w4 esc | 900 | 21600 | 21600 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| crash_noshm_w4 pilot | 100 | 2400 | 2400 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| crash_noshm_w4 esc | 900 | 21600 | 21600 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| crash_shm_w4 pilot | 100 | 2400 | 2400 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| crash_shm_w4 esc | 900 | 21600 | 21600 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| **TOTAL** | **3000** | **72000** | **72000** | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |

Verdict: **NOT REPRODUCED** in raw control.

### 3.2 Layer B (SqliteDb wrapper)

Independent aggregation from `layerb_summary.csv`:

| Config | Iters | Ops | OK | Fail | BUSY5 | BR261 | SNAP517 | LOCKED6 | LSH262 | Other | Integ fail |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| B_fresh_w2 | 100 | 400 | 250 | 150 | 0 | 0 | 0 | 0 | 0 | 150 | 0 |
| B_fresh_w4 | 100 | 1132 | 915 | 217 | 0 | 0 | 0 | 0 | 0 | 217 | 0 |

**Audit finding:** 367 "other" failures are harness recording defects (open failed, harness emitted rc/primary/extended=0/msg="" due to invalid `db.value()` access). Raw JSONL sample confirms. **These are INVALID as SQLite result-code evidence.** Layer B sqlite-code classification: **UNVERIFIABLE** for open failures; no confirmed 261. Integrity: 0 failures.

### 3.3 Layer C (ChainDatabase quota/open path)

Independent JSONL scan (all `pilot__C_quota_*.jsonl`, op in {open,quota_put}):

| Metric | Value |
|---|---:|
| Ops scanned | 3540 |
| OK | 3528 |
| Fail | 12 |
| BUSY5 (primary 5, extended 5) | 5 |
| BUSY_RECOVERY 261 (primary 5, extended 261) | 7 |
| SNAPSHOT 517 | 0 |
| LOCKED 6 | 0 |
| LOCKED_SHAREDCACHE 262 | 0 |
| Other | 0 |

**261 records (all confirmed in raw JSONL):**

1. `pilot__C_quota_w2__i0042__w1.jsonl` op=open p=5 e=261 `sqlite3_exec database is locked`
2. `pilot__C_quota_w4__i0085__w3.jsonl` op=open p=5 e=261
3. `pilot__C_quota_w4__i0059__w2.jsonl` op=open p=5 e=261
4. `pilot__C_quota_w4__i0059__w1.jsonl` op=open p=5 e=261
5. `pilot__C_quota_w4__i0027__w1.jsonl` op=open p=5 e=261
6. `pilot__C_quota_w4__i0011__w0.jsonl` op=open p=5 e=261
7. `pilot__C_quota_w4__i0001__w1.jsonl` op=open p=5 e=261

All integrity checks passed (integ_fail=0). All failures occurred at `ChainDatabase::open -> SqliteDb::open -> pragma exec` stage.

## 4. Evidence quality assessment

| Check | Result |
|---|---|
| B2-R5 evidence untouched | PASS |
| B2-R6 separate output dir | PASS |
| Unique IDs / no pilot-escalation overwrite | PASS (A: pilot 0-99, esc 1000-1899; B/C: pilot only 0-99) |
| Raw JSONL present for B/C | PASS |
| Layer B open-failure classification | **FAIL (harness defect)** - invalid as sqlite-code evidence |
| Layer C 261 raw evidence | PASS - 7 records with primary/extended/msg/api in JSONL |
| Integrity verification | PASS (0 integ_fail across reported layers) |
| Independent scan matches summary | PASS for A and C counts; B "other" explained as harness defect |

## 5. Verdict

- **Layer A:** NOT REPRODUCED.
- **Layer B:** INCONCLUSIVE (harness recording defect for open failures; no confirmed 261).
- **Layer C:** **REPRODUCED (PILOT, LIMITED SCOPE)** — 7x `SQLITE_BUSY_RECOVERY (primary 5, extended 261)` observed on production-path `ChainDatabase/SqliteDb` open/init pragma exec under concurrent multi-process contention; integrity OK. Repeatable within pilot (multiple iters/configs).
- **Overall Phase 3 verdict:** **PARTIAL / REPRODUCED (LIMITED)**. Not a confirmed root-cause closure; not a fix.
- **Root-cause status:** **LIKELY (UNCONFIRMED)** — concurrent open/init WAL recovery/lock contention on production open path. Requires isolated confirmation, extended_result_codes control, and before/after fix evidence.
- **Production modified:** NO.

## 6. Required next steps (owner review)

1. Fix Layer B harness recording defect; rerun Layer B pilot under approved scope.
2. Escalate Layer C 261-bearing configs to 1000 iterations with immutable disjoint IDs and independent audit.
3. Add control proving extended=261 capture fidelity (sqlite3_extended_result_codes on vs off).
4. Isolate open/init pragma vs WAL recovery mechanism with dedicated tracing.
5. Only after confirmed root cause: propose minimal fix for owner approval. No speculative remediation.

**TESTNET GO NO-GO. VPS FROZEN.**