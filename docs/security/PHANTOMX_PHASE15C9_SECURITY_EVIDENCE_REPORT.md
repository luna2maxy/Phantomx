# PHANTOMX Phase 15C.9 Security Evidence Report

**Status:** PARTIAL  
**Date:** 2026-04-30 (workspace execution)

## Summary
Phase 15C.9 closed the report gap for 15C.7/15C.8 and committed a reproducible cross-process consume harness. Critical concurrency/failure-injection/matrix/sanitizer acceptance criteria remain incomplete.

## Build/Test Evidence (actual)
- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — PASS
- `./build-ubuntu/tests/phantomx_unit_tests` — 121 passed, 0 failed, 121 total
- `ctest --test-dir build-ubuntu --output-on-failure` — 11/11 PASS
- `cmake --build build-ubuntu --target phantomxd -j6` — PASS

## Concurrency Evidence
- Per-identity concurrent quota creation (32 req, 7 active, 100 iters): **NOT IMPLEMENTED** in 15C.9.
- Global concurrent quota creation (4095 active, 32 req, 100 iters): **NOT IMPLEMENTED** in 15C.9.
- Cleanup/create/consume/count race: **NOT IMPLEMENTED**.
- Cross-process concurrent challenge creation: **NOT IMPLEMENTED**.

## SQLite Cross-Process Evidence
- Committed harness: `tests/harness/consume_challenge_race.cpp`, `tests/harness/run_consume_challenge_race.sh`.
- 2 processes, 100 iterations: exactly_one=100, anomalies=0 — TESTED.
- 4 processes, 20 iterations: exactly_one=20, anomalies=0 — TESTED.
- Crash-before/during tx, restart-after-fail/commit, busy/locked injection, corruption recovery: NOT VERIFIED.

## PoP / RPC / Lifecycle
- PoP production decoder/signature/context matrices: PARTIAL.
- Registration RPC 27-case integration matrix: PARTIAL/NOT IMPLEMENTED.
- Lifecycle adversarial/restart/fail-closed matrix: PARTIAL/LOCAL ONLY.

## Sanitizers
- ASAN/UBSAN: NOT RUN.
- TSan: NOT RUN.

## Registry / BFT
- Verifier registry consensus-authoritative: BLOCKED (protocol approval required). Decision tracker incomplete.
- PX-CONS-0001 direct view-change evidence: INVESTIGATING. No direct VC evidence captured.

## Files Changed
- `docs/security/PHANTOMX_PHASE15C8_SECURITY_EVIDENCE_REPORT.md`
- `tests/harness/consume_challenge_race.cpp`
- `tests/harness/run_consume_challenge_race.sh`
- prior 15C.8 code/test changes as recorded.

## Verdict
**Phase 15C.9: PARTIAL. Mainnet/Production testnet: NO-GO.**
