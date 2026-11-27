# PoI Proof Commitment Design

Status: design only; no consensus code changed.

## Audit boundary

Current code has PoI challenge/evidence, metrics, score, verifier signatures, and aggregator certificates, but does not yet establish a canonical PoI proof encoding committed in every block. Relevant code: `include/phantomx/poi/challenge/challenge.hpp`, `include/phantomx/poi/evidence/evidence.hpp`, `include/phantomx/poi/measurement/measurement.hpp`, `include/phantomx/verifier/service.hpp`, `include/phantomx/aggregator/aggregator.hpp`.

This document defines the required target, not an assertion that the current implementation already satisfies it.

## Canonical proof envelope

A future PoI proof envelope must contain, in canonical order:

1. version;
2. network `chain_id`;
3. assignment/challenge ID;
4. member identity public key and payout address binding;
5. epoch and target block height/range;
6. challenge nonce and verifier-selected task;
7. evidence payload and measurement fields;
8. fixed-point score and measurement policy version;
9. ordered verifier public keys and signed attestations;
10. aggregator public key and certificate signature;
11. proof status and expiry.

All integers use fixed-width big-endian or an explicitly specified canonical compact encoding. Lengths are bounded by ChainParams. Trailing bytes and duplicate verifier IDs are invalid.

## Signature scope and replay protection

Member/evidence signatures must cover the domain tag, chain ID, challenge ID, assignment ID, epoch, height scope, evidence bytes, and policy version. Verifier signatures cover the same digest plus verifier decision. Aggregator signatures cover the ordered attestation hashes and certificate scope. A proof from another chain, member, assignment, epoch, or height scope must fail validation.

## Verification

A verifier checks challenge assignment, member identity, evidence format, measurement bounds, freshness, signature scope, and policy. An aggregator accepts only the required independent verifier quorum and rejects duplicate/conflicting attestations. The node must validate the certificate rather than trusting pool metadata.

## Block commitment

The preferred commitment is a deterministic `poi_commitment_root` over canonical proof/certificate leaves included by the proposer. Each leaf commits proof hash, member identity, assignment, epoch, height scope, score, verifier set, and aggregator certificate hash. The block must expose the root and enough data/proof for verifiers to recompute it.

Adding a header field or transaction type is consensus-visible. The exact field position, serialization, merkle/root algorithm, and proof inclusion mechanism must be finalized in implementation tests before activation.

## Activation and genesis

Enable only through a regtest activation height/flag. Testnet/mainnet remain disabled until restart/catch-up, invalid-proof, replay, and multi-node finality tests pass. Genesis contains no PoI proof unless a separate deterministic genesis commitment is specified.

## Test plan

Failing tests first:

- canonical encode/decode and trailing-byte rejection;
- chain/member/epoch/height replay rejection;
- invalid evidence and signature rejection;
- verifier quorum and duplicate-attestation rejection;
- deterministic commitment root;
- block ordering and invalid commitment rejection;
- four-node identical finalized commitment;
- restart/catch-up identical proof state.

## Risks

Uncommitted proof state permits divergent pool views. Ambiguous measurement policy permits non-deterministic validation. Missing chain/epoch binding permits replay. Header/transaction changes require a separate activation and migration record in `docs/PROGRESS.md`.
