# PHASE B2-R6 — Reproduction Report (Phase 3)

**Status:** PARTIAL (pilot completed; escalation limited/not required by all configs)  
**Date:** 2026-11-26  
**Mode:** CONTROLLED TEST-ONLY REPRODUCTION — NO PRODUCTION CHANGES  
**Branch/HEAD:** `encryption` / `d094596549a53eb0695527df06729eca41e737e9` (dirty 114 preserved)  
**B2-R5 evidence:** untouched. B2-R6 output: `/tmp/phantomx_b2r6_out/`.

## 1. Scope executed

| Layer | Description | Executed | Notes |
|---|---|---|---|
| A | Raw SQLite control (extended_result_codes ON) | YES | 3 configs, pilot 100 + escalate 900 = 1000 iters/config |
| B | `db::SqliteDb` wrapper production-path (disposable DB) | YES (pilot) | 2 configs, 100 iters/config. Harness recording defect on open-fail path; see §5. |
| C | `ChainDatabase` quota/open production-path (disposable DB) | YES (pilot) | 2 configs, 100 iters/config, 4/2 workers, 5 quota puts/worker. **261 observed**. |

No production source/config/SQLite behavior changes. No B2-R5 evidence modified. No speculative remediation.

## 2. Environment

- Linux x86_64, SQLite 3.46.1, gcc/g++ 15.2.0
- Build artifacts: `build-ubuntu/libphantomx_common.a` (existing, not rebuilt from modified source)
- Harnesses: `/home/ubuntu/b2r6_raw.c`, `/tmp/b2r6_layerb.cpp`, `/tmp/b2r6_layerc.cpp`
- Output: `/tmp/phantomx_b2r6_out/` (separate from B2-R5)

## 3. Layer A — Raw SQLite control

Extended result codes enabled in control harness. Pragmas aligned with production where applicable (WAL, FULL, busy_timeout 100ms in workers).

| Config | Run | Iters | Ops | OK | Fail | BUSY5 | BR261 | SNAP517 | LOCKED6 | LSH262 | Other | Integ fail | Overlap |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fresh_w4 | pilot | 100 | 2400 | 2400 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100 |
| fresh_w4 | esc | 900 | 21600 | 21600 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 900 |
| crash_noshm_w4 | pilot | 100 | 2400 | 2400 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100 |
| crash_noshm_w4 | esc | 900 | 21600 | 21600 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 900 |
| crash_shm_w4 | pilot | 100 | 2400 | 2400 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100 |
| crash_shm_w4 | esc | 900 | 21600 | 21600 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 900 |
| **TOTAL** | | **3000** | **72000** | **72000** | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **3000** |

**Result:** NOT REPRODUCED in raw control.

## 4. Layer B — SqliteDb wrapper (production code path)

Uses existing `db::SqliteDb::open/exec/begin/commit/prepare` on disposable DBs. No production source changes.

| Config | Iters | Ops | OK | Fail | BUSY5 | BR261 | SNAP517 | LOCKED6 | LSH262 | Other | Integ fail |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| B_fresh_w2 | 100 | 400 | 250 | 150 | 0 | 0 | 0 | 0 | 0 | 150 | 0 |
| B_fresh_w4 | 100 | 1132 | 915 | 217 | 0 | 0 | 0 | 0 | 0 | 217 | 0 |
| **TOTAL** | **200** | **1532** | **1165** | **367** | **0** | **0** | **0** | **0** | **0** | **367** | **0** |

**Critical recording defect:** All 367 failures are `op=open`, `rc=0,primary=0,extended=0,msg=""`. Inspection shows harness bug: on open failure it dereferenced `db.value().last_diag()` (undefined behavior). These records are **INVALID / UNVERIFIABLE as SQLite evidence**. They are NOT counted as sqlite BUSY/261. Layer B reproduction evidence is **PARTIAL/INVALID for open-failure classification**; integrity checks passed on DBs that were created.

## 5. Layer C — ChainDatabase quota/open production path

Uses existing `ChainDatabase::open` and `put_verifier_challenge_with_quota` on disposable DBs. Pre-initialized DB per iter so workers contend on open/quota path. No production source changes.

| Config | Iters | Ops | OK | Fail | BUSY5 | BR261 | SNAP517 | LOCKED6 | LSH262 | Other | Integ fail |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C_quota_w2 | 100 | 1195 | 1194 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 |
| C_quota_w4 | 100 | 2345 | 2334 | 11 | 5 | 6 | 0 | 0 | 0 | 0 | 0 |
| **TOTAL** | **200** | **3540** | **3528** | **12** | **5** | **7** | **0** | **0** | **0** | **0** | **0** |

### 5.1 SQLITE_BUSY_RECOVERY (261) occurrences (raw-evidence backed)

Independent scan of `/tmp/phantomx_b2r6_out/pilot__C_quota_*.jsonl`:

| File | Op | Primary | Extended | Message |
|---|---|---:|---:|---|
| `pilot__C_quota_w2__i0042__w1.jsonl` | open | 5 | 261 | `io_error: sqlite exec: database is locked [primary=5 extended=261 rc=5 api=sqlite3_exec]` |
| `pilot__C_quota_w4__i0085__w3.jsonl` | open | 5 | 261 | same pattern |
| `pilot__C_quota_w4__i0059__w2.jsonl` | open | 5 | 261 | same pattern |
| `pilot__C_quota_w4__i0059__w1.jsonl` | open | 5 | 261 | same pattern |
| `pilot__C_quota_w4__i0027__w1.jsonl` | open | 5 | 261 | same pattern |
| `pilot__C_quota_w4__i0011__w0.jsonl` | open | 5 | 261 | same pattern |
| `pilot__C_quota_w4__i0001__w1.jsonl` | open | 5 | 261 | same pattern |

**Observed mechanism:** failure during `ChainDatabase::open -> SqliteDb::open -> pragma exec` (`sqlite3_exec`, "database is locked", primary 5, extended 261, rc 5). Not quota-tx insert/commit; not RPC layer. Integrity checks: 0 failures.

## 6. Reproduction assessment

- **SQLITE_BUSY_RECOVERY (261): REPRODUCED (PILOT, LIMITED SCOPE)** in Layer C production-path open/init contention. 7 occurrences / 3540 ops / 200 iters, 2 configs, integrity ok.
- **Raw SQLite control:** NOT REPRODUCED (72000 ops).
- **SqliteDb wrapper:** INCONCLUSIVE due harness recording defect for open failures.
- **Root-cause status:** **LIKELY (not CONFIRMED)** — evidence points to WAL recovery/init contention during concurrent `ChainDatabase/SqliteDb` open pragma execution. Causal mechanism not fully isolated (no dedicated recovery-trace, no sqlite3_extended_result_codes enabled in production path, no before/after fix).

## 7. Limitations

- Layer B open-failure recording defect invalidates 367 records for sqlite classification.
- Layer C pilot only (100 iters/config), 2 configs; not escalated to 1000.
- No sqlite3_extended_result_codes enabled in production SqliteDb; extended code captured via `sqlite3_extended_errcode()` in diag - fidelity is plausible but should be confirmed with control.
- No crash-point matrix for Layer C.
- No checkpoint/during-init dedicated tracing.
- No fix attempted (correct; not authorized).

## 8. Raw evidence

- `/tmp/b2r6_raw_run.log`
- `/tmp/phantomx_b2r6_out/raw_summary.csv`
- `/tmp/phantomx_b2r6_out/layerb_summary.csv`
- `/tmp/phantomx_b2r6_out/layerc_summary.csv`
- `/tmp/phantomx_b2r6_out/pilot__C_quota_*.jsonl`
- Harnesses: `/home/ubuntu/b2r6_raw.c`, `/tmp/b2r6_layerb.cpp`, `/tmp/b2r6_layerc.cpp`
- B2-R5 evidence untouched.