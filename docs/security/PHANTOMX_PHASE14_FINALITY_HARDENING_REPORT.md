# PhantomX Phase 14 — Finality & Authorization Hardening Report

> Status: **PARTIAL / INVESTIGATING**  
> Date: 2026-11-26  
> Scope: safe hardening for PTM-001/002/003/004/005 and pool payout authz; isolated BFT validation evidence review.  
> No consensus/reward/monetary protocol changes. Canonical `config/testnet.json` unchanged (BFT disabled).

## Baseline (before Phase 14 edits)
- `./build-ubuntu/tests/phantomx_unit_tests`: 106/106 PASS
- `ctest --test-dir build-ubuntu --output-on-failure`: 11/11 PASS
- Worktree dirty; pre-existing modifications/untracked files preserved.

## Changes Implemented

| Area | Change | Files | Status |
|---|---|---|---|
| PTM-001 Heartbeat | `verifier_heartbeat` now updates liveness/online count only; **removed** quorum recalculation and pending-block finalization from heartbeat path. Response includes `finality_effect=none_liveness_only`. | `src/rpc/node_handlers.cpp` | IMPLEMENTED/TESTED (partial) |
| PTM-001 record path | `poi_record_block` no longer finalizes pending blocks based on attestations count; only explicit bootstrap exception when `online==0 && q_min<=1`. Finality authorized via attestation verification path. | `src/rpc/node_handlers.cpp` | IMPLEMENTED/TESTED (partial) |
| PTM-002 setpoiquorum | Requires `PHANTOMX_ADMIN_TOKEN` env configured and matching `admin_token` param; fail-closed if not configured. Enforces `q_min` range 1..256. | `src/rpc/node_handlers.cpp` | IMPLEMENTED/TESTED (partial) |
| PTM-005 stoppoi | Participant stop requires owner identity proof (same PoP identity claim path) and owner match; no global `g_poi_running=false` for participant-only stop unless no participants running. Global stop (`all=true`) requires admin token. | `src/rpc/node_handlers.cpp` | IMPLEMENTED/TESTED (compat impact) |
| PTM-003 register | Verifier registration address validation uses active `network` HRP instead of hardcoded regtest. | `src/rpc/node_handlers.cpp` | IMPLEMENTED/TESTED (partial) |
| Pool payout auth | `pool_record_payout` requires `PHANTOMX_ADMIN_TOKEN` admin_token; fail-closed. | `src/rpc/node_handlers.cpp` | IMPLEMENTED/TESTED (partial) |
| Regression tests | Added PTM-001 heartbeat liveness-only, PTM-002 admin token, PTM-005 owner-scoped stop/global admin, pool payout admin auth. | `tests/unit/test_poi.cpp` | TESTED |

## Tests Run

- `cmake --build build-ubuntu --target phantomx_unit_tests -j6` — PASS
- `./build-ubuntu/tests/phantomx_unit_tests` — **109 passed, 0 failed, 109 total**
- `ctest --test-dir build-ubuntu --output-on-failure` — **11/11 PASS** (0.57s)

## BFT Validation (PX-CONS-0001)

- Canonical `config/testnet.json`: **BFT disabled** (unchanged).
- Isolated config: `config/testnet-bft-validation.json` (test-only, separate ports/datadirs required).
- Harness: `/tmp/run_testnet_bft_validation.sh` (safe RPC parsing, DIAG preserves nodes on failure).
- Evidence: `/tmp/phx_bft_testnet_val.out`, `/tmp/phx-bft-testnet-val-015Zy9`.
- Result: **1 run PASS** - `stable=3`, `H1 ok`, node 1 stopped, `vc_seen=0` (harness check insufficient), restarted, `H2 ok`, all 4 nodes finalized height 2 with same `bft_qc` hash `e33a8c7d2aeee1ac1564d2a849e6a0791e5974bbe2485ef4f52c2e31c790f33d`.
- **Not closed**: no direct view-change protocol evidence captured; only 1 run; isolated config only, not canonical testnet. Status: **INVESTIGATING**.

## Findings Status

| ID | Severity | Status | Notes |
|---|---:|---|---|
| PTM-001 | Critical | **PARTIAL** | Heartbeat can no longer finalize/recalc quorum; still lacks verifier cryptographic heartbeat auth/freshness/replay protection and does not prove liveness. |
| PTM-002 | High | **PARTIAL** | Admin token gate + bounds added; not a full capability/RBAC model, no audit log, no persistence-semantics hardening beyond existing save. |
| PTM-003 | High | **PARTIAL** | Network HRP fixed; no verifier registration PoP/challenge/replay/conflict protocol. |
| PTM-004 | High | **INVESTIGATING** | Attestation freshness/epoch/revocation/replay/conflict/durable dedup not fully hardened. |
| PTM-005 | High | **PARTIAL** | Participant stop requires identity proof; global stop admin-gated. Compatibility impact: remote stop now requires identity fields; local/admin workflows need review. |
| POOL-AUTH | High | **PARTIAL** | `pool_record_payout` admin-gated; `submitpoievidence` submitter/worker ownership binding, dedup/replay/epoch/rate limits still incomplete. |
| PX-CONS-0001 | Critical | **INVESTIGATING** | Isolated BFT validation pass, but vc evidence insufficient; canonical testnet BFT disabled. |

## Compatibility / Rollback

- `stoppoi` behavior changed: remote participant stop requires identity proof; global stop requires `PHANTOMX_ADMIN_TOKEN` + `admin_token`. May break existing scripts/ops.
- `setpoiquorum` now fails closed unless `PHANTOMX_ADMIN_TOKEN` configured; ops must set admin token.
- `pool_record_payout` now requires admin token.
- Rollback: revert specific handler checks in `src/rpc/node_handlers.cpp`; preserve tests as expected-behavior update only if rollback accepted.

## Remaining Risks

- No verifier key PoP/eligibility/revocation/independent measurement proof.
- Attestation replay/freshness/epoch binding/revocation/durable dedup incomplete.
- Heartbeat still unauthenticated as verifier liveness proof (but cannot finalize).
- Admin token is shared-secret capability, not full RBAC/audit/multi-operator model.
- Pool evidence submission unauthenticated to worker ownership; payout destination ownership not proven.
- PX-CONS-0001 not closed with direct VC evidence.
- No formal proof, independent audit, long adversarial soak, production VRF, on-chain slashing economics.

## Readiness

- **Mainnet: NO-GO**
- **Production testnet: NO-GO**
- **Experimental/regtest: PARTIAL** (core tests pass; hardened paths require admin token/config updates)
- **Formal verification: NOT STARTED/NO-GO**
- **Independent audit: NOT STARTED/NO-GO**
