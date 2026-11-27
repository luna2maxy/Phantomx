# PhantomX Verifier Network

## Overview

The verifier network provides independent verification for PoI and storage proofs. Verifier PoI and verifier storage are **logically separated** by `VerifierType`, while sharing a common registry/assignment/service framework.

## Features

| Feature | Implementation | Notes |
|---|---|---|
| 1. Verifier registration | `VerifierRegistry::register_verifier` | Stores verifier pubkey, operator pubkey, type, endpoint, status |
| 2. Verifier eligibility | `VerifierRegistry::eligible_for` | Active, type match, reputation threshold |
| 3. Verifier identity | `VerifierRecord.verifier_pubkey` + operator key | Operator identity separated from verifier identity |
| 4. Random assignment | `assign_verifiers` | Deterministic ranking from auditable randomness seed + epoch + verifier pubkey hash |
| 5. Challenge creation | `VerifierService::create_challenge` | Challenge id, subject, verifier, expiry, payload |
| 6. Challenge delivery | `VerifierService::deliver_challenge` | Foundation delivery flag; real delivery via P2P/runtime |
| 7. Evidence verification | `VerifierService::verify_and_sign` | Checks subject match, expiry, non-simulator, verifier match |
| 8. Signed result | `sign_result` / `verify_result_signature` | Domain-separated hash `PHX/verifier/result/v1` signed by verifier key |
| 9. Dispute handling | `open_dispute` / `resolve_dispute` | Foundation dispute records and status transitions |
| 10. Verifier reputation | `update_reputation` | Delta-based reputation; jobs completed/failed counters |
| 11. Verifier reward accounting | `account_reward` | Accounts reward atoms per verifier |

## Independence Rules

- **No self-verification:** assignment and service reject verifier == subject.
- **Pool not sole verifier:** pool operator conflict excluded; assignment requires multi-verifier (`>=2`).
- **Multi-verifier:** `assign_verifiers` enforces minimum 2 verifiers.
- **Auditable assignment:** assignment hash computed from request + selected verifiers; deterministic from seed/epoch.
- **Signed results:** every verifier result is signed and verifiable.
- **Conflict-of-interest detection:** `check_conflict` rejects same subject, same operator as subject, same pool operator.

## PoI vs Storage Separation

- `VerifierType::kPoi` and `VerifierType::kStorage` are separate eligibility domains.
- `VerifierType::kBoth` can serve both only when explicitly registered as such.
- Reward accounting, reputation, and disputes are tracked in shared framework but should be reported by type in production.

## Tests

`tests/verifier/test_verifier.cpp` covers:
- verifier offline / ineligible
- malicious self-verification rejected
- replay nonce rejected
- collusion/conflict-of-interest excluded from assignment
- invalid/simulator evidence rejected
- signed result signature verification

## Limitations

- Challenge delivery is a foundation stub; production requires authenticated P2P delivery, retries, and receipts.
- Dispute resolution is record-keeping only; arbitration/quorum/slashing requires governance/consensus integration.
- Reputation is simple delta accounting; production needs weighted scoring, decay, and Sybil/collusion resistance.
- Assignment randomness must come from unbiased on-chain/committee randomness in production.
- No network-level verifier liveness probing yet; offline detection is registry-driven.