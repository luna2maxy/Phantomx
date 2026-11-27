# PhantomX Anti-Sybil

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Scope

This module provides identity and anti-Sybil foundation for PoI. It is **not** a claim of perfect Sybil resistance. PoI alone is not Sybil-resistant; final Sybil resistance depends on identity policy, verifier/PoI+Verifier rules, economic costs, evidence verification, and operational monitoring.

## Core Rules

- **IP is not sole identity.** Endpoint/network prefix is only a diversity signal.
- **Operator identity is separate from wallet address.** Node registration binds node key, operator key, and wallet address as distinct fields.
- **No self-reported final proof.** Evidence must have independent verifier attestations.
- **Evidence freshness required.** Stale evidence/identity is rejected by policy.

## Implemented Features

| Feature | Implementation | Notes |
|---|---|---|
| Node registration | `AntiSybilRegistry::register_identity` | Verifies signed identity, freshness, duplicate, operator cap |
| Identity key | `NodeIdentity.node_pubkey` (Ed25519) | Node key signs identity |
| Signed identity | `sign_identity` / `verify_identity` | Domain-separated hash `PHX/poi/identity/v1` |
| Challenge nonce | `registration_nonce`, PoI challenge nonce registry | Replay prevention |
| Identity rotation | `IdentityRotation`, `sign_rotation`, `verify_rotation`, `rotate_identity` | Requires old+new key signatures and new signed identity |
| Duplicate detection | Registry duplicate node pubkey rejection | Also wallet/operator/endpoint signals |
| Suspicious activity detection | `AntiSybilAssessment.signals`/risk score | Heuristic, not perfect |
| Network diversity signals | Endpoint prefix counts | Signal only, not identity |
| Collusion analysis | `detect_collusion_signals` | Operator cluster, endpoint cluster, wallet reuse heuristics |
| Contribution caps | `max_nodes_per_operator`, `max_contribution_per_operator/node` | Policy-configurable caps |
| Evidence freshness | `identity_fresh`, `evidence_fresh` | Timestamp/age checks |

## Threat Model

| Threat | Foundation handling | Residual risk |
|---|---|---|
| VPS farms | Operator node cap, endpoint diversity signal, contribution caps | Operator can distribute across operators/ASNs/endpoints |
| Many identities one operator | Operator pubkey cap, collusion operator cluster signal | Fake/operator key splitting possible |
| Fake bandwidth | Verifier-measured challenge-response, independent attestations, no self-report final | Colluding verifiers or compromised measurement path |
| Fake uptime | Signed heartbeat/evidence windows, independent attestations, freshness | Collusion or forged verifier keys |
| Replay | Nonce registry, challenge nonce, evidence hash binding | Poor nonce TTL/persistence outside scope |
| Verifier collusion | Independent attestation requirement, collusion signals, diversity policy needed | Requires quorum/governance/slashing layer |
| Pool collusion | Operator/pool separation, anti-self-verify policy required at consensus/runtime | Economic collusion remains possible |
| Sybil networks | Heuristics + caps + attestations + freshness | No perfect Sybil resistance |
| Compromised identity | Identity rotation with old+new signatures, expiry/freshness | Compromise before rotation, key theft |

## Limitations

- Heuristic signals can produce false positives/negatives.
- Endpoint prefix is a weak signal and must not be used as sole identity.
- Collusion detection requires off-chain/runtime telemetry and governance policy for enforcement.
- No hardware attestation, no personhood proof, and no perfect uniqueness guarantee.
- Contribution caps and thresholds are policy parameters and must be versioned/audited before consensus use.

## Files

- `include/phantomx/poi/identity/identity.hpp`
- `src/poi/identity/identity.cpp`
- `include/phantomx/poi/anti_sybil/anti_sybil.hpp`
- `src/poi/anti_sybil/anti_sybil.cpp`
- `tests/poi/anti_sybil/test_anti_sybil.cpp`