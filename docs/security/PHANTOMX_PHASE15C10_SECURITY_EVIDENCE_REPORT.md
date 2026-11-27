# PHANTOMX Phase 15C.10 Security Evidence Report

**Status:** PARTIAL  
**Date:** 2026-04-30 (workspace execution)  
**Branch/commit baseline:** `encryption` / `d094596` (dirty worktree preserved)

## 1. Summary
Phase 15C.10 added deterministic concurrent quota harnesses and closed missing 15C.8/15C.9 reports. Critical acceptance criteria remain unmet: 100-iteration quota race campaigns not completed to required scale, cleanup/create/consume/count race not implemented, SQLite failure injection incomplete, sanitizers not run, PoP/RPC/lifecycle matrices incomplete.

## 2. Build/Test Evidence (actual, after changes)
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS**
- `./build-ubuntu/tests/phantomx_unit_tests` — **121 passed, 0 failed, 121 total**
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS** (0.55s)
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS**

## 3. Concurrency Evidence

### 3.1 Per-identity quota (`put_verifier_challenge_with_quota`, production DB)
- Harness: `tests/harness/challenge_quota_race.cpp` (compiled against `build-ubuntu/libphantomx_common.a`, uses production `ChainDatabase::open/put_verifier_challenge_with_quota/count_active...`).
- Setup: initial=7 active challenges, same identity (`v1`,`o1`), 32 concurrent threads, each independent DB connection, barrier release.
- Acceptance checked by harness: exactly 1 ok, 31 reject, active identity <=8, global==initial+1, no err.
- Executed: **20 iterations, pass=20, fail=0**.
- Required: **>=100 iterations**. **NOT VERIFIED to required scale**.

### 3.2 Global quota
- Setup: initial=4095 active challenges, 32 concurrent threads, distinct identities, barrier release.
- Acceptance: exactly 1 ok, 31 reject, global active <=4096, no err.
- Executed: **10 iterations, pass=10, fail=0**.
- Required: **>=100 iterations where feasible**. **NOT VERIFIED to required scale**.

### 3.3 Cleanup/create/consume/count race
- **NOT IMPLEMENTED**. No harness covering concurrent cleanup + create + consume + active quota counting with independent connections/processes.

## 4. SQLite Failure Evidence
- SQLITE_BUSY/LOCKED/insert failure/cleanup failure/transaction rollback/read-only/unavailable/corruption/disk-full/commit-failure injection: **NOT IMPLEMENTED / NOT VERIFIED**.
- Existing ad-hoc separate-process consume races: 100x2 exactly_one=100 anomalies=0; 20x4 exactly_one=20 anomalies=0 (Phase 15C.9 evidence, harness committed). Crash recovery not proven.

## 5. Sanitizers
- ASAN+UBSAN: **NOT RUN** (not configured/executed in this phase; no sanitizer build logs). Status: NOT VERIFIED.
- TSan: **NOT RUN**. Status: NOT VERIFIED.

## 6. PoP Validation Matrix (production path)
| Case group | Status | Evidence |
|---|---:|---|
| Roundtrip/selected decode rejects | TESTED/PARTIAL | `ptm015c7_pop_v1_production_roundtrip_and_decode_rejects` |
| Selected signature field mutations | TESTED/PARTIAL | `ptm015c7_pop_v1_signature_field_mutations_production` |
| Full invalid field length/overflow/empty/oversized/max-valid/timestamps/future-issued/wrong key/sig/per-field/decode-context-signature classification | PARTIAL/NOT IMPLEMENTED | Required matrix incomplete |

## 7. Registration/Lifecycle Matrix
| Area | Status | Evidence |
|---|---:|---|
| Registration integration (actual handler, 27-case target) | PARTIAL | replay/lifecycle subset only; DB consume/registration failure injection, concurrent replay, full binding/conflict matrix missing |
| Lifecycle restart/fail-closed/adversarial | PARTIAL/LOCAL ONLY | PENDING/ACTIVE/SUSPENDED/REVOKED heartbeat checks; restart persistence, corrupt/missing/unknown status, unauthorized transitions, reactivation policy, DB-failure fail-closed incomplete |

## 8. Registry / PX-CONS-0001
- Registry: **BLOCKED**. `docs/security/PHANTOMX_VERIFIER_REGISTRY_PROTOCOL_PROPOSAL.md` not updated to full 20-topic decision tracker format. No consensus-authoritative implementation.
- PX-CONS-0001: **INVESTIGATING**. No direct view-change evidence (trigger/message/quorum/view increment/proposer transition/new proposal/QC/finality/node agreement). No consensus-rule changes.

## 9. Files Changed (Phase 15C.10 relevant)
- `tests/harness/challenge_quota_race.cpp` — new deterministic concurrent quota harness (production DB).
- `docs/security/PHANTOMX_PHASE15C8_SECURITY_EVIDENCE_REPORT.md` — created.
- `docs/security/PHANTOMX_PHASE15C9_SECURITY_EVIDENCE_REPORT.md` — created.
- `docs/security/PHANTOMX_PHASE15C10_SECURITY_EVIDENCE_REPORT.md` — created.
- Prior retained: `src/blockchain/chain.cpp`, `include/phantomx/blockchain/chain.hpp`, `src/rpc/node_handlers.cpp`, `include/phantomx/poi/verifier_pop.hpp`, `tests/unit/test_poi.cpp`, `tests/harness/consume_challenge_race.*`, `docs/security/PHANTOMX_PHASE15C7_SECURITY_EVIDENCE_REPORT.md`.

## 10. Remaining Blockers
- Per-identity and global quota race proof not at required 100-iteration scale.
- Cleanup/create/consume/count race harness missing.
- SQLite failure injection matrix missing.
- ASAN/UBSAN/TSan not run.
- PoP full production matrix incomplete.
- Registration 27-case integration matrix incomplete.
- Lifecycle restart/fail-closed/adversarial matrix incomplete.
- Registry decision tracker incomplete; consensus-authoritative registry BLOCKED.
- PX-CONS-0001 direct view-change evidence missing.

## 11. Verdict
**Phase 15C.10: PARTIAL. Mainnet/Production testnet: NO-GO.**
