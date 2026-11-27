# PhantomX PoI+Verifier Security

> **DEPRECATED (PoI+Verifier removed):** PhantomX has moved to **PoI + Verifier attestation** consensus/finality. This document is retained for history only and must not be used to reintroduce PoI+Verifier/verifier consensus.


## Security Model

PhantomX PoI+Verifier finality is designed around a standard Byzantine fault assumption: safety holds when Byzantine-controlled voting power is less than 1/3 of total voting power, and quorum requires >2/3 voting power.

This is a foundation model; it does **not** invent a novel PoI+Verifier algorithm. It uses established 2-phase quorum principles (prevote/precommit, quorum certificate, equivocation detection) similar in safety shape to Tendermint/HotStuff-class quorum reasoning.

## Byzantine Voting Power Bound

Let `N` be total active voting power and `f` be Byzantine voting power.

- **Quorum:** `Q > 2N/3`
- **Safety requirement:** `f < N/3`
- Any two quorums intersect in honest voting power, preventing conflicting finalized blocks when rules are enforced.

If `f >= N/3`, safety can be violated. If honest active power <= 2N/3, liveness/finality can stall.

## Voting Power Distribution

- Voting power is integer and policy-assigned.
- Eligibility is based on verified identity, non-slashed status, and minimum PoI score.
- PoI is contribution/eligibility input; it is not perfect Sybil resistance.
- Storage contribution/capacity does **not** grant PoI+Verifier voting power.

## Operator/Pool Limits

- **Operator cap:** `max_operator_voting_power` caps total voting power across verifier identities controlled by the same operator key.
- **Verifier cap:** `max_validator_voting_power` limits a single verifier.
- **Pool restriction:** pool cannot multiply voting power by registering many identities; identities map to operator keys and are subject to anti-Sybil/cap policies.
- **No self-verify / conflict-of-interest:** verifier/operator/pool conflicts must be excluded before verifier eligibility or reward accounting.

## Verifier Collusion Handling

- Verifier PoI and verifier storage are logically separated.
- Multi-verifier assignments, signed results, auditable assignment, and conflict-of-interest checks reduce collusion risk.
- Collusion evidence (shared operator clusters, endpoint clusters, suspicious patterns) feeds anti-Sybil/reputation policy.
- Severe collusion should trigger suspension/slashing/governance action.

## Network Partition Safety

- During partition, a side without >2/3 active honest voting power cannot finalize.
- A finalized block requires valid precommit QC; conflicting finalized blocks are rejected.
- On healing, nodes sync missing certificates/blocks and resume from last finalized state.
- **Rule:** finalized block must never be replaced by a chain with only higher PoI score or higher non-finalized weight.

## Attack Cases and Mitigations

| Case | Mitigation | Residual Risk |
|---|---|---|
| Honest verifiers | Normal QC/finality path | Liveness depends on network/timing |
| Byzantine verifiers | >2/3 quorum, signature verification, QC checks | Unsafe if >=1/3 voting power Byzantine |
| Duplicate votes | Deduplicate by verifier+phase+epoch+view | Storage/logging overhead |
| Equivocation | Detect conflicting vote hashes, record evidence, reject | Requires slashing/governance enforcement |
| Network partition | No quorum no finality; reject conflicting finality | Temporary liveness loss |
| Delayed messages | View timeout, retry in new view | Timing tuning needed |
| Offline verifier | Quorum based on active power; liveness if >2/3 still active | Finality stalls if quorum unavailable |
| Conflicting proposals | Lock/finality rules, QC validation | Needs full proposal lock implementation in production |
| Restart | Persist finalized cert/state; reject stale/conflicting finality | Requires durable state store |
| Finality recovery | Sync certificates and blocks from peers | Peer withholding/eclipse risks require P2P hardening |

## Testing Requirements

The consensus foundation tests cover:

- Honest verifiers reaching QC and finality.
- Byzantine duplicate vote acceptance as duplicate and equivocation detection.
- Conflicting finality certificate rejection.
- Timeout/view change and recovery acceptance in new view.
- Offline verifier causing insufficient quorum.
- Verifier eligibility/operator voting-power cap.

Additional production tests required: network partition simulation, delayed/reordered message fuzzing, adversarial proposer behavior, persisted recovery/crash tests, and full lock-safety model checking.