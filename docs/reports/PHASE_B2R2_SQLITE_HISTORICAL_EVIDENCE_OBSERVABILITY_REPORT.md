# PHASE B2-R2 — SQLite Historical Evidence Audit & Observability Plan

**Status: PARTIAL (read-only audit/plan; no implementation)**  
**Date:** 2026-11-26  
**Mode:** STRICT PLAN / READ-ONLY INVESTIGATION  
**Branch:** `encryption`  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged)  
**Dirty worktree:** 114 entries at B2-R2 start (`/tmp/b2r2_baseline_status.txt`)  
**Preservation:** No destructive git operations; no production source changes; no canonical testnet config changes; no instrumentation/stress tests executed.

## 1. Baseline preservation

| Item | Value |
|---|---|
| `git rev-parse HEAD` | `d094596549a53eb0695527df06729eca41e737e9` |
| `git branch --show-current` | `encryption` |
| `git status --short | wc -l` | 114 |
| Baseline snapshot | `/tmp/b2r2_baseline_status.txt` |
| Production code modified | None |
| Stress tests executed | None (explicitly not authorized) |

## 2. Historical evidence inventory

| ID | Source | Claim | Primary/Extended | Context | Raw artifact referenced | Artifact present now | Classification |
|---|---|---|---:|---|---|---:|---|
| H-R4-1 | `docs/security/PHANTOMX_PHASE15C11R4_BUSY_RECOVERY_REPORT.md` (?? untracked) | Quota race reproduced `io_error: sqlite exec: database is locked [primary=5 extended=261 rc=5]` | 5/261 | `put_verifier_challenge_with_quota` thread/process races | Report text quotes error; no raw log file attached | No raw log file found; report untracked | **REPORT_ONLY** |
| H-R4-2 | R4 §4/§7/§8 | Thread harness R2: per-identity 99/100, 1 FAIL (261); global 97/100, 3 FAIL (261). Process 2-proc per-identity 20-iter intermittent 261 (`ok=1 reject=0 err=1 ... integrity=ok`) | 5/261 | Quota race, production DB API | Inline report values | No raw CSV/log | **REPORT_ONLY** |
| H-R18-1 | `docs/security/PHANTOMX_PHASE15C11R18_DETERMINISTIC_HARNESS_ACCEPTANCE_REPORT.md` (?? untracked) | H multi-process smoke3 reproduced `SQLITE_BUSY_RECOVERY 5/261` with overlap proof | 5/261 | R16 scenario H, concurrent RW init, multi-process | `/tmp/r18_smoke3/H/H.iterations.csv` cited with specific fields; `/tmp/r18_smoke2/H` prior | `/tmp/r18_smoke*` not present | **REPORT_CORROBORATED** (specific fields cited) but raw artifact unavailable |
| H-R19-1 | `docs/security/PHANTOMX_PHASE15C11R19_OBSERVER_AND_CAUSAL_VALIDATION_REPORT.md` (?? untracked) | H multi-process reproduced 5/261 with overlap proof; limited raw-vs-prod showed raw fresh_concurrent once `busy261=1` | 5/261 | R16 H; R19 raw-vs-prod fresh concurrent | Inline counts: raw iter3 `ok=15 fail=1 busy55=0 busy261=1` | No raw log file; report untracked | **REPORT_CORROBORATED** (specific counts) but raw artifact unavailable |
| H-TV-1 | `docs/reports/TV_AUDIT_2026-11-26.md`, `TV_BASELINE_2026-11-26.md`, `TV_FINAL_READINESS.md`, `TV_SECURITY_REPORT.md` (?? untracked) | "R18/R19 reproduced SQLITE_BUSY_RECOVERY 5/261..." / cause not proven | 5/261 | Summary assertions referencing R18/R19 | None; secondary summary | N/A | **REPORT_ONLY** (secondary) |
| H-B2-1 | `docs/reports/PHASE_B2_SQLITE_BUSY_RECOVERY_ROOT_CAUSE_REPORT.md` | Summarizes prior reports as reproduced, cause not proven | 5/261 | Investigation summary | Cites TV reports | TV reports present but untracked | **REPORT_ONLY** (tertiary) |
| H-B2R-1 | `docs/reports/PHASE_B2R_SQLITE_BUSY_RECOVERY_REPRODUCTION_REPORT.md` | B2-R controlled session: BUSY 5/5 observed; 5/261 NOT REPRODUCED | 5/5 observed, 5/261 none | R16 A-H, R19 raw/prod, probe, A/B/C/D-attempt | Commands/results in report; /tmp logs from session may exist transiently | Report present; some /tmp b2r artifacts present | **RAW_VERIFIED** for B2-R session observations (commands/results recorded in-session; artifacts partially present in /tmp) |
| H-CODE-1 | `tests/harness/r16_wal_init_harness.cpp`, `r19_raw_vs_prod.cpp`, `sqlite_busy_recovery_probe.cpp` | Harnesses can classify/detect 5/261 | 5/261 detection capability | Test harness code | Source code | Present in worktree | **RAW_VERIFIED** (capability exists), not evidence of reproduction |

## 3. Evidence credibility classification

| Classification | Items | Rationale |
|---|---:|---|
| RAW_VERIFIED | H-B2R-1, H-CODE-1 | B2-R executed in this session with recorded commands/outputs; harness source present. |
| REPORT_CORROBORATED | H-R18-1, H-R19-1 | Reports include specific fields/counts/file paths, but cited raw /tmp artifacts absent and reports are untracked. |
| REPORT_ONLY | H-R4-1, H-R4-2, H-TV-1, H-B2-1 | Assertions or inline values without attached raw logs/artifacts; TV/B2 are secondary/tertiary. |
| UNVERIFIABLE | All historical 5/261 claims from R4/R18/R19 | Raw logs/CSVs referenced are not present in `/tmp` or repo; reports untracked; cannot independently verify original runs. |
| CONTRADICTED | None | B2-R non-reproduction does not contradict historical claim (different session/conditions). |

## 4. Historical reproduction claims

| Claim | Source | Operation/context | Concurrency | Code/Extended | Evidence quality | Verifiable now? |
|---|---|---|---|---|---|---|
| 5/261 during quota race tx | R4 | `put_verifier_challenge_with_quota` BEGIN IMMEDIATE / sqlite exec | 32-thread and 2-process races | primary 5 extended 261 | Report-only | No |
| 5/261 during H multi-process concurrent RW init | R18/R19 | SqliteDb open/init PRAGMA path, scenario H | multi-process, 4 child workers, CLOCK_MONOTONIC overlap proven | primary 5 extended 261 | Report-corroborated, raw missing | No |
| 5/261 in raw fresh concurrent init (once) | R19 | raw sqlite open+WAL pragmas | 16 threads, fresh DB, 3-iter run | busy261=1 in one iter | Report-corroborated, raw missing | No |
| 5/261 not reproduced in B2-R | B2R | R16 A-H, R19, probe, matrix A-D | as tested | 0 busy261 | Raw-verified session report | Yes (session record) |

## 5. Raw evidence availability

- **Present:** B2-R report and some `/tmp/b2r_*` artifacts from this session; harness source files in worktree.
- **Missing:** `/tmp/r18_smoke2`, `/tmp/r18_smoke3`, R18/R19 raw CSV/JSON/event logs, R4 raw race logs, any original command stdout/stderr captures.
- **Git provenance:** R4/R18/R19/TV reports are `??` untracked; not in HEAD history. No git log evidence of their creation/commit.

## 6. Git history findings (read-only)

| Query | Result | Interpretation |
|---|---|---|
| `git log --all --grep="BUSY_RECOVERY\|261\|R18\|R19\|R16"` | No matches | No commits with these keywords |
| `git log -- ...sqlite_db.cpp chain.cpp r16/r19/probe` | 4798975, 24111d9, 690f0cb, 30168b7 | No recent changes to these files in tracked history; current worktree has untracked harness/report files |
| `git status` for R4/R18/R19/TV reports | `??` untracked | Reports exist in workspace, not committed; provenance limited |
| R8-R12 reports | Referenced as missing in R18/R19 | Historical gap acknowledged |

## 7. Historical/current environment comparison

| Field | R4 (report) | R18/R19 (report) | B2-R/B2-R2 (current) | Status |
|---|---|---|---|---|
| Branch/HEAD | `d094596` (`encryption`) | `d094596549a53eb0695527df06729eca41e737e9` (`encryption`) | same | Same baseline commit |
| Dirty worktree | dirty, preserved | 106 entries pre-R18 | 114 entries | Different dirty state |
| SQLite version | UNKNOWN | UNKNOWN | 3.46.1 (pkg-config/header) | UNKNOWN historical |
| OS/kernel | UNKNOWN | UNKNOWN | Ubuntu 7.0.0-44-generic x86_64 | UNKNOWN historical |
| Compiler | UNKNOWN | g++ implied by build; exact version UNKNOWN | g++ 15.2.0 | UNKNOWN/partial |
| SQLite compile options | UNKNOWN | UNKNOWN | not captured in report; can be queried read-only | UNKNOWN historical |
| Journal/sync | WAL, FULL, FULLMUTEX, busy_timeout=5000 | same stated | same in code | Likely same (code same HEAD) |
| Extended result codes enabled | UNKNOWN (sqlite3_extended_errcode used) | UNKNOWN | not explicitly enabled; extended_errcode used | Same code path |
| Busy timeout | 5000ms RW | 5000ms | 5000ms RW, 2000ms RO | Same code path |
| Concurrency | 32 threads / 2 processes quota races | H multi-process 4 workers, 1-iter smoke | R16 A-H 5/3 iters; matrix A 2/4/8 procs | Different test parameters |
| Workload | Quota tx cleanup/count/insert | DB open/init PRAGMA sequence | Mix init/open + simple tx + recovery attempt | Different workloads |
| DB lifecycle | Independent ChainDatabase opens, production quota path | Fresh/existing WAL init | Isolated /tmp DBs | Different |
| Crash timing | N/A | N/A (concurrent init) | SIGKILL after COMMIT attempt | Different |
| Logs/artifacts | Not attached | /tmp/r18_smoke* cited, missing | /tmp/b2r_* present | Current only partially retained |

## 8. Production-path review (read-only)

### Path map

1. **Open/init:** `SqliteDb::open` -> `sqlite3_open_v2(RW|CREATE|FULLMUTEX)` -> pragmas WAL/FULL/foreign_keys/temp_store/busy_timeout=5000 (`src/database/sqlite_db.cpp`). `ChainDatabase::open` -> SqliteDb open -> init_schema -> recover_after_crash -> apply_genesis_if_needed (`src/blockchain/chain.cpp`).
2. **Tx begin:** `SqliteDb::begin()` -> `BEGIN IMMEDIATE TRANSACTION`.
3. **Write/commit/rollback:** prepared statements + step/execute; `TransactionGuard` RAII rollback on destructor; explicit commit/rollback.
4. **Busy handling:** SqliteDb has busy_timeout PRAGMA only, no busy handler. Quota path `put_verifier_challenge_with_quota` has app-level retry for busy/locked (primary 5/6 or message match), max 20 attempts, backoff, rollback before retry; commit ambiguity lookup on commit failure.
5. **Recovery:** SQLite WAL recovery is internal to SQLite; app has `recover_after_crash()` and `integrity_check()` paths in ChainDatabase, but 5/261 occurs inside SQLite open/init/tx operations.

### Harness coverage gaps

| Production path | Raw sqlite harness | SqliteDb/R16/R19 | ChainDatabase/quota | RPC caller | Gap |
|---|---:|---:|---:|---:|---|
| sqlite3_open_v2 + pragmas | Yes | Yes | Indirect | No | RPC lifecycle not exercised |
| BEGIN IMMEDIATE busy | Partial | Partial | Yes (quota) | Indirect | Multi-process RPC not tested |
| Commit ambiguity | No | No | Code exists; no fault injection | No | **Gap** |
| WAL recovery after crash | Partial attempt | Limited | No dedicated test | No | **Gap** |
| Multi-process init/recovery | Probe/H limited | R16 H (historical claim) | No | No | **Gap** - historical 5/261 claim here |
| Long reader blocking recovery | Probe attempt | R16 F/G | No | No | Timing-sensitive gap |
| Checkpoint contention | Minimal | No dedicated | No | No | **Gap** |

## 9. Observability proposal (plan only - NO IMPLEMENTATION)

### Goals

Distinguish `SQLITE_BUSY (5/5)`, `SQLITE_BUSY_RECOVERY (5/261)`, `SQLITE_BUSY_SNAPSHOT (5/517)`, `SQLITE_LOCKED (6)`, `SQLITE_LOCKED_SHAREDCACHE (6/262)`, and other extended codes. Capture enough context to identify operation, tx state, DB lifecycle, WAL/recovery conditions, and cross-process correlation.

### Fields to capture per event

- `ts_monotonic_ns`, `ts_wall_iso8601`
- `pid`, `tid`, `process_name`, `build_id/git_head` (if safe)
- `db_path_hash` or redacted identifier (no secrets)
- `connection_id`, `open_attempt_id`, `lifecycle_generation`
- `operation` (open/pragma/begin/stmt_prepare/bind/step/commit/rollback/checkpoint/close)
- `tx_state` (none/active/committed/rolled_back/ambiguous)
- `sqlite_api`
- `rc`, `primary`, `extended`, `sqlite_errmsg`
- `busy_timeout_ms`, `retry_attempt`, `backoff_us`, `retry_decision`
- `elapsed_us`
- `wal_exists`, `shm_exists`, `db_exists`, `wal_size`, `shm_size` (where safe/cheap)
- `journal_mode`, `locking_mode`, `synchronous` (on open/snapshot)
- `checkpoint_mode/result` if checkpoint
- `commit_ambiguity_lookup_result` for quota/critical tx
- `final_result` (ok/error/retry/ambiguous_committed/conflict)

### Privacy/safety

- No SQL parameter values containing secrets/private keys/tokens.
- No raw auth credentials or PII.
- Redact DB paths if they may contain sensitive usernames; prefer hash/label.
- Bound log size/rate; avoid logging large blobs.

### Correlation

- Include `run_id`, `connection_id`, monotonic sequence, PID/TID.
- For multi-process, use CLOCK_MONOTONIC timestamps in logs and include boot id if available.
- Emit structured JSON lines to per-process file; merge offline by monotonic timestamp + pid.

### Instrumentation levels (proposal)

| Level | Scope | Risk | Use |
|---|---|---:|---|
| Test-only harness instrumentation | tests/harness only | Low | Reproduction/debug; preferred first |
| Temporary local instrumentation | local debug build, guarded by env flag, never enabled by default | Medium | Short-lived diagnosis; must be removed before merge unless approved |
| Production observability | src/database + chain, compile/runtime flag, rate-limited, redacted | Medium-High | Only after owner approval; requires regression/sanitizer/security review |

**No instrumentation implemented in this phase.**

## 10. Next reproduction strategy

| Priority | Experiment | Hypothesis | Evidence | Setup | Workload | Distinguishing observation | Success | Integrity checks | Risk |
|---:|---|---|---|---|---|---|---|---|---|
| 1 | Rebuild historical R18/R19 H scenario with high iterations and event capture | 5/261 occurs in multi-process concurrent init/WAL recovery window | R18/R19 report-corroborated | Isolated /tmp DB per iter, 4+ processes, CLOCK_MONOTONIC, 100-1000 iters | R16 scenario H equivalent | any `primary=5 extended=261` with full event log | >=1 reproducible 5/261 with raw log | integrity_check, reopen, schema, row count | Low (disposable DB) |
| 2 | Raw SIGKILL-WAL-recovery contention harness | 5/261 occurs when opener recovers WAL while reader holds lock | SQLite semantics + probe | create WAL, SIGKILL writer, hold reader tx, probe open/query/checkpoint | 1000 tries, varying busy_timeout | 5/261 with WAL/shm state captured | reproducible 5/261 | integrity_check after recovery | Low |
| 3 | Production-wrapper open/init contention (ChainDatabase/SqliteDb) | Wrapper/init sequencing contributes | R19 limited raw-vs-prod showed raw 261 once; wrapper showed busy55 | Use SqliteDb::open in isolated harness, no prod code change | concurrent opens, 16-64 threads/procs | compare 5/5 vs 5/261 vs success | statistically meaningful distribution | integrity/open checks | Low |
| 4 | Quota tx contention with commit ambiguity fault injection (test-only) | R4 261 in quota path may be tx contention, not WAL recovery | R4 report-only | isolated ChainDatabase, 32 threads/procs | put_verifier_challenge_with_quota | 5/261 or 5/5, ambiguity outcomes | reproduce and classify | no overshoot, no dup, integrity ok | Medium (must not touch prod DB) |
| 5 | Environment/build-option diff capture | Historical environment differed | UNKNOWN historical fields | read-only `sqlite3_compileoption_get`, ldd, kernel, fs | N/A | diff matrix | complete matrix | N/A | None |

## 11. Root-cause classification

| Question | Classification | Rationale |
|---|---|---|
| Historical 5/261 occurred? | **UNVERIFIED** (historically reported; raw unavailable) | R4/R18/R19 reports claim 5/261, but artifacts untracked and raw logs missing. |
| Current session reproduced 5/261? | **NOT REPRODUCED** | B2-R: 0 busy261 across executed matrix. |
| Root cause of historical 5/261 | **UNVERIFIED** | No raw evidence; multiple plausible contexts (quota tx contention vs WAL init/recovery contention). |
| Most likely technical domain if real | **LIKELY** WAL recovery/read-lock contention or concurrent open/init contention; quota-path busy contention also plausible for R4 context | SQLite 5/261 semantics; R18/R19 scenario H; R4 quota race. Still not confirmed. |

## 12. Data-integrity risks

| Risk | Level | Notes |
|---|---|---|
| Quota overshoot/duplicate challenge on busy recovery | Low-Medium | Quota tx has BEGIN IMMEDIATE + retry + commit ambiguity lookup; not fault-injection proven. |
| Silent corruption | Low | No evidence; integrity checks passed in B2-R. |
| Availability degradation under contention | Medium-High | Busy/5/261 can fail operations; historical reports indicate intermittent failures. |
| Commit ambiguity after process crash | Unverified | Requires crash/fault injection. |

## 13. Proposed future implementation scope (owner approval required)

- Test-only deterministic harness for WAL recovery contention and multi-process init.
- Observability-only instrumentation behind flag (test-only first).
- If reproduced and confirmed: minimal production fix in SqliteDb/ChainDatabase with strict regression/fault-injection/sanitizer gates.
- No protocol/consensus changes.

## 14. Explicit limitations

- No raw historical logs/CSVs for R4/R18/R19 claims available.
- R4/R18/R19/TV reports are untracked worktree files, not git-tracked.
- SQLite version/build options for historical runs UNKNOWN.
- B2-R non-reproduction is not proof of absence.
- No additional stress tests run in B2-R2.

## 15. Final verdict

**PHASE B2-R2: PARTIAL / COMPLETE-FOR-PLAN-SCOPE** — Historical evidence audited, credibility classified, production path reviewed, observability and next-experiment plan produced. Root cause remains **UNVERIFIED**; historical 5/261 **UNVERIFIED from raw evidence**; current session **NOT REPRODUCED**. No production implementation.
