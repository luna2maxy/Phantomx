# PHASE B2 — SQLITE_BUSY_RECOVERY (5/261) Root-Cause Investigation Report

**Status: INVESTIGATION ONLY — NO SOURCE IMPLEMENTATION**  
**Date:** 2026-11-26  
**Branch:** `encryption`  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged)  
**Dirty worktree (baseline):** 114 entries (`/tmp/b2_invest_baseline_status.txt`)  
**Worktree preservation:** All existing changes preserved. No destructive git commands. No canonical testnet config changes.

## 1. Repository baseline

| Item | Value |
|---|---|
| Branch | `encryption` |
| HEAD | `d094596549a53eb0695527df06729eca41e737e9` |
| Dirty worktree | 114 (`git status --short` count) |
| Baseline snapshot | `/tmp/b2_invest_baseline_status.txt` |
| Source changes in this phase | None (report-only) |

## 2. SQLite version and build configuration

| Item | Evidence |
|---|---|
| SQLite runtime/header | `pkg-config --modversion sqlite3` => `3.46.1`; `/usr/include/sqlite3.h` defines `SQLITE_VERSION "3.46.1"`, `SQLITE_VERSION_NUMBER 3046001` |
| Package | `libsqlite3-dev:amd64 3.46.1-9ubuntu0.3` |
| CMake discovery | `CMakeLists.txt` finds `unofficial-sqlite3` / `SQLite3` / pkg-config `sqlite3`; links `SQLite::SQLite3` or `unofficial::sqlite3::sqlite3` |
| Extended result codes API | `sqlite3_extended_result_codes` **not called** in source (`grep` no matches). Code reads `sqlite3_extended_errcode()` in diag capture; SQLite docs state extended_errcode returns extended code even when extended result codes mode is disabled. **Fact:** extended code can be observed; explicit enablement not present. |

## 3. Database architecture and connection lifecycle

### 3.1 SqliteDb wrapper (`include/phantomx/database/sqlite_db.hpp`, `src/database/sqlite_db.cpp`)

- **Open RW:** `SqliteDb::open` (`sqlite_db.cpp:~255`) uses `sqlite3_open_v2(..., SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr)`.
- **Open RO:** `SqliteDb::open_readonly` uses `SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX`.
- **Initialization pragmas RW:** `journal_mode=WAL`, `synchronous=FULL`, `foreign_keys=ON`, `temp_store=MEMORY`, `busy_timeout=5000` (`sqlite_db.cpp:278-283`).
- **Initialization pragmas RO:** `temp_store=MEMORY`, `busy_timeout=2000` (`sqlite_db.cpp:327-330`).
- **No busy handler:** `sqlite3_busy_handler` not found.
- **No explicit extended result code enable:** `sqlite3_extended_result_codes` not found.
- **Connection mutex:** `SqliteDb` has `mutable std::recursive_mutex mu_` (header). `exec`, `prepare`, `close`, `backup_to`, `integrity_check`, `last_error` take `mu_`. `Statement` holds `mu_` pointer and locks on bind/step/finalize/column access (`sqlite_db.cpp:120-200`).
- **Error capture:** `capture_diag` records `rc`, `sqlite3_errcode`, `sqlite3_extended_errcode`, `sqlite3_errmsg` (`sqlite_db.cpp:31-42`). Errors formatted as `[primary=... extended=... rc=... api=...]`.
- **Transaction API:** `begin()` = `BEGIN IMMEDIATE TRANSACTION;`; `commit()` = `COMMIT;`; `rollback()` = `ROLLBACK;` (`sqlite_db.cpp:390-392`).
- **TransactionGuard:** destructor rolls back if active and not committed (`sqlite_db.cpp:447-452`).

### 3.2 ChainDatabase (`include/phantomx/blockchain/chain.hpp`, `src/blockchain/chain.cpp`)

- **Single DB connection:** `ChainDatabase` owns one `db::SqliteDb db_` and `std::shared_ptr<std::recursive_mutex> mu_` (header). Opened via `db::SqliteDb::open(path)` in `ChainDatabase::open` (`chain.cpp:92-109`).
- **Lifecycle:** open -> init_schema -> recover_after_crash -> apply_genesis_if_needed. Close takes `*mu_`.
- **Schema:** metadata, blocks, utxos, tx_index, undo, snapshots, verifier_challenges + indexes (`chain.cpp:113-130`).
- **Access pattern:** most public methods take `std::scoped_lock lk(*mu_)` and use `db_`. Some read methods cast away const for prepare.
- **Separate DB files in ecosystem:** aggregator uses `SqliteDb::open` to separate DB (`src/aggregator/aggregator.cpp:391,436`); BFT state/regtest use separate DB paths by design. 5/261 investigation here focuses on SQLite usage patterns, especially ChainDatabase/verifier challenge path and WAL init/open.

### 3.3 WAL/checkpoint settings

- WAL enabled on RW open (`PRAGMA journal_mode=WAL`).
- `synchronous=FULL`.
- No `wal_autocheckpoint`/`wal_checkpoint` pragma in normal open path. `sqlite3_wal_checkpoint_v2` appears in test harnesses/probes only.
- Backup uses `sqlite3_backup_*` API (`sqlite_db.cpp:404-426`).

## 4. Code paths relevant to SQLITE_BUSY / SQLITE_BUSY_RECOVERY

### 4.1 Direct code mentions

| Location | Evidence |
|---|---|
| `src/blockchain/chain.cpp:222` | `is_busy_diag` checks `primary==5 (SQLITE_BUSY)` or `primary==6 (SQLITE_LOCKED)` or message contains "database is locked"/"database is busy"; used by quota tx retry. |
| `src/blockchain/chain.cpp:170-393` | `put_verifier_challenge_with_quota`: `BEGIN IMMEDIATE`, cleanup/count/count/insert/commit inside same tx; retries on busy/locked up to 20 attempts with `sleep(1+attempt*2 ms)`; commit ambiguity lookup on commit failure. |
| `tests/harness/sqlite_busy_recovery_probe.cpp` | Standalone probe opens two connections, sets `busy_timeout=50`, WAL+FULL, begins read on A, inserts on B, runs `sqlite3_wal_checkpoint_v2(PASSIVE)` while read active - directly targets recovery/checkpoint/busy behavior observation. |
| `tests/harness/r16_wal_init_harness.cpp` | Classifies `SQLITE_BUSY primary=5 extended=261` as `SQLITE_BUSY_RECOVERY_5_261`; runs fresh/existing-WAL/concurrent/long-reader/existing-writer/multi-process init scenarios. |
| `tests/harness/r19_raw_vs_prod.cpp` | Compares raw sqlite open+pragma sequence vs `SqliteDb::open` concurrent init; counts busy55/busy261/other. |

### 4.2 Quota transaction path (verified source)

`ChainDatabase::put_verifier_challenge_with_quota` (`chain.cpp:164-393`):
- Takes `ChainDatabase` mutex `*mu_` for the whole attempt body (`std::scoped_lock lk(*mu_)` inside retry loop at ~235).
- Starts `BEGIN IMMEDIATE TRANSACTION` via `TransactionGuard`.
- On stage error: if busy/locked and attempt < 19, rollback, emit retry event, sleep `1+attempt*2 ms`, `continue`.
- On quota reject (business): rollback, return `challenge_capacity_global/identity`.
- On commit failure: emits commit error, then performs **ambiguity lookup** by `challenge_id` and compares full record fields. If same record found, returns success (`ok_ambiguous_commit`); if conflicting record, returns conflict error; if lookup errors/not found, returns error. **Does not blind-retry after commit ambiguity.**
- Final retry exhaustion: returns "challenge quota transaction busy/locked after retries".

**Observation (verified):** retry is only before commit success/ambiguity resolution; commit path has ambiguity reconciliation. This is a strong integrity pattern. It does not prove absence of all commit-ambiguity edge cases under process crash/network-RPC boundary, but it is better than blind retry.

### 4.3 ChainDatabase call sites

- `src/rpc/node_handlers.cpp:1271` calls `put_verifier_challenge_with_quota(...)`.
- Tests/harnesses call quota function directly.
- No other production caller found by grep except RPC handler and harness/tests.

## 5. Reproduction / existing evidence

### 5.1 Existing evidence (from repository reports/harnesses)

| Evidence | Status | Notes |
|---|---|---|
| `docs/reports/TV_BASELINE_2026-11-26.md` | VERIFIED | States `SQLITE_BUSY_RECOVERY (primary=5, extended=261): reproduced, cause NOT PROVEN`. |
| `docs/reports/TV_AUDIT_2026-11-26.md` | VERIFIED | Marks SQLite reliability blocker; R18/R19 reproduced 5/261; cause NOT PROVEN. |
| `docs/reports/TV_FINAL_READINESS.md` | VERIFIED | SQLITE_BUSY_RECOVERY REPRODUCED / NOT PROVEN; readiness NO-GO. |
| `docs/reports/TV_SECURITY_REPORT.md` | VERIFIED | Lists 5/261 reproduced, cause unproven - reliability risk under concurrency. |
| `tests/harness/r16_wal_init_harness.cpp` | VERIFIED file exists | Classifies and exercises 5/261 during WAL init/open scenarios. |
| `tests/harness/r19_raw_vs_prod.cpp` | VERIFIED file exists | Raw vs prod concurrent open comparison, counts 5/261. |
| `tests/harness/sqlite_busy_recovery_probe.cpp` | VERIFIED file exists | Probe for read/checkpoint/insert recovery interaction. |

### 5.2 Not reproduced in this investigation run

- No destructive/stress tests executed against user DBs.
- No new deterministic reproduction run was executed in this phase (investigation-only, report-only). Existing reports claim reproduction; this report treats those as prior evidence, not re-executed in this session.

## 6. Verified facts

1. **VERIFIED:** SQLite 3.46.1 used (pkg-config/header/package).
2. **VERIFIED:** RW connections open with `SQLITE_OPEN_FULLMUTEX`, WAL, `synchronous=FULL`, `busy_timeout=5000`; RO uses `busy_timeout=2000`.
3. **VERIFIED:** `sqlite3_busy_handler` and `sqlite3_extended_result_codes` are not explicitly set in SqliteDb.
4. **VERIFIED:** SqliteDb wraps calls with internal recursive mutex; Statement also locks per bind/step/finalize/column.
5. **VERIFIED:** ChainDatabase uses one SqliteDb connection and an outer `shared_ptr<recursive_mutex>` for most operations.
6. **VERIFIED:** `put_verifier_challenge_with_quota` uses `BEGIN IMMEDIATE`, in-tx cleanup/quota checks/insert, busy/locked retry before commit completion, and commit-ambiguity lookup by full record on commit failure.
7. **VERIFIED:** Prior project reports mark SQLITE_BUSY_RECOVERY 5/261 as reproduced, cause not proven.
8. **VERIFIED:** R16/R19/probe harness files exist in tree and target 5/261/WAL init/recovery scenarios.

## 7. Unverified hypotheses / risk areas

| Hypothesis | Status | Rationale / missing evidence |
|---|---|---|
| 5/261 primarily occurs during concurrent WAL initialization/open on same DB file (journal_mode/WAL pragma/init window) rather than steady-state tx contention | LIKELY | R16/R19 naming and classification target init/open scenarios; 5/261 is WAL recovery contention by SQLite definition; but no fresh run logs attached in this phase. |
| Long-running readers/checkpointers in separate process/thread block WAL recovery, surfacing SQLITE_BUSY_RECOVERY | LIKELY | Matches SQLite semantics and probe design; needs deterministic reproduction trace on current build. |
| ChainDatabase single-connection + FULLMUTEX + outer mutex prevents same-process multi-thread corruption for ChainDatabase ops | LIKELY | Code structure supports it, but not a proof under all callers/external processes. |
| Commit ambiguity path fully eliminates duplicate/lost challenge records under process crash/power loss at RPC boundary | UNVERIFIED | Code checks same record by challenge_id+fields; crash timing between DB commit visibility and RPC response still requires fault-injection evidence. |
| Busy timeout 5000ms is sufficient for production contention profiles | UNVERIFIED | Depends on workload, checkpoint/readers, filesystem, process count. |
| No other DB path (aggregator, BFT state, other SqliteDb users) has 5/261 exposure | UNVERIFIED | Multiple SqliteDb::open call sites exist; only ChainDatabase quota path deeply inspected here. |

## 8. Data-integrity risk assessment

| Risk | Assessment | Evidence |
|---|---|---|
| Lost update / quota overshoot in `put_verifier_challenge_with_quota` | **Mitigated/Low-Medium (code-level), Unproven under fault injection** | BEGIN IMMEDIATE, same-tx cleanup+count+insert, outer mu_, retry on busy/locked before commit, commit ambiguity lookup. Needs crash/fault-injection proof. |
| Duplicate challenge insert on commit ambiguity | **Low-Medium** | Ambiguity lookup checks full record equality by challenge_id; conflict returns error. Needs test for partial visibility/crash windows. |
| DB corruption | **Low based on inspected pattern** | SQLite WAL+FULL+synchronous, single connection, integrity_check/backup APIs exist. No direct corruption evidence seen. |
| Denial of service / availability under recovery contention | **Medium-High** | 5/261 reproduced historically; retries may exhaust; busy timeout may be insufficient under multi-process/WAL recovery. |
| Cross-process consistency | **Unverified** | SQLite serializes writers, but app-level assumptions, multiple DB files, process crashes, and external readers need validation. |

## 9. Root-cause conclusion

**Conclusion: NOT REPRODUCED in this session; LIKELY (per prior evidence + SQLite semantics) but NOT CONFIRMED as a single definitive code defect.**

- Prior project reports and harnesses indicate `SQLITE_BUSY_RECOVERY (primary=5, extended=261)` was reproduced.
- SQLite semantics: 5/261 occurs when SQLite cannot recover WAL because another connection/process holds a read lock needed for recovery. This commonly appears during open/WAL init/checkpoint/read-write contention across connections/processes.
- The inspected production code has reasonable protections for quota tx integrity (BEGIN IMMEDIATE, mutex, busy retry, commit ambiguity check), but explicit extended result code enablement/busy handler instrumentation and multi-process WAL-recovery handling are limited.
- **No single confirmed root cause** can be declared without fresh deterministic reproduction traces showing the failing call stack, connection roles, WAL/shm state, and concurrent actors on current build.

## 10. Proposed fix alternatives (NO IMPLEMENTATION)

| Alternative | Root cause target | Pros | Risks / costs | Tests required |
|---|---|---|---|---|
| Deterministic reproduction harness first | Unknown 5/261 origin | Evidence-based; avoids wrong fix | Time/cost | R16/R19/probe reruns with full diag capture |
| Enable `sqlite3_extended_result_codes(db,1)` on all connections | Observability/classification | Better diag fidelity | None functional expected | Unit/integration logs |
| Centralize busy/recovery policy in SqliteDb (retry only safe ops) | Busy/recovery handling | Consistent behavior | Wrong retry can duplicate side effects | Fault-injection, commit ambiguity tests |
| WAL init/open serialization across processes (lock file) | Multi-process init/recovery contention | May reduce 5/261 on open | Deadlock/stale lock risk; portability | Multi-process open/startup tests |
| Tune busy_timeout/backoff; avoid hot open/close | Transient contention | Simple | Masks root cause; availability tradeoff | Load/concurrency tests |
| Checkpoint policy control (PASSIVE/TRUNCATE at safe points) | WAL growth/recovery windows | Can reduce recovery work | Checkpoint can block/fail; needs careful timing | WAL checkpoint contention tests |
| Ensure read txns short-lived; audit long readers | Recovery blocked by readers | Addresses common 5/261 cause | Requires code audit across components | Long-reader fault tests |
| Commit idempotency tokens / reconciliation table for critical writes | Commit ambiguity | Strong integrity | Schema/protocol/app complexity | Crash-injection, replay tests |

**Recommendation:** start with deterministic reproduction + instrumentation on current source/build. Do not apply blind retry. Prioritize correctness/durability/consistency.

## 11. Recommended remediation plan (future, owner-approved)

1. **Reproduce deterministically** on current build with R16/R19/probe and capture full SqliteConnectionEvent/QuotaTxDiagEvent, process/thread/connection IDs, WAL/shm file states.
2. **Classify failure domain:** open/init vs steady-state write tx vs checkpoint/recovery vs cross-process.
3. **Add observability only if needed:** explicit extended result codes, structured logs for 5/261, WAL/journal/checkpoint state snapshots. (code change - future phase only).
4. **If init/open contention confirmed:** evaluate startup/open serialization or pre-open WAL recovery strategy; avoid global behavior changes without tests.
5. **If steady-state tx contention confirmed:** review busy timeout/backoff, transaction scope, long readers, checkpoint timing. Keep commit ambiguity protections.
6. **Fault-injection matrix:** busy on begin/write/commit, crash before/after commit, restart recovery, multi-process concurrent writers/readers.
7. **Only after evidence:** minimal fix, regression suite, ASAN/UBSAN/TSan rerun, BFT/regtest rerun.

## 12. Required regression and fault-injection matrix

| Category | Case | Expected |
|---|---|---|
| Single-process | Concurrent threads, multiple connections, same DB | No corruption; errors classified; no quota overshoot |
| Single-process | BEGIN IMMEDIATE contention | Retry bounded; no blind commit retry |
| Multi-process | Two+ processes RW same DB | No corruption; 5/261 observed/handled per policy |
| Multi-process | Process kill during tx | After restart: no partial commit, no duplicate challenge, integrity ok |
| Commit ambiguity | Busy/error on COMMIT, record committed | Ambiguity lookup returns ok/idempotent |
| Commit ambiguity | Busy/error on COMMIT, record absent | Error returned, no duplicate on retry |
| WAL recovery | Long reader + writer/checkpoint | 5/261 classified; bounded behavior; recovery after reader closes |
| Integrity | integrity_check, reopen, schema, persisted rows | All pass after stress/fault runs |

## 13. Files that may need modification in a future implementation phase (owner approval required)

- `src/database/sqlite_db.cpp` / `include/phantomx/database/sqlite_db.hpp` - extended result codes, busy policy/instrumentation, open/init behavior.
- `src/blockchain/chain.cpp` / `include/phantomx/blockchain/chain.hpp` - quota tx retry/ambiguity policy only if evidence demands.
- `tests/harness/r16_wal_init_harness.cpp`, `tests/harness/r19_raw_vs_prod.cpp`, `tests/harness/sqlite_busy_recovery_probe.cpp` - evidence harness refinement.
- New targeted regression/fault-injection tests under `tests/` (not weakening existing tests).
- Possibly daemon/RPC error mapping docs if behavior becomes user-visible.

## 14. Explicit out-of-scope items

- No source implementation/fix in this phase.
- No Phase B2 implementation start.
- No POI Pool work.
- No VPS provisioning/deployment/public testnet.
- No canonical testnet config changes.
- No consensus/genesis/network/monetary/reward/block/PoI semantics changes.
- TESTNET GO remains **NO-GO**. VPS deployment remains **FROZEN**.

## 15. Final investigation status

- **Phase B2 investigation:** COMPLETE (report-only).
- **Root cause:** **LIKELY, NOT CONFIRMED**. Prior evidence reports reproduction of 5/261; SQLite semantics point to WAL recovery/read-lock contention; exact current-build failing path not deterministically re-captured in this session.
- **Data integrity risk:** Medium for availability/contention; Low-Medium for quota integrity given existing protections, but unproven under crash/fault injection.
- **Implementation started:** No.
