# PHANTOMX Verifier Registry Architecture

**Status:** DRAFT / INVESTIGATING / BLOCKED for implementation decision  
**Phase:** 15C.3  
**Scope:** Verifier identity, eligibility, revocation, and registry consistency for PoI/BFT finality.

## 1. Current Observed Behavior (code audit)

- **Storage:** verifier records are stored in chain metadata `verifier_registry_json` (JSON array) via `ChainDatabase::set_metadata/get_metadata` (`src/rpc/node_handlers.cpp` load/save helpers).
- **Registration:** RPC-only `verifier_register_challenge` + `verifier_register` with Ed25519 PoP (Phase 15C.2 binary payload v1). New registration creates `PENDING`.
- **Activation/status:** `verifier_set_status` requires `PHANTOMX_ADMIN_TOKEN`; transitions restricted by code. No governance/consensus authorization.
- **Eligibility:** RPC `poi_submit_attestation` and P2P attestation handler reject unregistered or non-ACTIVE verifiers by reading local registry.
- **Distribution:** **no P2P replication, no consensus state, no signed registry sync observed**. Each node relies on its local metadata DB.
- **Finality impact:** local verifier eligibility influences whether a node accepts/counts attestations and can finalize PoI blocks locally. This creates divergence risk if registries differ.

**Conclusion:** Current registry is **local-only metadata** with local enforcement. It is **NOT** consensus-authoritative and **NOT** deterministically replicated.

## 2. Architecture Options

| Criterion | A. Consensus-authoritative registry | B. Deterministically replicated registry | C. Signed external registry |
|---|---|---|---|
| Byzantine resistance | High if transitions are consensus-validated | Medium-high; depends on ordered authenticated update channel and fork handling | Depends on external signer trust/quorum; chain cannot independently prove legitimacy |
| Registration conflicts | Resolved by consensus ordering/state machine | Resolved by deterministic ordering; must define conflict rules | Possible if signers disagree; needs revocation/signature policy |
| Key rebinding | Explicit state transition in consensus | Deterministic update rule; replay/conflict protection needed | Signed update; requires key rollover policy |
| Owner transfer | Explicit consensus/governance tx/op | Explicit authenticated ordered update | Signed by governance key; off-chain policy heavy |
| Revocation propagation | Consensus-final; all nodes converge | Requires reliable replication and reorg handling | Depends on distribution freshness; partition can delay revocation |
| Network partition | Partition follows consensus rules; no local admin divergence | Nodes may diverge until sync; dangerous during partition | Nodes may miss revocation/update; stale trust possible |
| Restart/recovery | Recovered from chain state | Needs replay log/snapshot + anti-replay | Needs signed registry snapshot/feed + freshness |
| Node sync | Same as block sync | Extra sync protocol/state | Extra fetch/verify channel |
| Deterministic quorum eligibility | Yes, if part of consensus state | Possible if all nodes apply same ordered updates before finality decisions | Only if freshness and source trust are deterministic and enforced identically |
| Governance requirements | Protocol/consensus change | Protocol/network replication rules | External governance key management |
| Compatibility with existing PhantomX | Major: new tx/op/state transition/validation | Medium-major: new P2P/state replication | Medium: add signed registry verification, but trust/governance external |

## 3. Recommendation (based on current code)

**Recommended target:** **A. Consensus-authoritative verifier registry** for any registry state that affects PoI/BFT finality/quorum.  
**Rationale:** Current finality/attestation acceptance depends on verifier eligibility. Local-only admin status is unsafe for distributed consensus. Consensus-authoritative transitions give deterministic convergence, replay ordering, and auditable state transitions.

**Interim safe mode (until A implemented):** treat verifier registry as **local operator input only**, do not allow it to affect production consensus finality; keep PoI finality fail-closed/disabled in production-like configs. Any admin status change must be considered local and non-authoritative.

## 4. Required Governance/Protocol Decisions (BLOCKED)

1. **Authoritative source:** Is verifier eligibility consensus state, replicated state, or external signed policy? (Decision required before implementation.)
2. **Activation authority:** Who can activate PENDING verifiers? (on-chain governance, multisig, foundation key, validator vote, etc.)
3. **Revocation authority and SLA:** Who can revoke/suspend; how fast must revocation propagate before it is finality-safe?
4. **State transition format:** New transaction type / block payload / consensus message / governance artifact.
5. **Migration:** How existing local `verifier_registry_json` entries map into authoritative state; how conflicts are resolved.
6. **Compatibility:** Mixed-version behavior and upgrade activation height/epoch.

## 5. Security Implications

- **Current risk:** A node-local admin can mark verifiers ACTIVE/REVOKED differently than peers, causing attestation/quorum divergence or unsafe local finality decisions.
- **Required invariant:** For finality-affecting eligibility, all honest nodes must compute identical verifier eligibility for the same block/epoch/view.
- **PoP limitation:** PoP proves key possession only; it does not prove unique human/machine or honest behavior.

## 6. Status

- Architecture audit: **INVESTIGATING/PARTIAL**
- Comparison matrix: **IMPLEMENTED (design doc)**
- Recommended architecture: **PROPOSED (A)**
- Implementation: **BLOCKED** pending governance/protocol decision
- Production readiness: **NO-GO**
