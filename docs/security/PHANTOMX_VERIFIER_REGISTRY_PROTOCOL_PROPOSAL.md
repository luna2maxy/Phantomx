# PHANTOMX Verifier Registry Protocol Proposal (Option A)

**Status:** DRAFT / BLOCKED — requires governance/protocol approval before implementation  
**Phase:** 15C.6  
**Scope:** Consensus-authoritative verifier registry for PoI/BFT eligibility and finality.

## 1. Problem

Current verifier registry is local-only chain metadata (`verifier_registry_json`) mutated by RPC admin operations. Nodes can diverge on ACTIVE/SUSPENDED/REVOKED status, making finality-affecting eligibility non-deterministic across nodes. This is unsafe for production consensus.

## 2. Proposal: Consensus-authoritative registry

All verifier registry transitions affecting eligibility/quorum/finality MUST be deterministic consensus state transitions, validated by every node from chain data.

## 3. State model

### Consensus state (authoritative)
- `verifier_id` (bounded bytes/string, canonical encoding)
- `public_key` (Ed25519 32 bytes)
- `owner_id` (canonical owner identity; exact type requires protocol decision)
- `status` enum: `PENDING | ACTIVE | SUSPENDED | REVOKED | EXPIRED`
- `registered_height`
- `status_height`
- `activation_height` (optional)
- `revocation_height` (optional)
- `registry_version`
- `identity_hash` (canonical hash of immutable identity fields)

### Non-consensus metadata (local/operator, non-authoritative)
- endpoint, name, api_url, last heartbeat, latency metrics, local notes.
- MUST NOT affect quorum/finality eligibility.

## 4. State transitions

| Transition | Preconditions | Authorization | Deterministic effect | Effective |
|---|---|---|---|---|
| Register | valid PoP v1, unused challenge, no conflicting identity | PoP proves key possession; registration acceptance policy defined by protocol | create PENDING record | block including valid transition |
| Activate | PENDING or policy-allowed SUSPENDED, authorized actor | governance/consensus-authorized actor (BLOCKED decision) | status=ACTIVE, activation_height=H | H or configured epoch boundary |
| Suspend | ACTIVE/PENDING per policy | authorized actor | status=SUSPENDED | H/epoch |
| Revoke | ACTIVE/PENDING/SUSPENDED | authorized actor | status=REVOKED, revocation_height=H | H/epoch; irreversible unless explicit governance rule |
| Expire | expiry rule satisfied | deterministic rule | status=EXPIRED | deterministic height/epoch |

All transitions must be signed, ordered, replay-protected, and validated identically by all nodes.

## 5. Eligibility rules

- Only `ACTIVE` verifiers with valid keys and no conflicting state are eligible for PoI/BFT attestation at block/epoch H.
- Eligibility is computed from finalized/consensus state at deterministic height/epoch, never from local admin metadata.
- Suspended/revoked/expired/pending/unknown/corrupt state => ineligible (fail-closed).

## 6. Migration

- Existing `verifier_registry_json` local metadata is **non-authoritative** and cannot be silently promoted to consensus state.
- Migration requires explicit import/registration transactions or genesis/bootstrap governance decision.
- Conflicts (duplicate id/key/owner) must fail closed and require operator/governance resolution.

## 7. Upgrade / mixed-version

- New registry rules activate at explicit activation height/epoch.
- Pre-activation nodes must not accept post-activation registry transitions as authoritative without upgrade.
- Mixed-version finality participation is unsafe; define release gate and fork/upgrade policy.
- Reindex/snapshot recovery must reconstruct registry from consensus state transitions.

## 8. Threat model

- Malicious admin/local RPC: cannot alter consensus eligibility under Option A.
- Compromised verifier key: revoke via authorized consensus transition; PoP not Sybil resistance.
- Duplicate identity/key rebinding/owner conflict: rejected deterministically.
- Replay/conflicting transitions: rejected by ordering/nonce/height rules.
- Network partition: consensus rules apply; no local divergence of authoritative state.
- Upgrade disagreement: may cause fork; requires activation coordination.

## 9. Required governance/protocol decisions (BLOCKED)

1. Authorized actor model for Activate/Suspend/Revoke (multisig, governance tx, validator vote, foundation key with sunset, etc.).
2. Transition transport: dedicated tx type, consensus message, or governance payload.
3. Activation height/epoch and testnet rollout plan.
4. Reactivation policy (especially post-REVOKED).
5. Owner identity format and ownership transfer rules.
6. Migration plan for existing local registries.

## 10. Recommendation

Adopt Option A for finality-affecting eligibility. Do not implement until decisions above are approved. Until then, keep production/testnet finality fail-closed where local registry could diverge.

**Mainnet/Production testnet: NO-GO.**
