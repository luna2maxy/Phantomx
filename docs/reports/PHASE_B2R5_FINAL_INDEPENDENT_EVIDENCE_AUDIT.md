# PHASE B2-R5 — Final Independent Evidence Audit

**Audit mode:** STRICT AUDIT — NO IMPLEMENTATION — OWNER REVIEW  
**Overall verdict:** **PARTIAL**  
**Date:** 2026-11-26  
**Scope:** Independent audit of B2-R5 raw SQLite crash-matrix evidence only. No B2-R6, no remediation, no production changes.

## A. Executive summary

B2-R5 raw evidence exists and is internally scannable, but the audit found a material evidence-integrity issue: **summary.csv contains 8000 rows but only 7200 unique (cfg,iter) records; 800 rows are duplicates (iters 0-99 per 8 configs, pilot + escalation)**. The escalation pass reused iteration IDs 0-899, overwriting pilot per-worker JSONL/trace files for iters 0-99. Verifiable per-worker raw evidence covers **7200 unique iterations / 214200 ops**, all OK, 0 sqlite error codes (BUSY5/BR261/SNAP517/LOCKED6/LSH262/other=0). The reported 238000 ops / 8000 iters claim is **not fully independently verifiable** from per-worker raw files due to duplicate/overwritten pilot evidence. No SQLITE_BUSY_RECOVERY 261 observed in verifiable raw evidence. Production-path comparison remains **NOT EXECUTED**.

## B. Baseline and worktree preservation

| Item | Observed |
|---|---|
| HEAD | `d094596549a53eb0695527df06729eca41e737e9` |
| Branch | `encryption` |
| `git status --short` count | 114 |
| Baseline files | `/tmp/b2r5_baseline_status.txt` present (SHA256 `e8e4cc08...`), `/tmp/b2r5_start_time.txt` present (`e29d2c65...`) |
| Dirty worktree preserved | PASS (no destructive git ops performed by audit) |
| Production source modified by audit | NO |

## C. Evidence inventory and SHA-256

| Path | Type | Size (bytes) | SHA-256 | Role | Independently verified |
|---|---|---:|---|---|---|
| `/home/ubuntu/b2r5_harness.c` | C source | 14612 | `d474cf60b75bc13fa9fde4dcf0d2ea64da3259b9f3b22fffa3b90b80f3c7b72e` | Test harness source | Read/hashed |
| `/home/ubuntu/b2r5_harness` | ELF binary | 30680 | `763cd9b16ff1a6badb472308c4368e5fa94b50b6d52f5ee6f7546a91c493cbb0` | Compiled harness | Hashed (not rebuilt) |
| `/tmp/b2r5_run.log` | log | 3716 | `e665dad8500a8f67ba9eef02b3430839daa1a013c54ffa084558f9624b94b617` | Run summary | Read |
| `/tmp/phantomx_b2r5_out/summary.csv` | CSV | 450335 | `ce90ff2bd3eb168e3d7b665793e93d6ed675640f6907f579a77c60398d572a7f` | Per-iter summary | Parsed; duplicates found |
| `/tmp/phantomx_b2r5_out/*.jsonl` | JSONL | 30600 files | Not individually hashed in report (inventory large) | Per-worker raw events | Scanned for codes/counts |
| `/tmp/phantomx_b2r5_out/*.trace` | trace | 30600 files | Not individually hashed | Per-worker intervals | Scanned for overlap |
| `docs/reports/PHASE_B2R5_PRODUCTION_PATH_SQLITE_RECOVERY_REPORT.md` | report | 7107 | `052d67b92276a8f48e7e387cf90847e476e24ef69845e5f57e9045c5a08b8ab9` | B2-R5 report | Read |
| `/tmp/b2r5_baseline_status.txt` | baseline | 3362 | `e8e4cc08a3b4e09edddbcf214cf93dd4874393dd40b961dde5ca4275c443e1c0` | Baseline | Hashed |
| `/tmp/b2r5_start_time.txt` | baseline | 21 | `e29d2c6573060f3dfac2d876d03b642ad11ab2fd45f2ceaa121caea710e1eb39` | Start time | Hashed |
| `/tmp/phantomx_b2r5_out/independent_overlap_audit.csv` | audit CSV | generated | audit artifact | Independent overlap | Generated |
| `/tmp/phantomx_b2r5_out/independent_integrity_audit.csv` | audit CSV | generated | audit artifact | Independent integrity | Generated |

## D. Iteration matrix verification

### Claimed vs verifiable

| Configuration | Claimed expected | Summary rows | Unique (cfg,iter) in CSV | Per-worker files present (iters) | Verifiable unique iters | Duplicate summary rows |
|---|---:|---:|---:|---:|---:|---:|
| R5_fresh_w4 | 1000 | 1000 | 900 (0-899) | 900 | 900 | 100 |
| R5_after_begin_w4 | 1000 | 1000 | 900 | 900 | 900 | 100 |
| R5_mid_write_w4 | 1000 | 1000 | 900 | 900 | 900 | 100 |
| R5_before_commit_w4 | 1000 | 1000 | 900 | 900 | 900 | 100 |
| R5_after_commit_w4 | 1000 | 1000 | 900 | 900 | 900 | 100 |
| R5_after_commit_noshm_w4 | 1000 | 1000 | 900 | 900 | 900 | 100 |
| R5_after_commit_noshm_w2 | 1000 | 1000 | 900 | 900 | 900 | 100 |
| R5_after_commit_noshm_w8 | 1000 | 1000 | 900 | 900 | 900 | 100 |
| **TOTAL** | **8000** | **8000** | **7200** | **900/cfg** | **7200** | **800** |

**Finding:** `run_cfg` always iterates `it=0..iters-1`; pilot (100) then escalation (900) caused escalation to overwrite pilot per-worker JSONL/trace for iters 0-99. Summary CSV appended both, producing 800 duplicate (cfg,iter) rows. **The 8000/238000 claim is not fully independently verifiable from per-worker raw evidence.** Verifiable: 7200 unique iters, 214200 ops.

## E. Independent SQLite result-code counts

### From verifiable per-worker JSONL (30600 files, 7200 unique iters)

| Result | Code | Count |
|---|---:|---:|
| SQLITE_BUSY | 5 | 0 |
| SQLITE_BUSY_RECOVERY | 261 | 0 |
| SQLITE_BUSY_SNAPSHOT | 517 | 0 |
| SQLITE_LOCKED | 6 | 0 |
| SQLITE_LOCKED_SHAREDCACHE | 262 | 0 |
| Other errors | - | 0 |
| OK/ROW/DONE | - | 214200 |
| **Total ops** | - | **214200** |

### From summary.csv (includes 800 duplicate rows)

Total rows 8000, all br261/snap517/locked6/lsh262/busy5/other/fail = 0, ops sum 238000. Duplicates make this not independent raw proof.

**Audit conclusion:** `SQLITE_BUSY_RECOVERY NOT REPRODUCED IN B2-R5 RAW SQLITE EXPERIMENT` (within verifiable raw evidence). No 261 observed.

## F. Crash-point verification

| Crash point | Harness code evidence | Runtime event evidence | Independent external trace | Verdict |
|---|---|---|---|---|
| fresh | Yes | Yes (summary/worker files) | No | PARTIAL |
| after BEGIN | Yes (SIGKILL after BEGIN IMMEDIATE) | Yes (consistent post-state 100 rows) | No | PARTIAL |
| mid-write | Yes (SIGKILL at i==750) | Yes (100 rows) | No | PARTIAL |
| before COMMIT | Yes (SIGKILL before COMMIT) | Yes (100 rows) | No | PARTIAL |
| after COMMIT | Yes (COMMIT then SIGKILL) | Yes (1600 rows consistent) | No | PARTIAL |
| after COMMIT noshm | Yes (COMMIT, SIGKILL, unlink shm) | Yes (1600 rows, integ ok) | No | PARTIAL |

No WAL recovery contention is claimed proven. SHM removal is not proof of recovery contention.

## G. Independent overlap audit

Artifact: `/tmp/phantomx_b2r5_out/independent_overlap_audit.csv`.

| Metric | Value |
|---|---|
| Trace files scanned | 30600 |
| Verifiable unique iters | 7200 |
| Iterations with any worker-interval overlap | 7197 |
| Iterations without detected overlap | 3 |
| Invalid intervals (start>end or negative) | 0 |
| Reported claim | 7997/8000 |
| Audit verifiable claim | **7197/7200** |

Overlap is **worker SQLite-operation interval overlap** (`[t_start,t_end]` in worker_open trace). Not process-lifetime-only overlap. Not independent syscall trace proof. PIDs not recorded in trace file (marked NA in audit CSV) — minor trace-format limitation.

## H. Independent integrity and row-count audit

Artifact: `/tmp/phantomx_b2r5_out/independent_integrity_audit.csv` (7200 unique rows, corrected expected values).

| Check | Result |
|---|---|
| `PRAGMA integrity_check` failures | 0 (all verifiable unique iterations) |
| Row mismatches with corrected expected | 0 |
| fresh/after_begin/mid_write/before_commit expected 100 | All 100, integ OK |
| after_commit/after_commit_noshm expected 1600 | All 1600, integ OK |
| Duplicate/missing/partial row counts | NA (not individually computed per-row diff; integrity+count consistent) |

## I. After-commit row mismatch investigation

- **Reported:** harness reported row_mismatch, actual 1600, integrity OK, attributed to harness expected-value bug.
- **Audit:** Confirmed. After_commit configs consistently 1600 rows across all 4000 summary rows and sampled DB (`/tmp/phantomx_b2r5_R5_after_commit_w4_0.db`: rows 1600, integrity ok). Crash prep inserts 1500 rows + 100 seed = 1600 expected. Harness config used 2100.
- **Evidence handling:** mismatch disclosed in report; no evidence deleted/overwritten by audit.
- **Verdict:** **HARNESS EXPECTED-VALUE BUG CONFIRMED**. No database integrity issue indicated.

## J. Pilot and escalation validation

- Pilot 100 iters/config recorded in log and summary; escalation 900 iters/config followed. Escalation decision consistent with log (integ_fail=0, other=0).
- **Issue:** escalation reused iter IDs 0-899, overwriting pilot per-worker files and duplicating summary rows. This violates clean evidence accounting and makes 8000-iteration claim not fully verifiable. Pilot stability check used `integ_fail==0 && other==0` but did not detect expected-value mismatch as a blocker (it was a harness bug, later disclosed).

## K. Production-path equivalence status

**NOT EXECUTED**. No additional evidence. B2-R5 proves only raw SQLite harness behavior under tested conditions.

## L. Missing evidence and limitations

- Pilot per-worker JSONL/trace overwritten for iters 0-99 (800 duplicate summary rows).
- No independent external crash-point trace (syscall/ptrace).
- PIDs not in trace files.
- No per-row duplicate/missing diff beyond count+integrity.
- During-checkpoint and during-init crash cases missing.
- Production-path `SqliteDb`/`ChainDatabase` comparison NOT EXECUTED.
- Claimed 7997/8000 overlap not independently verifiable as stated; verifiable 7197/7200.

## M. Overall verdict

**PARTIAL**

- Reproduction: **NOT REPRODUCED** (no 261 in verifiable raw evidence).
- Root cause: **UNVERIFIED**.
- Production-path equivalence: **NOT EXECUTED**.
- Evidence quality: material accounting defect (duplicate/overwritten pilot worker evidence) prevents accepting "8000 iters / 238000 ops fully verified".

## N. Recommended next step

Before any further phase: rerun or re-audit with non-overwriting iteration IDs (pilot 0-99, escalation 100-999), generate immutable per-iter CSV with expected/actual/integ/overlap, include PID in traces, and add independent crash-point tracing. Then, if approved, execute test-only production-path `SqliteDb`/`ChainDatabase` disposable-DB comparison with no production source changes.

## O. Explicit confirmation of scope compliance

- Production source modified: **NO**.
- Production config modified: **NO**.
- SQLite production behavior/policy modified: **NO**.
- Harness source modified: **NO** (read-only audit; audit scripts/artifacts created in `/tmp/phantomx_b2r5_out/`).
- Raw evidence deleted/overwritten: **NO**.
- B2-R5 experiments rerun: **NO**.
- B2-R6/B3/remediation started: **NO**.
- Git destructive operations: **NO**.
- Worktree preservation: **PASS** (HEAD `d094596549a53eb0695527df06729eca41e737e9`, branch `encryption`, dirty 114).
