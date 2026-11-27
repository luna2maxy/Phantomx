# PoI Uptime Proof Design (PX-SEC-0002)

> Status: DRAFT / FOUNDATION PROTOTYPE - NOT CONSENSUS-AUTHORITATIVE
> As of: 2026-11-26

## Goal
Replace self-reported heartbeat uptime with verifiable cryptographic uptime proofs.

## Threats
- Fake liveness, replayed responses, colluding verifiers, clock skew abuse, selective challenge censorship.

## Proposed Data Model (V1)

```text
PoiChallengeV1 {
  version: u8
  epoch: u64
  slot: u64
  challenge_id: hash32          // H(domain||canonical)
  verifier_pubkey: pubkey32
  machine_id: hash32
  owner_address: bytes
  nonce: bytes32
  issued_at_ms: u64
  expires_at_ms: u64
  challenge_type: u8            // uptime=1
  params_hash: hash32
  signature: sig64              // verifier
}

PoiResponseV1 {
  version: u8
  challenge_id: hash32
  epoch: u64
  slot: u64
  machine_id: hash32
  machine_pubkey: pubkey32
  response_hash: hash32
  responded_at_ms: u64
  signature: sig64              // machine key
}

PoiAttestationV1 {
  version: u8
  challenge_id: hash32
  epoch: u64
  slot: u64
  machine_id: hash32
  verifier_pubkey: pubkey32
  pass: bool
  score_uptime_milli: u32       // 0..100000
  observed_at_ms: u64
  evidence_hash: hash32
  signature: sig64              // verifier
}
```

## Signing Domains
- `PHX/poi/challenge/v1`
- `PHX/poi/response/v1`
- `PHX/poi/attestation/v1`

## Verification Rules
- Canonical serialization before sign/verify/hash.
- Reject unknown keys, bad sig, expired challenge, skew outside `MAX_SKEW_MS`.
- Replay cache keyed by `(challenge_id, machine_pubkey, verifier_pubkey, slot)`.
- Conflicting same-key attestation for same slot => equivocation evidence.
- Reward-relevant uptime only from quorum attestations.

## Quorum Aggregation
- `quorum_required = max(q_min, floor(2*n_active_verifiers/3)+1)`.
- `verified_slot = count(pass attestations from distinct active verifiers) >= quorum_required`.
- `S_uptime = verified_online_slots / total_slots_in_window`.

## Feature Gating
- Prototype must be behind config/feature flag.
- MUST NOT affect consensus settlement or `mine_one`/reward authority until finalized PoI record integration and release gates pass.

## Current Status
- Existing implementation is heartbeat accounting in RPC (`kPoiUptimeWindowSec=86400`); NOT production proof.
- No verifier challenge protocol, no machine response signatures, no quorum attestation, no on-chain evidence.
