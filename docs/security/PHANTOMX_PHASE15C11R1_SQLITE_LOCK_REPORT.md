# PHANTOMX Phase 15C.11-R1 SQLite Lock Report

**Status:** PARTIAL  
**Date:** 2026-04-30 (workspace execution)  
**Baseline commit:** `d094596` (`encryption`)  
**Worktree:** dirty, preserved (no reset/clean/revert).

## 1. Scope
Investigate intermittent `database is locked` failures in challenge quota concurrency harness and remediate only with evidence. No genesis/config/network/monetary/reward/block/consensus rule changes. No consensus-authoritative registry. No P2P verifier registration. No PoP protocol change.

## 2. Reproduction and classification
- Harness: `tests/harness/challenge_quota_race.cpp` (production `ChainDatabase::put_verifier_challenge_with_quota`, independent DB connections, 32 concurrent threads).
- Before R1 fix: failures observed as `io_error: sqlite exec: database is locked` (no extended code in message at that point). After adding extended code logging in `src/database/sqlite_db.cpp`, single-run repro attempts did not capture a locked error in 20 quick runs; aggregate 100-iteration campaigns still showed failures with old text form before rebuild in earlier runs. **Extended code capture: NOT VERIFIED** (intermittent, not captured after instrumentation rebuild in sampled debug runs).
- Classification: observed lock contention manifests as `SQLITE_BUSY`-class "database is locked" from sqlite exec/begin paths; no `SQLITE_LOCKED`/`SQLITE_BUSY_TIMEOUT` explicitly captured after instrumentation. **PARTIAL**.

## 3. Transaction lifecycle audit
- `put_verifier_challenge_with_quota` (`src/blockchain/chain.cpp`): `BEGIN IMMEDIATE` tx, cleanup, global count, per-identity count, quota validation, INSERT, COMMIT; rollback on validation/statement failures. Busy retry added for begin/statement/commit errors where message contains `database is locked`/`database is busy`.
- Risk noted: retry on generic busy errors is only safe before ambiguous commit success; commit retry currently guarded by busy message and attempt limit, but ambiguous-commit proof not established. **NOT VERIFIED** for full commit-ambiguity safety.
- `consume_verifier_challenge` remains atomic conditional UPDATE in tx with `sqlite3_changes==1` - unchanged.
- `cleanup_verifier_challenges`/count helpers unchanged; statement lifetimes scoped. Full lifecycle proof: **PARTIAL**.

## 4. Busy/locked policy
- Existing SQLite config: WAL, synchronous FULL, FULLMUTEX, busy_timeout 5000 (`src/database/sqlite_db.cpp`).
- R1 change: added busy/locked retry with backoff in `put_verifier_challenge_with_quota` (20 attempts, `1+attempt` ms). Rationale: transient write contention across independent connections can return busy/locked; retry before commit/after rollback is safer than surfacing transient contention. 
- Not a root-cause proof: contention still observed in 100-iteration campaigns. Increasing retries is **not claimed** as root-cause fix.
- Status: **IMPLEMENTED/PARTIAL**, root-cause **NOT VERIFIED**.

## 5. Failure injection evidence
- Writer lock/BUSY/LOCKED/timeout/insert failure/cleanup failure/rollback/read-only/unavailable/corruption/disk-full/commit failure: **NOT IMPLEMENTED / NOT VERIFIED**. No reproducible SQLite-native fault-injection harness added.

## 6. Quota concurrency regression
### Per-identity (7 active, 32 threads, independent connections, 100 iterations)
- Result: **92 PASS / 8 FAIL** (100 iterations). Failures: intermittent `err=1` or `err=2` with no quota overshoot observed (`identity=8, global=8` in recorded failures). Acceptance 100/100: **NOT MET**.

### Global (4095 active, 32 threads, distinct identities, 100 iterations)
- Result: **97 PASS / 3 FAIL** (100 iterations). Failures: intermittent `err=1` with no overshoot observed (`global=4096`). Acceptance 100/100: **NOT MET**.

## 7. Cross-process creation
- **NOT IMPLEMENTED**. No independent-process quota-creation harness for 2/4 processes. Thread/connection tests not reported as process tests.

## 8. Final regression
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS**
- `./build-ubuntu/tests/phantomx_unit_tests` — **121 passed, 0 failed, 121 total**
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS** (0.54s)
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS**

## 9. Files changed
- `src/database/sqlite_db.cpp` — added extended SQLite primary/extended/rc codes to error messages.
- `src/blockchain/chain.cpp` — busy/locked retry/backoff in `put_verifier_challenge_with_quota`; added `<thread>` include.
- `tests/harness/challenge_quota_race.cpp` — record open-db errors into result files.
- `docs/security/PHANTOMX_PHASE15C11R1_SQLITE_LOCK_REPORT.md` — this report.

## 10. Remaining risks / acceptance
| Criterion | Status |
|---|---|
| Root cause lock contention explained with evidence | PARTIAL / NOT VERIFIED |
| Transaction lifecycle safe on tested paths | PARTIAL |
| No false success/quota bypass observed | TESTED (no overshoot observed in captured runs) / NOT VERIFIED exhaustively |
| Per-identity 100 iterations pass, no errors | **NOT MET** (92/100) |
| Global 100 iterations pass, no errors | **NOT MET** (97/100) |
| Cross-process creation tested | NOT IMPLEMENTED |
| Reproducible failure handling tests pass | NOT IMPLEMENTED / NOT VERIFIED |
| Final unit/CTest/daemon PASS | PASS |
| Evidence report available | IMPLEMENTED |

## Verdict
**Phase 15C.11-R1: PARTIAL. Mainnet/Production testnet: NO-GO.**
