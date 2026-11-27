# PhantomX Verifier Protocol

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Status
Foundation draft for Prompt 01. Implementation is partial in `src/verifier/`, `src/poi/`, and RPC handlers. This document defines the target protocol; gaps are listed in Known Gaps.

## Purpose
Verifiers independently validate PoI evidence, transaction/block data as assigned, and produce signed attestations. Aggregators collect attestations into verifiable certificates; verifiers/PoI+Verifier finalize blocks. Verifiers do **not** finalize blocks.

## Roles and Identity Separation
- **Contributor/Participant**: produces PoI evidence.
- **Verifier**: verifies evidence/assignments; signs attestation.
- **Aggregator**: collects verifier attestations; builds certificate. Must not create verification result itself.
- **Verifier**: participates in PoI+Verifier consensus/finality.
- **Pool Operator**: manages workers/accounting; no consensus authority.

Identities must be distinct keys/IDs. A verifier must not verify evidence owned by its own identity or conflicting operator identity.

## Verifier Registration
Required fields (target):
- `verifier_pubkey` (Ed25519, role-separated)
- `operator_pubkey`
- `verifier_type` (`Poi` only in current no-storage scope)
- endpoint/capability metadata
- registration timestamp
- stake/bond/status (policy-defined)

Current code: `verifier::VerifierRegistry` stores verifier/operator pubkeys, type, status, endpoint, reputation, job/reward counters. Registration is local registry-based; production needs signed registration, anti-Sybil, and governance/policy controls.

## Eligibility
Verifier eligible when:
- registered and active/not banned/suspended
- type matches assignment
- reputation above policy threshold
- no conflict of interest (self, same operator as subject, pool/operator conflict as policy defines)
- meets liveness/response requirements

Current code: `VerifierRegistry::eligible_for` checks active, type match, reputation > -100.

## Assignment
Assignment must be deterministic and auditable:
- input: epoch/assignment seed, subject/evidence hash, eligible verifier set, policy params
- output: selected verifier IDs, assignment ID, expiry, quorum threshold
- minimum verifiers per assignment >= 2 (policy)
- randomization must be unbiased/auditable in production; foundation may use deterministic hash ranking from auditable seed

Current code: `verifier/assignment.*` provides deterministic ranking/assignment foundation; production randomness hardening required.

## Verification Flow
1. **Challenge/Assignment creation**: include network ID, epoch, assignment ID, subject ID, nonce, expiry, payload hash.
2. **Delivery**: authenticated P2P/runtime delivery with retries/receipts (foundation currently stub/limited).
3. **Evidence verification**: check subject match, expiry/freshness, nonce replay, signature, format, simulator flag, policy bounds.
4. **Attestation**: verifier signs result with domain separation, e.g. `PHX/verifier/attestation/v1`.
5. **Collection**: aggregator collects valid independent attestations.
6. **Conflict handling**: conflicting results recorded; quorum/penalty policy applied.

## Attestation Format (target)
- `network_id`
- `epoch_id`
- `assignment_id`
- `subject_id`
- `evidence_hash`
- `verifier_pubkey`
- `result` (pass/fail/score/metrics as applicable)
- `timestamp`
- `expires_at`
- `nonce` / `assignment_nonce`
- `signature`

All consensus-relevant fields must be canonically serialized and domain-separated before signing.

## Anti-Abuse Rules
- No self-verification.
- No pool as sole verifier.
- Duplicate verifier identity/signature in same assignment rejected.
- Replay nonce/evidence rejected.
- Expired assignment/evidence rejected.
- Simulator evidence (`simulator=true`) must never be accepted as mainnet/consensus-valid PoI proof.
- Conflicting attestations require explicit dispute/quorum policy.

## Reputation and Rewards
- Reputation updated on completed/failed jobs, disputes, slashing evidence.
- Verifier reward only from valid assignments/attestations per reward policy.
- Duplicate payout prevented by settlement accounting.

## Known Gaps
- Signed verifier registration/heartbeat and anti-spoof hardening incomplete.
- Authenticated challenge delivery/retry/receipt not complete.
- Dispute resolution/quorum/slashing not fully integrated with consensus/governance.
- Verifier liveness probing is registry-driven.
- No complete aggregator certificate flow yet (see `AGGREGATOR_PROTOCOL.md`).
