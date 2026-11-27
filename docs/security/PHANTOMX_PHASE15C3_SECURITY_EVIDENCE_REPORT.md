# PHANTOMX Phase 15C.3 Security Evidence Report

**Status:** PARTIAL / INVESTIGATING  
**Date:** 2026-04-30  
**Scope:** Verifier PoP evidence, SQLite atomicity evidence, verifier registry architecture, lifecycle adversarial gaps.

## Baseline (reproduced)

- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — **PASS**
- `./build-ubuntu/tests/phantomx_unit_tests` — **114 passed, 0 failed, 114 total** (before 15C.3 test additions)
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS**
- `cmake --build build-ubuntu --target phantomxd -j6` — **PASS**

## Task Status

| Task | Status | Notes |
|---|---:|---|
| 1. Verifier registry architecture | **IMPLEMENTED (doc) / BLOCKED (decision)** | Current registry is local-only chain metadata; no P2P replication/consensus authority. Architecture doc created with A/B/C comparison and recommendation A. Implementation blocked on governance/protocol decisions. |
| 2. Binary PoP deterministic test vectors | **PARTIAL / TESTED** | Added `tests/vectors/poi_verifier_pop_v1_vectors.json` (1 deterministic vector, fixed seed/key, expected payload/signature). Added unit test loading vector and verifying signature. Missing: full vector set, decoder expected-result fields beyond verify, negative vectors in file. |
| 3. Field mutation tests | **PARTIAL / TESTED** | Added basic mutations (version, network, trailing byte) asserting signature invalidation. Missing full per-field mutation suite, truncation/length overflow/boundary/unknown op/version/encoding cases, exact-byte sign assertions for all cases. |
| 4. SQLite concurrency evidence | **PARTIAL / INVESTIGATING** | Audited config: WAL, synchronous=FULL, foreign_keys=ON, busy_timeout=5000, FULLMUTEX, TransactionGuard uses BEGIN IMMEDIATE/COMMIT/ROLLBACK. Challenge consume uses conditional UPDATE in TransactionGuard and checks `sqlite3_changes==1`. **No cross-connection or cross-process tests executed.** Cannot claim cross-process atomicity. |
| 5. Challenge resource limits | **NOT IMPLEMENTED** | No max outstanding/per-verifier/per-owner limits, no rate limiting, no cleanup/retention bounds, no deterministic capacity errors. |
| 6. Verifier lifecycle adversarial tests | **PARTIAL** | Existing: PENDING cannot attest/heartbeat until activated, replay challenge rejected, admin status transitions basic. Missing unauthorized activation/suspension/revocation matrix, revoked/suspended/expired attestation RPC+P2P matrix, restart/corrupt-status/duplicate-quorum identity/conflicting key-owner tests. |
| 7. Migration/compatibility | **PARTIAL** | Legacy string PoP payload no longer accepted by new binary decoder path; clients sending old `payload` break. No migration doc/policy; no mixed-version/rollback downgrade analysis completed. |
| 8. Build/test | **TESTED** | After 15C.3 additions: `cmake --build build-ubuntu --target phantomx_unit_tests -j6` PASS; `./build-ubuntu/tests/phantomx_unit_tests` **115 passed, 0 failed, 115 total**; `ctest --test-dir build-ubuntu --output-on-failure` **11/11 PASS**; `cmake --build build-ubuntu --target phantomxd -j6` PASS. |
| 9. Report | **IMPLEMENTED (this report)** | Evidence and blockers documented honestly. |

## SQLite Observed Configuration

From `src/database/sqlite_db.cpp`:
- Open: `SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX`
- Pragmas: `journal_mode=WAL`, `synchronous=FULL`, `foreign_keys=ON`, `temp_store=MEMORY`, `busy_timeout=5000`
- TransactionGuard: `BEGIN IMMEDIATE TRANSACTION`; commit/rollback; destructor rollback if active/uncommitted.
- Challenge consume: `UPDATE verifier_challenges SET used_at=?, status='used' WHERE ... used_at IS NULL AND expires_at >= ?`; success only when `sqlite3_changes(db)==1`; wrapped in TransactionGuard.

**Evidence gap:** No independent connection/process test proving exactly-one-consumer under concurrent processes. FULLMUTEX protects within process/libsqlite instance; cross-process behavior depends on SQLite file locking/WAL semantics and was not tested.

## Verifier Registry Consistency

- Current: **local-only metadata** (`verifier_registry_json`). **NOT IMPLEMENTED** as consensus-authoritative or replicated.
- Risk: nodes can diverge on ACTIVE/SUSPENDED/REVOKED status, affecting attestation/quorum/finality decisions.
- Recommendation: Consensus-authoritative registry for finality-affecting eligibility (Architecture A). Implementation **BLOCKED** pending governance/protocol decision.

## Migration / Compatibility

- **Breaking:** `verifier_register_challenge` now returns `payload_hex` (binary PoP v1), not legacy string `payload`. Clients must sign `payload_hex` bytes.
- **Legacy acceptance:** old string payload format is not accepted by v1 decoder; no downgrade path implemented. This is safer but requires explicit migration communication.
- **Rollback risk:** rolling back to pre-v1 code after v1 challenges issued may create incompatibility; needs formal migration plan. **BLOCKED** for production decision.

## Remaining Blockers (NO-GO)

- Verifier registry not consensus-authoritative/replicated; local admin can diverge nodes.
- Cross-process SQLite atomicity not verified by tests.
- Challenge limits/rate limiting/cleanup not implemented.
- Full PoP vector/mutation/adversarial lifecycle/restart matrix incomplete.
- No formal verification, no independent security audit, no long adversarial soak, no production VRF/unbiased randomness, no on-chain slashing/economic settlement.
- PX-CONS-0001 BFT view-change evidence still INVESTIGATING.

## GO/NO-GO

- Mainnet: **NO-GO**
- Production testnet: **NO-GO**
- Phase 15C.3 complete: **NO**
