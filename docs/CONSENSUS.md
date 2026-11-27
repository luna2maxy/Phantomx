# PhantomX Consensus

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Overview

PhantomX uses a hybrid consensus model:

- **PoI (Proof of Internet)** determines contribution scoring and verifier eligibility inputs.
- **PoI+Verifier (Byzantine Fault Tolerant) finality** provides deterministic finality for blocks.

PoI is **not** Sybil-resistant by itself. Storage contribution does **not** automatically grant voting power. Finalized blocks must not be replaced by a chain that only has a higher PoI score.

## Formal Model

### Verifier Membership

- The active verifier set is defined per epoch as `VS_e = {v1, ..., vn}`.
- Each verifier has: `validator_pubkey`, `operator_pubkey`, `voting_power`, `active`.
- `total_voting_power(VS_e) = Σ voting_power(vi)` for active verifiers.
- Verifier set changes are applied at epoch rotation boundaries and recorded by policy.

### Verifier Eligibility

A verifier is eligible only if all hold:

1. Identity is verified (node/operator identity rules, anti-Sybil signals evaluated).
2. Not slashed/suspended by consensus policy.
3. PoI score >= `min_poi_score` (fixed-point `ScoreMilli`).
4. Passes conflict-of-interest and policy checks.

Storage capacity/contribution may affect storage-market accounting but **MUST NOT** be used as PoI+Verifier voting power.

### Voting Power

- Voting power is integer, policy-assigned, and auditable.
- Foundation implementation computes power from PoI score: `power = clamp(poi_score, policy caps)`.
- Operator cap: total voting power attributable to one `operator_pubkey` may be capped by `max_operator_voting_power`.
- Verifier cap: `max_validator_voting_power` limits single-verifier dominance.
- Voting power is recomputed/confirmed at epoch boundaries; mid-epoch changes require explicit governance rule (not default).

### Proposer Selection

- Proposer is selected deterministically from `VS_e` for `(epoch, view)` using auditable randomness/seed hashed with verifier-set ordering.
- Selection is weighted by voting power among active verifiers.
- Production randomness must be unbiased and committed before use; foundation uses deterministic hash ranking for auditability.

### Block Proposal

A proposal `P` contains:

- `epoch, view, height, block_hash, parent_hash, proposer_pubkey, timestamp, signature`.

Validation:

1. Proposer is the selected proposer for `(epoch, view)`.
2. Signature is valid for proposer key over domain-separated proposal hash.
3. Parent is known and compatible with local finalized/locked state.
4. Block passes stateless/stateful validation rules.

### Vote

Two-phase PoI+Verifier voting:

- **Prevote**: verifier signals proposal is acceptable for current view.
- **Precommit**: verifier signals readiness to finalize proposal for current view.

Vote fields: `phase, epoch, view, height, block_hash, validator_pubkey, voting_power, signature`.

Vote is valid if:

1. Verifier is active in `VS_e`.
2. Voting power matches verifier record.
3. Signature verifies over `PHX/poi+verifier/vote/v1` domain-separated hash.
4. Epoch/view match local state.

### Quorum

Quorum requires strictly more than 2/3 of total voting power:

- `signed_power * 3 > total_voting_power * 2`

Equivalent integer threshold: `floor(2*total/3) + 1`.

### Quorum Certificate (QC)

A QC is a set of valid, unique, same-phase/same-epoch/same-view/same-height/same-block votes whose summed voting power satisfies quorum.

- **Prevote QC**: quorum of prevotes.
- **Precommit QC**: quorum of precommits.

QC verification rejects duplicates, invalid signatures, unknown/inactive verifiers, mismatched vote fields, or insufficient power.

### Finality Certificate

A finality certificate contains a valid **Precommit QC** for `(height, block_hash)`.

A block is finalized when a valid finality certificate is observed. Finalized state is durable and must be persisted before acknowledging finality externally.

### Timeout

- Each view has a timeout. On timeout, verifiers move to `view+1`.
- Timeout/view change must not finalize conflicting blocks.
- New view clears in-memory pending votes for old view (foundation simplification); production should retain equivocation evidence.

### View Change

- Triggered by timeout, invalid proposal, or failure to form QC.
- Proposer for new view is selected deterministically by `(epoch, view+1)`.
- Safety: verifiers do not precommit conflicting blocks that violate lock/finality rules.

### Equivocation

Equivocation is any verifier signing two different votes for same `(verifier, phase, epoch, view)`.

- Detected when conflicting vote hashes/blocks are seen.
- Evidence must be stored: verifier, phase, epoch, view, hash_a, hash_b, signatures.
- Policy response: reject conflicting vote, slash/suspend by governance/consensus rules.

### Verifier Rotation

- Occurs at epoch boundary with eligibility/voting power recomputation.
- Rotation must include identity/anti-Sybil checks and operator caps.
- Old votes do not carry into new epoch unless explicitly defined.

### Recovery

- On restart, node reloads finalized certificate, verifier set, epoch/view/lock state, and equivocation evidence.
- Node must not finalize below or conflicting with persisted finality.
- If local state is behind, node syncs blocks and QCs before participating in consensus.

## Safety and Liveness Assumptions

- **Safety**: assumes Byzantine voting power < 1/3 of total voting power and correct quorum verification.
- **Liveness**: assumes partial synchrony / eventual message delivery and >2/3 honest active voting power available.
- **Network partition**: finality may stall during partition; conflicting finalized blocks must not occur.

## Foundation Limitations

- This is a consensus foundation, not a complete production PoI+Verifier implementation.
- No full pacemaker, leader failure aggregation, block proposal gossip, or persisted vote store yet.
- Timeout/view-change is simplified.
- Slashing/governance enforcement is policy-level, not fully automated on-chain here.
- Randomness/proposer selection must be hardened with unbiased on-chain randomness in production.
