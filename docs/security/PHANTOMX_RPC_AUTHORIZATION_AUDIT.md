# PhantomX RPC Authorization Audit

> Status: INVESTIGATING / DESIGN+PARTIAL TESTED  
> As of: 2026-11-26  
> Scope: remotely accessible RPC methods that mutate PoI/verifier/quorum/accounting state.  

## Summary

RPC token authentication is required by the RPC server, but **token possession is not owner identity**. Several mutation endpoints allow any authenticated RPC caller to affect shared or other-owner state. PoP identity (`PHX/poi/identity/v1`) is enforced for `startpoi` and `poi_heartbeat` remote claims; unsigned remote fallback is gated by `PHANTOMX_POI_ALLOW_UNSIGNED_IDENTITY=1` (dev-only, default disabled).

## Security Matrix

| Method | Line | Remote | Auth | Owner/Key Proof | Replay/Freshness | Rate/Resource | Persistent Effect | Consensus/Eligibility/Accounting Impact | Risk | Status |
|---|---:|---:|---|---|---|---|---|---|---|---|
| `startpoi` | 2147 | yes | RPC token | Ed25519 PoP required for remote; owner-pubkey binding | ts skew 300s; no nonce/replay cache | none explicit | participant + identity bindings in poi_state_json | affects PoI participation/score inputs | Medium: PoP not Sybil-resistant; local unsigned fallback | Fixed-Foundation/Tested |
| `poi_heartbeat` | 2244 | yes | RPC token | same as startpoi | 300s skew; min interval 60s | 60s min interval | participant heartbeat/uptime counters | affects uptime/active status | Medium: self-reported uptime accounting | Fixed-Foundation/Tested |
| `stoppoi` | 2228 | yes | RPC token | **none** | none | none | marks participant not running | can deny service to participant | **High**: any token holder can stop another owner's participant | INVESTIGATING |
| `setpoiquorum` | 1489 | yes | RPC token | **none/admin not separated** | none | none | q_min/finality quorum persisted | affects PoI finality quorum policy; can weaken or disrupt finality | **High** | INVESTIGATING |
| `verifier_register` | 1212 | yes | RPC token + optional `PHANTOMX_VERIFIER_REG_TOKEN` | pubkey format only; address format checked with regtest HRP | no signed ownership proof | size limits | verifier registry persisted | affects verifier set/quorum/finality | **High**: no cryptographic key ownership proof; regtest HRP assumption | INVESTIGATING |
| `registerverifier` | 2364 | yes | RPC token | delegates to verifier_register | same | same | same | same | High | INVESTIGATING |
| `verifier_heartbeat` | 1277 | yes | RPC token + optional reg token | **none** | no freshness proof | none explicit | verifier last_heartbeat/status, quorum recalculation | affects online verifier count and quorum | **High**: impersonation/replay possible | INVESTIGATING |
| `verifier_list` | 1322 | yes | RPC token | none | N/A | none | read-only | informational | Low | Audited |
| `poi_submit_attestation` | 1342 | yes | RPC token | attestation signature checked; verifier registration checked | timestamp accepted as param; replay/duplicate handling needs audit | none explicit | block finality attestations | affects PoI block finality/quorum | **High**: needs full verifier eligibility/revocation/replay audit | INVESTIGATING |
| `submitpoievidence` | 1915 | yes | RPC token | **none** (worker_id hex parsed only) | no freshness/proof | none explicit | pool share record | affects pool accounting only, not consensus settlement | **High** for pool integrity | INVESTIGATING |
| `pool_record_payout` | 1895 | yes | RPC token | none observed | none | none | payout accounting | pool accounting; potential fraud if exposed | High | INVESTIGATING |
| `getpoiinfo/getpoiscore/getfinalityinfo/...` | various | yes | RPC token | none | N/A | none | read-only | informational | Low | Audited |

## Development Fallback: `PHANTOMX_POI_ALLOW_UNSIGNED_IDENTITY`

- Default: disabled (unsigned remote identity claims rejected).
- Enabled: allows unsigned identity claims for development compatibility.
- Risk: any RPC-authenticated caller can claim identity without PoP when enabled.
- Required: production-like startup should reject or loudly warn; keep dev-only; document migration.
- Status: behavior observed by integration checks; startup hard-fail/warning not fully enforced.

## Required Fixes (priority)

1. **stoppoi**: require owner PoP/owner-bound auth; prevent cross-owner stop; keep local admin path documented.
2. **setpoiquorum**: split local config vs consensus-sensitive; require admin capability and audit log; no silent consensus impact.
3. **verifier_register/heartbeat**: require signed verifier key ownership proof, deterministic binding, revocation/rotation model, remove regtest HRP as identity authority.
4. **poi_submit_attestation**: enforce verifier eligibility, epoch/freshness, replay cache/persistent dedup, revocation, deterministic quorum.
5. **submitpoievidence**: authenticate submitter, bind owner/machine, dedup, separate pool accounting from consensus.
6. **Global**: role/capability authorization beyond shared RPC token; audit logging for mutations.

## Evidence

- Unit: `./build-ubuntu/tests/phantomx_unit_tests` 106/106 PASS.
- CTest: 11/11 PASS.
- HTTP integration (temp scripts): unsigned remote rejected, valid PoP accepted, bad sig/skew/conflicts rejected, restart persistence, env gate PASS.
- Malformed/corrupt poi_state_json load: unit tests PASS.

## Remaining Risks

- PoP is not Sybil resistance.
- Shared RPC token is insufficient for owner authorization.
- Mutation endpoints can affect other-owner/shared state.
- Uptime/replay/pool accounting are not consensus-authoritative and lack production persistence.
