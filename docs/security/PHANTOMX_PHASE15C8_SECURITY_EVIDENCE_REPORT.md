# PHANTOMX Phase 15C.8 Security Evidence Report

**Status:** PARTIAL  
**Date:** 2026-04-30 (workspace execution)  
**Scope:** Close 15C.7 deliverables, audit/fix challenge quota atomicity, initial separate-process consume evidence, production PoP refactor/tests start.

## Preservation
- Dirty worktree preserved; no reset/clean/revert.
- Canonical `config/testnet.json`, genesis, network ID, monetary/consensus/reward/block format unchanged.
- No P2P registration. No consensus-authoritative registry implementation.

## Baseline
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — PASS
- `./build-ubuntu/tests/phantomx_unit_tests` — 121 passed, 0 failed, 121 total (after incremental tests in same tree)
- `ctest --test-dir build-ubuntu --output-on-failure` — 11/11 PASS
- `cmake --build build-ubuntu --target phantomxd -j6` — PASS

## Atomic quota implementation
- Added `ChainDatabase::put_verifier_challenge_with_quota(...)` (`src/blockchain/chain.cpp`, `include/phantomx/blockchain/chain.hpp`).
- Transaction boundary (`BEGIN IMMEDIATE` via `TransactionGuard`): bounded cleanup, global active count, per-identity active count (`verifier_id OR owner` policy preserved), quota checks, insert, commit; rollback on failure.
- `verifier_register_challenge` in `src/rpc/node_handlers.cpp` uses atomic path; quota failures fail closed.

## Separate-process consume harness
- Harness committed: `tests/harness/consume_challenge_race.cpp`, `tests/harness/run_consume_challenge_race.sh`.
- Production path used: `ChainDatabase::consume_verifier_challenge`.
- Result: 100 iterations, 2 processes — **exactly_one=100, anomalies=0**. 20 iterations, 4 processes — **exactly_one=20, anomalies=0**.
- Limitations: no crash-during-tx injection, no busy/locked fault injection, no corruption recovery, no long soak; crash recovery NOT VERIFIED.

## PoP/lifecycle evidence
- Shared production PoP v1 header: `include/phantomx/poi/verifier_pop.hpp`; RPC uses shared encoder/decoder.
- Tests: roundtrip/decode rejects, selected signature field mutations, challenge boundary/cleanup, local lifecycle PENDING/ACTIVE/SUSPENDED/REVOKED heartbeat.
- Full required PoP decoder/signature/context, RPC 27-case integration, lifecycle adversarial/restart/fail-closed matrices remain PARTIAL/NOT IMPLEMENTED.

## Files changed (relevant)
- `include/phantomx/blockchain/chain.hpp`, `src/blockchain/chain.cpp`
- `src/rpc/node_handlers.cpp`, `include/phantomx/poi/verifier_pop.hpp`
- `tests/unit/test_poi.cpp`, `tests/harness/*`
- `docs/security/PHANTOMX_PHASE15C7_SECURITY_EVIDENCE_REPORT.md`

## Missing / blockers
- Concurrent per-identity/global quota creation race tests incomplete in 15C.8; later partially exercised in 15C.10 harness (not 100-iteration full acceptance proof in this report).
- SQLite failure injection incomplete.
- PoP/RPC/lifecycle matrices incomplete.
- PX-CONS-0001 direct view-change evidence missing — INVESTIGATING.
- Registry decision tracker incomplete; consensus-authoritative registry BLOCKED.

## Verdict
**Phase 15C.8: PARTIAL. Mainnet/Production testnet: NO-GO.**
