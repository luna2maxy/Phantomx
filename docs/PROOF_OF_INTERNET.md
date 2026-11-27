# PhantomX Proof of Internet (PoI)

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Overview

PoI measures real network contribution from participants and produces a fixed-point score used for eligibility/contribution accounting in the PhantomX consensus model. PoI is **not** Sybil-resistant by itself; Sybil resistance and finality are handled by the separate PoI+Verifier/verifier identity/stake-policy layers.

## Score Weights

| Component | Weight | Notes |
|---:|---:|---|
| Bandwidth | 30% | Upload/download throughput under challenge-response |
| Uptime | 25% | Heartbeat availability over window |
| Latency | 20% | Multi-region probe RTT, robust aggregation |
| Relay | 25% | Opt-in relay delivery and accounted transfer |

Total: 100%. All scores use fixed-point integer arithmetic (`ScoreMilli`, `100000 = 100.000%`).

## Fixed-Point Scoring

- Component scores are clamped to `0..100000`.
- Total score: `sum(component_score * weight) / 100000`.
- No floating-point arithmetic in consensus-relevant scoring paths.

Implementation: `include/phantomx/poi/score.hpp`, `src/poi/score.cpp`.

## Bandwidth

Implemented foundation:

- **Randomized data challenge:** verifier issues challenge with nonce, expiry, payload size, subject/verifier identity.
- **Upload/download measurement:** bandwidth samples include upload/download kbps, timestamp, challenge nonce, verifier pubkey.
- **Throughput verification:** samples are accepted only when marked valid by verifier-side measurement flow (network probing runtime integrates with this model).
- **Multi-verifier measurement:** aggregate uses median across valid verifier samples.
- **Replay prevention:** nonce registry rejects reused challenge nonces within TTL.

Implementation: `src/poi/challenge/`, `src/poi/measurement/`.

## Uptime

Implemented foundation:

- **Heartbeat:** uptime sample tracks heartbeats ok/expected.
- **Availability window:** window start/end recorded.
- **Session tracking:** expected/ok/missed counters.
- **Missed heartbeat:** `missed_heartbeats` tracked.
- **Availability evidence:** evidence record captures window and measured counters; verifier attestation signs evidence hash.

## Latency

Implemented foundation:

- **Multi-region probing:** `LatencyProbe.region` and verifier identity per probe.
- **Repeated measurements:** multiple probes aggregated.
- **Median/robust aggregation:** trimmed mean (removes one outlier each side when >=5 samples); median helper available.
- **Outlier handling:** invalid/zero probes ignored; trimmed mean reduces outlier impact.
- **Signed measurement results:** evidence hash signed by verifier key (`VerifierAttestation`).

## Relay

Implemented foundation:

- **Opt-in relay job:** relay receipts represent opt-in relay work; only delivery-acked receipts count.
- **Delivery acknowledgement:** `RelayReceipt.delivery_ack`.
- **Data transfer accounting:** `bytes_relayed` summed for acknowledged deliveries.
- **Privacy protection:** payload contents not stored in evidence; receipts/evidence carry hashes/metadata, not plaintext user files.
- **Abuse prevention:** only verifier-attested evidence, nonce freshness/replay prevention, rate/resource limits enforced at network/runtime layers; invalid receipts excluded.

## Evidence and Verification

Evidence record includes: kind, subject pubkey, challenge nonce, window, measured values, component score, simulator flag, verifier attestations.

Rules:

- **No self-reported final proof:** participant reports are not final; evidence must have independent verifier attestation(s).
- **Challenge-response:** measurements tied to random challenge nonce.
- **Nonce:** unique challenge nonce; replay registry rejects reuse.
- **Signed evidence:** verifier signs `SHA-256d("PHX/poi/evidence/v1" || serialized_evidence)`.
- **Freshness:** challenge issued/expires timestamps; stale evidence rejected by policy.
- **Independent verification:** `evidence_has_independent_attestation` requires valid signatures from distinct verifier keys.

Implementation: `src/poi/evidence/`.

## Simulator

`src/poi/simulator.cpp` provides deterministic/synthetic test metrics and evidence generation for local testing.

**Warning:** Simulator output is marked `simulator=true` and MUST NOT be treated as mainnet proof or consensus-valid PoI evidence.

## Limitations

- This is a foundation module; live network probing, verifier quorum policy, staking/identity binding, and slashing/penalty economics are integrated in later stages.
- Runtime must enforce verifier independence (no self-verification, no pool self-verify), clock freshness bounds, and secure measurement transport.
- Latency/bandwidth targets and normalization parameters are policy-configurable and must be versioned for consensus.