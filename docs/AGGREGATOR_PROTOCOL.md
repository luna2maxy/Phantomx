# PhantomX Aggregator Protocol

## Status
Target protocol draft for Prompt 01. **Not fully implemented** as a dedicated module; current code has verifier/pool foundations but no complete `src/aggregator` certificate pipeline. This document is the implementation target.

## Purpose
Aggregators collect valid signed verifier attestations, detect duplicates/conflicts, evaluate quorum, and produce a **Verification Certificate** that any full node can verify without trusting the aggregator. Aggregators never finalize blocks and never replace verifiers.

## Identity
- `aggregator_pubkey` (role-separated signing key)
- `operator_pubkey`
- registration/eligibility status
- reputation and failure counters

Aggregator must not be the same identity as the subject being verified for that assignment, and must not create its own verification result.

## Assignment
Deterministic/auditable assignment:
- inputs: network/epoch, assignment/evidence hash, eligible aggregator set, seed
- outputs: selected aggregator(s), backup/failover, expiry
- assignment ID included in all attestations/certificates
- rotation on failure/offline

## Attestation Collection
Aggregator accepts attestation only if:
- signature valid over canonical domain-separated payload
- verifier is eligible for assignment
- assignment ID / subject / evidence hash match
- nonce/timestamp/freshness valid
- not duplicate from same verifier identity
- not conflicting without being recorded

Conflicting results:
- record all signed attestations
- mark certificate as conflicting/insufficient as policy defines
- do not silently pick first result

## Quorum
Quorum is based on independent verifier identities, not raw signature count.
- threshold configured by policy (e.g., `t-of-n`)
- verifier set must be unique
- quorum calculation deterministic
- insufficient quorum => certificate rejected/not final for reward eligibility

## Verification Certificate (target format)
- `version`
- `network_id`
- `epoch_id`
- `assignment_id`
- `subject_id`
- `subject_type` (PoI evidence / tx / block data / other policy-defined)
- `evidence_hash`
- `verifier_set` (ordered verifier pubkeys/IDs)
- `attestation_hashes` (ordered)
- `attestations` or commitment+proofs (policy-defined)
- `result`
- `quorum` (required, achieved, policy id)
- `issued_at`, `expires_at`
- `aggregator_pubkey`
- `aggregator_signature`

Canonical serialization required. Full node verification must check all verifier signatures, quorum, uniqueness, assignment binding, and aggregator signature.

## Signature Scheme
If BLS aggregate signatures are used in future, specify: domain separation, key validation, rogue-key protection, proof of possession, and fallback/compat policy. Until then, individual Ed25519 verifier attestations + aggregator signature over certificate is the safer foundation.

## Failure Recovery
- aggregator timeout/failure => backup aggregator selected
- partial attestation set persisted/auditable
- certificate issuance idempotent for same assignment/result

## Security Rules
- Aggregator cannot finalize block.
- Aggregator cannot alter verifier attestations.
- Certificate invalid if any included attestation invalid (unless policy explicitly supports partial quorum proof with valid subset; must be specified).
- Full nodes verify certificates locally; do not trust aggregator RPC response.

## Known Gaps
- No dedicated `src/aggregator` module yet.
- No certificate builder/verifier implementation yet.
- No on-chain/RPC certificate submission/verification path fully wired.
- No dispute/slashing/governance integration.
