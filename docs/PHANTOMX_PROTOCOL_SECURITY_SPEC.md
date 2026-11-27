# PHANTOMX_PROTOCOL_SECURITY_SPEC.md

> **Status:** DRAFT / TARGET SPECIFICATION — **NOT PRODUCTION APPROVED**  
> **Document role:** Single source of truth for PhantomX protocol, security, PoI, consensus, settlement, and release gates.  
> **Current production decision:** **NO-GO** (mainnet/testnet production).  
> **As of:** 2026-11-26  
> **Baseline code reality:** `build-ubuntu` green, unit 99/99, ctest 11/11, official BFT regtest PASS, `E2E_VIEW_CHANGE=1` PASS. Current PoI identity/uptime/settlement/multi-machine implementations contain foundation-only mechanisms and MUST NOT be treated as production-complete.

## 0. Normative Language

The keywords **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are normative. "Foundation" means implemented for development/testing but not security/production complete. "Target" means required by this spec; it may not be fully implemented.

## 1. Threat Model

### 1.1 Assets
- **Chain safety:** no conflicting finalized blocks, no invalid state transition.
- **Chain liveness:** honest quorum can eventually finalize valid blocks under assumptions.
- **PoI integrity:** bandwidth/latency/uptime/relay measurements influence rewards only when verified.
- **Reward integrity:** no inflation, no double payout, no unearned payout, deterministic accounting.
- **Identity integrity:** owner/machine identity cannot be cheaply forged, replayed, or double-counted.
- **Key/wallet integrity:** private keys, RPC credentials, validator keys, verifier keys.

### 1.2 Trust Assumptions
- **BFT safety:** Byzantine voting power `< 1/3` of total active voting power.
- **BFT liveness:** partial synchrony/eventual delivery and `> 2/3` honest active voting power available.
- **Cryptography:** Ed25519, SHA256d/SHA256 as used, libsodium primitives, and random sources are secure when correctly used.
- **PoI verification:** no single verifier is trusted; reward-relevant PoI records require quorum of independent verifiers.
- **Time:** nodes have bounded clock skew; freshness windows use explicit skew limits. Clock is not a safety oracle.

### 1.3 Adversaries
- **Network adversary:** delay, drop, reorder, replay, partition, eclipse, Sybil peers, DDoS.
- **RPC adversary:** unauthenticated/abused endpoints, credential theft, request forgery, source-IP spoofing/proxy/NAT ambiguity.
- **PoI participant adversary:** fake bandwidth/latency/uptime, replayed heartbeats, multi-identity/Sybil machines, colluding machines, VM farms.
- **Verifier adversary:** dishonest measurement, collusion, censorship, equivocation, selective attestation, stale evidence.
- **Validator/BFT adversary:** equivocation, invalid proposal, withholding, view-change abuse, randomness bias.
- **Economic adversary:** reward farming, double counting, slashing evasion, griefing appeals, settlement manipulation.

### 1.4 Explicit Non-Assumptions
- Source IP MUST NOT be treated as identity proof. It MAY be telemetry/rate-limit signal only.
- Self-reported heartbeat MUST NOT be treated as uptime proof.
- PoP signature alone MUST NOT be treated as Sybil resistance.
- RPC-local aggregate score MUST NOT be treated as consensus/settlement authority.
- `mine_one` local scaling MUST NOT be treated as on-chain settlement.

### 1.5 Security Goals
| Goal | Requirement |
|---|---|
| G1 Safety | No two conflicting blocks finalized for same height/epoch rule. |
| G2 Finality integrity | Finalized state never replaced by higher-PoI/non-finalized chain. |
| G3 PoI authenticity | Reward-relevant PoI data has verifiable cryptographic evidence. |
| G4 Sybil resistance | Creating many identities/machines has bounded economic/cost/attestation barrier; no free unlimited weight. |
| G5 No double payout | Same owner/machine/epoch/measurement slot cannot be rewarded twice. |
| G6 Deterministic settlement | Every node replaying chain computes identical rewards/penalties. |
| G7 Auditability | All reward/slashing decisions traceable to finalized records/evidence. |
| G8 Availability | Liveness degrades gracefully under faults; no silent corruption. |

### 1.6 Current Known Gaps (NO-GO)
- No mechanically checked formal proof (TLA+/TLC/Apalache equivalent) for full protocol.
- No independent security audit.
- No long-running multi-node adversarial/partition/Byzantine evidence.
- No production VRF/unbiased randomness.
- No full on-chain slashing settlement economics.
- PoI uptime currently heartbeat accounting; identity currently PoP + IP heuristic foundation.

## 2. Identity and Sybil Resistance

### 2.1 Identity Objects

#### Owner Identity
- `owner_address`: canonical reward/account address.
- `owner_pubkey`: Ed25519 public key bound to owner; MUST sign owner-level registration.
- `owner_id`: `H("PHX/poi/owner/v1" || owner_pubkey || owner_address)`.

#### Machine Identity
- `machine_pubkey`: Ed25519 key generated/stored on machine; private key never leaves machine.
- `machine_id`: `H("PHX/poi/machine/v1" || owner_id || machine_pubkey || metadata_hash)`.
- `metadata_hash`: hash of stable declared metadata (agent version, capabilities, declared region class, optional attestation descriptor). Raw metadata MUST NOT include secrets.

#### Registration Record
```text
PoiMachineRegistrationV1 {
  version: u8 = 1
  owner_address: bytes
  owner_pubkey: pubkey32
  machine_pubkey: pubkey32
  machine_id: hash32
  nonce: u64
  issued_at: u64
  expires_at: u64
  stake_or_bond_ref: optional hash32/txid
  attestation_ref: optional hash32
  capabilities: bitfield/u64
  signature_owner: sig64   // over owner domain
  signature_machine: sig64 // over machine domain
}
```

### 2.2 Signing Domains
All signatures MUST be domain-separated and canonical-serialized:
- `PHX/poi/owner-register/v1`
- `PHX/poi/machine-register/v1`
- `PHX/poi/challenge/v1`
- `PHX/poi/response/v1`
- `PHX/poi/measurement-attestation/v1`
- `PHX/poi/evidence/v1`
- `PHX/poi/settlement/v1`

### 2.3 Anti-Sybil Controls (Target)
- **Cost/stake:** each active machine MUST carry minimum bond/stake/fee or protocol-approved credential. Unbonding has delay; slashing can consume bond.
- **Uniqueness:** `machine_pubkey` MUST map to exactly one active `owner_id` at a time. Reassignment requires expiry/revocation/finalized transfer rule.
- **Rate limits:** registration, challenge response, and attestation submission rate-limited per owner, machine, IP-class, and verifier.
- **Attestation option:** hardware/device/cloud attestation MAY reduce Sybil score penalty but MUST NOT bypass cryptographic evidence/quorum.
- **IP policy:** IP is advisory. MAY be used for abuse throttling; MUST NOT be sole accept/reject for consensus-relevant identity except temporary RPC abuse protection. Current "1 IP 2 address reject" is foundation-only.
- **Auditability:** active bindings, revocations, conflicts, and penalties persisted and queryable by deterministic index.

### 2.4 Identity State Machine
```text
UNREGISTERED -> PENDING_REGISTRATION -> ACTIVE -> COOLDOWN -> EXPIRED/REVOKED/SLASHED
ACTIVE -> CHALLENGED -> ACTIVE
ACTIVE -> SUSPENDED (evidence pending) -> ACTIVE|SLASHED|REVOKED
```
State transitions MUST be caused by signed records/finalized chain events, not local RPC memory alone.

### 2.5 Current Foundation Mapping
- **Implemented:** remote PoP signature `identity_pubkey/sig/ts`, owner<->pubkey binding persisted in `poi_state_json`, IP conflict reject.
- **Missing:** machine key registration protocol, stake/bond/cost, auditable chain-visible registry, revocation/rotation lifecycle, Sybil resistance proof.

## 3. PoI Record Specification

### 3.1 Canonical PoI Evidence Types

#### Challenge
```text
PoiChallengeV1 {
  version, epoch, slot, challenge_id, verifier_pubkey, machine_id,
  owner_address, nonce, issued_at_ms, expires_at_ms, challenge_type,
  parameters_hash, signature
}
```
`challenge_type`: `bandwidth`, `latency`, `uptime`, `relay`, or `composite`.

#### Response
```text
PoiResponseV1 {
  version, challenge_id, epoch, slot, machine_id, machine_pubkey,
  response_hash, metrics_payload_hash, responded_at_ms, signature
}
```

#### Verifier Attestation
```text
PoiAttestationV1 {
  version, challenge_id, epoch, slot, machine_id, verifier_pubkey,
  pass: bool, score_components_milli, measured_values_hash,
  observed_at_ms, evidence_hash, signature
}
```

#### Aggregated PoI Record (reward-relevant)
```text
PoiRecordV1 {
  version, epoch, slot, owner_address, machine_id,
  attestation_root, attestation_count, quorum_required,
  score_bandwidth_milli, score_latency_milli, score_uptime_milli,
  score_relay_milli, score_network_milli, score_composite_milli,
  weight_milli, evidence_hash, finalized_height, finalized_block_hash
}
```

### 3.2 Score Formula (v2, retained)
All components normalized to `[0,1]`; invalid/missing/NaN/Inf/negative = unavailable (`-1`) and MUST NOT contribute positively. Missing uptime MUST NOT be treated as perfect.

- `S_bw = B / (B + 500)` where `B` is verified Mbps, no hard cap/saturation.
- `S_lat = 1 / (1 + L / 100)` where `L` is verified latency ms.
- `S_uptime = verified_online_time / total_measurement_window`, clamped `[0,1]`.
- `S_relay = clamp(R,0,1)` from verified relay ratio.
- `S_network = 1 - exp(-M / Mref)` from verified active miners/network signal.

Weights (sum 1.0):
- bandwidth `0.30`
- latency `0.20`
- uptime `0.25`
- relay `0.15`
- network `0.10`

`score_percent = 100 * clamp(sum(weight_i * S_i)/sum(active_weight_i),0,1)`. If required components are unavailable per policy, record is partial/ineligible as specified.

### 3.3 Validity Rules
- Canonical serialization required before hashing/signing.
- Record ID = `H(domain || canonical_bytes)`.
- Duplicate `(epoch, slot, machine_id, challenge_id, verifier_pubkey)` attestation MUST be ignored as duplicate; conflicting same-key attestation is equivocation evidence.
- Evidence older than freshness window MUST be rejected.
- Reward-relevant records MUST reference finalized/quorum-verified evidence.

## 4. Verifier Trust Model

### 4.1 Verifier Roles
- **Consensus validator:** BFT voting/finality (existing BFT engine).
- **PoI verifier:** issues challenges, measures, signs attestations. May be same or separate role; separation MUST be explicit in config/policy.

### 4.2 Trust Rules
- No single PoI verifier can authorize a reward-relevant record.
- `quorum_required = max(q_min, floor(2*n_active_verifiers/3)+1)` unless protocol sets stricter threshold.
- Verifier set MUST be deterministic for epoch/slot assignment.
- Verifier assignment SHOULD be unpredictable/unbiasable (VRF/randomness beacon) to reduce collusion.
- Verifiers MUST sign measurements; unsigned or unknown-key attestations rejected.

### 4.3 Conflict of Interest / Collusion
- Verifier MUST NOT attest own machine/owner where prohibited by policy.
- Operator caps and identity clustering limits apply.
- Collusion evidence (identical timing/payload patterns, shared operator, impossible geography/latency) feeds slashing/governance review.

### 4.4 Current Foundation Mapping
- Existing BFT validator/quorum/signature logic is present; PoI verifier attestation path exists but is foundation and not full consensus-verified settlement input.

## 5. Cryptographic Uptime Proof

### 5.1 Protocol
1. **Assign:** deterministic/unbiasable assignment selects verifier quorum for `(machine_id, epoch, slot)`.
2. **Challenge:** each verifier sends signed `PoiChallengeV1` with nonce and expiry.
3. **Response:** machine signs `PoiResponseV1` with machine key; includes challenge ID and freshness.
4. **Verify:** verifier performs independent check (liveness probe, protocol ping, or approved telemetry proof) and emits signed `PoiAttestationV1`.
5. **Aggregate:** quorum attestations form `PoiRecordV1`. Only aggregated record may influence reward weight.

### 5.2 Freshness / Replay Protection
- Nonce unique per challenge; response MUST bind nonce/challenge_id.
- `expires_at_ms` enforced with bounded clock skew (`MAX_SKEW_MS`, target 300s or tighter by network class).
- Reject replayed `(challenge_id, machine_pubkey)` or old slot evidence.
- Evidence hash indexed in replay cache until epoch finalized + retention window.

### 5.3 Uptime Calculation
- `verified_online_time = count(valid_quorate_uptime_slots) * slot_duration`.
- `total_measurement_window = epoch_window_duration`.
- `S_uptime = verified_online_time / total_measurement_window`.
- Missing/unquorate slot counts as offline/unverified, never perfect.

### 5.4 Current Foundation Mapping
- **Current:** heartbeat counter window in RPC. **Not acceptable for production.**
- **Target:** signed verifier challenge/response/attestation quorum.

## 6. Epoch Lifecycle

### 6.1 Epoch Definition
- `epoch = floor(height / EPOCH_BLOCKS)` or protocol-defined time/height hybrid; MUST be deterministic and network-wide.
- Epoch parameters: verifier set snapshot, randomness seed, challenge schedule, settlement window, slashing appeal window.

### 6.2 Lifecycle
```text
EPOCH_OPEN
  -> IDENTITY_SNAPSHOT
  -> CHALLENGE_SCHEDULE_PUBLISHED
  -> MEASUREMENT_SLOTS (challenge/response/attestation)
  -> RECORD_AGGREGATION
  -> RECORD_FINALIZATION (BFT/finality)
  -> REWARD_CALCULATION
  -> SETTLEMENT_COMMIT
  -> EPOCH_CLOSED
```

### 6.3 Boundary Rules
- Verifier/identity set changes apply at boundary only unless emergency governance rule.
- Evidence from epoch E cannot settle in epoch E-1.
- Late evidence MAY be accepted only within explicit grace window and MUST NOT mutate finalized settlements.

## 7. Consensus State Transitions

### 7.1 BFT/Finality (existing model retained)
- Two-phase votes: Prevote, Precommit.
- Quorum: `signed_power * 3 > total_voting_power * 2`.
- Finality certificate: valid Precommit QC.
- Finalized block MUST NOT be replaced by higher PoI score or non-finalized chain.
- View change/pacemaker/lock rules follow existing BFT safety constraints.

### 7.2 PoI-Influenced State Transition (Target)
PoI affects consensus only through finalized, verifiable records:
1. Block proposes/includes PoI record commitments and settlement refs.
2. Node validates records: signatures, quorum, freshness, registry membership, no double count, deterministic score.
3. BFT finalizes block containing valid commitments.
4. Settlement state transition applies deterministic reward/slash updates.

### 7.3 Invalid Transition Conditions
- Invalid signature/domain/serialization.
- Unknown/slashed verifier or unregistered machine/owner.
- Insufficient quorum.
- Replay/duplicate slot settlement.
- Conflicting finalized record for same slot.
- Settlement amount exceeds epoch budget/invariant.

## 8. Reward Calculation

### 8.1 Budget
- Block subsidy from `rewards::block_subsidy_atoms(height)`.
- Split: PoI contributors 90%, verifier operators 10% (master plan; no pool operator subsidy category). Any change requires governance + spec version bump.
- Remainder from rounding goes to the PoI contributor bucket; the epoch sum is exact, with no silent inflation.
- Share arithmetic MUST use overflow-safe `mul_div_u64()` / `bps_share()`; a plain `(total * bps) / denom` wraps above ~9.22e14 atoms.

### 8.2 PoI Weight
For each finalized machine record in epoch:
- `w_m = clamp(score_composite_milli / 100000, 0, 1) * eligibility_factor * anti_sybil_factor`.
- `owner_weight = min(W_owner_cap, sum(w_m for machines owned by owner))`.
- `machine_payout_share = owner_payout_share * (w_m / sum(w_m in owner, capped/normalized))`.

### 8.3 Determinism
- Inputs: finalized chain records only.
- Fixed-point integer arithmetic for consensus-critical accounting.
- No floating point in consensus/state transition; floating point MAY be used only in non-authoritative UI.

### 8.4 Current Foundation Mapping
- **Current:** `mine_one` scales contributor share by max active RPC PoI score. **Not production settlement.**

## 9. Settlement Transactions

### 9.1 Target Model
Settlement MUST be on-chain and replay-verifiable. Two acceptable implementation shapes (choose one in Phase 1, freeze in spec):
- **A. Settlement Tx:** explicit transaction kind carrying epoch settlement commitment and outputs.
- **B. State Transition Receipt:** block-committed settlement root with deterministic application rules; outputs/proofs auditable.

### 9.2 Minimum Settlement Record
```text
PoiSettlementV1 {
  version, epoch, finalized_height, poi_records_root,
  reward_budget_atoms, poi_pool_atoms, verifier_pool_atoms,
  payouts_root, slashes_root, unissued_atoms, rule_hash, signature_or_qc_ref
}
```

### 9.3 Invariants
- `sum(payouts) + sum(slashes_burned_or_redirected) + unissued == distributable_budget` by rule.
- No negative balances.
- No payout without finalized eligible PoI record.
- Settlement tx/record cannot be replayed for same epoch.

## 10. Ownership and Multi-Machine Accounting

### 10.1 Ownership Binding
- Machine MUST be registered to exactly one `owner_address` at active time.
- Reward payout destination defaults to owner address; alternate payout address requires signed owner authorization.

### 10.2 Multi-Machine Aggregation
- Aggregation is per epoch from finalized per-machine records.
- Anti-double-count key: `(epoch, slot, machine_id, challenge_id)` and `(epoch, owner_address, machine_id)`.
- Same physical machine claiming multiple machine IDs MUST be detectable by evidence/policy and penalizable.
- Owner cap prevents unlimited linear scaling; exact cap curve is protocol parameter.

### 10.3 Audit Trail
For every payout, store/index:
- owner, machine_id, epoch, finalized record hash, weight, gross atoms, penalties, net atoms, settlement tx/ref.

### 10.4 Current Foundation Mapping
- **Current:** RPC `aggregate_score = min(100, sum(active participant scores))`. Useful for UI/foundation only; **not audited on-chain payout weight**.

## 11. Slashing Evidence and Execution

### 11.1 Evidence Types
- BFT equivocation (vote/view-change/proposal where applicable).
- PoI verifier equivocation/conflicting attestation.
- Invalid/fraudulent measurement attestation.
- Replay/freshness violation.
- Identity/machine double-binding fraud.
- Randomness withholding/reveal violation (when randomness protocol active).

### 11.2 Evidence Record
```text
SlashingEvidenceV1 {
  version, evidence_id, offender_pubkey_or_machine_id, reason,
  epoch, height, slot, evidence_hash, evidence_payload_ref,
  reporter, signatures, observed_at
}
```

### 11.3 Execution
- Evidence MUST be verifiable deterministically.
- Slashing decision: automatic for objective cryptographic equivocation; governance/appeal path for subjective/complex cases as policy defines.
- Penalty application MUST be on-chain settlement/state transition, not only in-memory engine accounting.
- Appeal: time-bounded; appeal does not erase evidence; resolution recorded.

### 11.4 Current Foundation Mapping
- BFT engine has slashing records/appeal/settlement accounting hooks; not full on-chain economic enforcement. PoI slashing not production-complete.

## 12. Randomness

### 12.1 Requirements
- Randomness used for proposer/assignment MUST be unpredictable before commitment, unbiasable by single participant within tolerance, publicly verifiable, and deterministic after finalization.
- Commit-reveal alone is insufficient for production if last revealer bias is material.

### 12.2 Target
- Production VRF or beacon scheme with slashing for withholding and verifiable proofs.
- Randomness seed committed/finalized per epoch and referenced by assignments.

### 12.3 Current Foundation Mapping
- BFT has commit-reveal randomness foundation and deadline/slash hooks; **not unbiased production VRF**. NO-GO.

## 13. Finality and BFT

### 13.1 Rules
- Active validator set defined per epoch; total voting power known.
- Proposal/vote/QC signatures verified over domain-separated hashes.
- Quorum: `>2/3` voting power.
- Lock rule prevents conflicting commits; justify-QC/high-QC rules enforced.
- View change uses quorum certificates and deterministic proposer selection.
- Equivocation detected, evidenced, and penalized.

### 13.2 Recovery
- WAL/state persistence required; node restart MUST not violate lock/finality.
- Sync missing blocks/QCs/certificates before participating.
- Conflicting finalized cert rejected.

### 13.3 Current Foundation Mapping
- BFT engine, pacemaker, lock/finality normalization, WAL, equivocation/slashing hooks exist; regtests pass. Production claim still blocked by formal proof, long-running adversarial evidence, VRF, and full on-chain slashing economics.

## 14. Formal Properties

### 14.1 Safety Properties
- **S1 No conflicting finality:** cannot finalize two different block hashes at same finalized height.
- **S2 Lock safety:** committed/locked block not reverted by conflicting proposal without valid higher justify QC per rule.
- **S3 Settlement conservation:** no inflation; budgets conserved.
- **S4 No double reward:** unique settlement key cannot pay twice.
- **S5 Evidence integrity:** accepted evidence verifies cryptographically and canonically.

### 14.2 Liveness Properties
- **L1 Finality liveness:** under partial synchrony and >2/3 honest power, valid proposals finalize.
- **L2 Settlement liveness:** finalized PoI records settle within bounded epoch window.
- **L3 Challenge liveness:** eligible machines receive/challenge slots per schedule when verifiers available.

### 14.3 Verification Plan
- Maintain TLA+/model spec for BFT and PoI settlement state machine.
- CI-executable bounded model checks (`tools/model_check_bft.py` class) for smoke.
- Mechanically checked proof/model checking (TLC/Apalache/equivalent) for release gate.
- Deterministic fault-injection/adversarial cluster runs archived as evidence.

## 15. GO/NO-GO Release Gates

A release is **GO** only when all mandatory gates pass. Otherwise **NO-GO**.

| Gate | Mandatory Evidence | Status (current) |
|---|---|---|
| GATE-01 Spec frozen | Approved protocol/security spec, versioned, test vectors | NO-GO (draft) |
| GATE-02 Deterministic build/test | Reproducible build docs, CI green, unit/ctest/regtest/BFT VC green | Partial (local green; reproducible/prod CI evidence incomplete) |
| GATE-03 Identity/Sybil | Signed registration, persistent auditable registry, stake/cost/attestation policy, anti-replay/conflict tests | NO-GO |
| GATE-04 Uptime proof | Signed verifier challenges, quorum attestations, freshness/replay tests, no self-report authority | NO-GO |
| GATE-05 Consensus-verified PoI | PoI records validated by quorum and finality before reward effect | NO-GO |
| GATE-06 On-chain settlement | Deterministic settlement tx/state transition, replay/audit tests, no inflation | NO-GO |
| GATE-07 Multi-machine accounting | Per-machine finalized weights, anti-double-count, owner cap, audit trail | NO-GO |
| GATE-08 Slashing economics | Verifiable evidence, on-chain penalty execution, appeal policy, economic simulation | NO-GO |
| GATE-09 Formal verification | Mechanically checked BFT/settlement model artifacts in CI or audited process | NO-GO |
| GATE-10 Security audit | Independent audit, critical/high resolved, residual risk accepted formally | NO-GO |
| GATE-11 Adversarial ops evidence | Long-running multi-node partition/Byzantine/fault-injection/soak, runbooks, monitoring | NO-GO |
| GATE-12 Randomness | Production VRF/unbiased randomness with verification and withholding penalties | NO-GO |

### Release Decision Rule
- **Mainnet:** all GATE-01..12 required.
- **Public production testnet:** GATE-01..08,10,11,12 required; GATE-09 may be limited only by explicit risk acceptance, but still NO-GO for mainnet.
- **Dev/testnet internal:** may run with foundation gaps, but MUST be labeled non-production and MUST NOT claim security/finality/economic guarantees.

## Appendix A — Implementation Mapping (Current)

| Area | Current files/modules | Current status |
|---|---|---|
| PoI scoring v2 | `include/phantomx/poi/score.hpp`, `src/poi/score.cpp`, `tests/unit/test_poi.cpp` | Foundation/partial; deterministic formula present |
| PoI RPC/state | `src/rpc/node_handlers.cpp` | Foundation; contains heartbeat accounting and local aggregate |
| Identity PoP foundation | `src/rpc/node_handlers.cpp`, `src/rpc/http_server.cpp` | Foundation; remote signed claim, local unsigned fallback |
| BFT engine | `include/phantomx/consensus/bft.hpp`, `src/consensus/bft.cpp` | Foundation+; regtests pass, production gates open |
| Rewards | `include/phantomx/rewards/rewards.hpp`, `src/rewards/rewards.cpp` | Foundation; not full on-chain PoI settlement |
| Chain/UTXO | `src/blockchain/*`, `src/utxo/*` | Existing base; settlement integration required |
| Wallet/Qt | `apps/phantomx-qt/*` | UI/foundation; must reflect audited on-chain data |

## Appendix B — Required Immediate Documentation Actions

- Keep `docs/KNOWN_LIMITATIONS.md` synchronized with this spec.
- Add test vectors for identity signatures, challenge/response, attestation quorum, settlement invariants.
- Add migration/versioning note for `poi_state_json` and any future on-chain PoI record format.
- Mark all non-authoritative RPC fields (`aggregate_score`, heartbeat uptime, local settlement scaling) as foundation/diagnostic until Phase 4-6 complete.
