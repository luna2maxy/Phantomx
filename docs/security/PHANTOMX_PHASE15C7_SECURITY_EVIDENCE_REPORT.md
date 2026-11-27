# PHANTOMX Phase 15C.7 Security Evidence Report

**Status:** PARTIAL  
**Date:** 2026-04-30 (workspace execution)  
**Scope:** Production-path PoP v1 tests, challenge abuse/boundary/cleanup tests, lifecycle local tests, shared PoP refactor.  
**Not in scope / not completed:** separate-process SQLite atomicity evidence, full PoP/context/RPC/lifecycle matrices, registry consensus-authoritative implementation, PX-CONS-0001 direct VC evidence.

## Preservation
- Dirty worktree preserved; no reset/clean/revert.
- Canonical `config/testnet.json` unchanged.
- Genesis/network ID/monetary/consensus/reward/block format unchanged.
- No P2P registration. No consensus-authoritative registry implementation.

## Baseline (reproduced before 15C.7 additions)
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — PASS
- `./build-ubuntu/tests/phantomx_unit_tests` — 116 passed, 0 failed, 116 total
- `ctest --test-dir build-ubuntu --output-on-failure` — 11/11 PASS

## Implemented (Phase 15C.7)
- **Shared production PoP v1 module:** `include/phantomx/poi/verifier_pop.hpp` (`PopPayloadFieldsV1`, `pop_network_u8`, `encode_pop_payload_v1`, `decode_pop_payload_v1`). No PoP format change intended.
- **RPC refactor:** `src/rpc/node_handlers.cpp` uses shared production PoP encode/decode; local duplicate implementation removed.
- **Tests added:** `tests/unit/test_poi.cpp`
  - `ptm015c7_pop_v1_production_roundtrip_and_decode_rejects` — TESTED/PARTIAL
  - `ptm015c7_pop_v1_signature_field_mutations_production` — TESTED/PARTIAL
  - `ptm015c7_challenge_boundary_8_9_per_identity` — TESTED/PARTIAL (DB-level)
  - `ptm015c7_challenge_cleanup_expired_opens_capacity` — TESTED/PARTIAL
  - `ptm015c7_lifecycle_pending_rejected_active_allowed_local` — TESTED/PARTIAL, **LOCAL ONLY**

## Evidence (actual)
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — PASS
- `./build-ubuntu/tests/phantomx_unit_tests` — 121 passed, 0 failed, 121 total (after Phase 15C.8 incremental tests in same tree; see 15C.8 report for final attribution)
- `ctest --test-dir build-ubuntu --output-on-failure` — 11/11 PASS
- `cmake --build build-ubuntu --target phantomxd -j6` — PASS (final daemon build executed in Phase 15C.8 closure)

## Gaps / Blockers
- Separate-process SQLite consume/restart/crash evidence missing — INVESTIGATING.
- PoP decoder/signature/context matrices incomplete — PARTIAL.
- RPC registration integration matrix incomplete — PARTIAL.
- Challenge concurrent create/flood/global-cap race/DB-error fail-closed tests incomplete — PARTIAL.
- Lifecycle adversarial/restart/corruption/fail-closed matrix incomplete — PARTIAL/LOCAL ONLY.
- Registry decision tracker incomplete; consensus-authoritative registry BLOCKED.
- PX-CONS-0001 direct view-change evidence missing — INVESTIGATING.

## Decision
**Phase 15C.7 status: PARTIAL. Mainnet/Production testnet: NO-GO.**
