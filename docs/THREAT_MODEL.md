# PhantomX Threat Model

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Status
Living document (Prompt 01). This is a foundation threat model; it is not a claim of security.

## Assets
- Chain integrity: block headers, transactions, UTXO set, chainstate DB.
- Consensus safety/finality: verifier votes, quorum/finality certificates.
- PoI integrity: challenges, evidence, scores, eligibility inputs.
- Verification integrity: verifier attestations, aggregator certificates.
- Keys: wallet, node, verifier, verifier, aggregator, pool/operator keys.
- Rewards/accounting: subsidy, distribution, claims/payouts.
- Node availability: P2P, RPC, DB, mempool.

## Trust Boundaries
- Untrusted P2P peers/messages.
- Untrusted RPC clients (except authenticated/authorized roles).
- Untrusted contributors/workers submitting evidence/shares.
- Aggregators are not trusted for correctness; certificates must be independently verifiable.
- Pools are not consensus authorities.
- Local filesystem/keystore permissions are trusted only as OS configuration; keys must be protected.

## Threats and Mitigations

| Threat | Impact | Current mitigation (foundation) | Required hardening |
|---|---|---|---|
| Double spend | Ledger inconsistency | UTXO validation, mempool checks, block validation | Full consensus integration, fork-choice/finality enforcement, adversarial tests |
| Invalid block/tx acceptance | Chain corruption | Structural validation, PoW/regtest checks, DB atomicity efforts | Complete consensus validation, property/fuzz tests |
| Replay (P2P/RPC/PoI evidence) | Duplicate effects/false credit | PoI nonce/replay registry foundation, tx uniqueness in mempool | Persistent replay caches, domain separation everywhere, network-wide anti-replay |
| Sybil participants | Fake contribution/reward capture | PoI not claimed Sybil-resistant; verifier/operator separation | Identity/bond/rate limits, verifier diversity, economic/governance controls |
| Verifier collusion | False PoI/certificates | No self-verify rule, assignment/audit intent | Randomized independent assignment, quorum, disputes, slashing, monitoring |
| Aggregator manipulation | False aggregation | Target: cert verifiable by full node | Implement cert builder/verifier, reject invalid/duplicate/conflicting attestations |
| PoI+Verifier equivocation | Safety violation risk | Equivocation concept in docs/code foundation | Persistent evidence, slashing/governance, full QC/finality rules |
| Network partition | Liveness loss / fork risk | PoI+Verifier safety assumptions documented | Partition tests, timeout/view-change hardening, finality-safe reorg rules |
| P2P DoS/resource exhaustion | Node outage | Basic rate/size limits in places | Comprehensive message bounds, peer scoring/ban, fuzz/chaos |
| RPC abuse | Unauthorized control/data leak | Token auth in daemon, local bind defaults | RBAC per method, TLS/reverse proxy guidance, audit log persistence, rate limits |
| Key compromise | Fund/theft/identity abuse | libsodium Ed25519, role separation, keystore 0600 best-effort | Encrypted keystore, HSM/OS keystore, rotation/revocation, memory hygiene |
| Reward manipulation/double payout | Inflation/unfair payout | Deterministic reward calc, ClaimRegistry foundation | Persistent claim nullifiers, on-chain settlement, audit trails |
| DB corruption/crash inconsistency | Data loss/invalid state | SQLite, recovery/verify methods | Atomic block+UTXO+undo writes, crash testing, backups/snapshots |

## Security Assumptions
- Cryptographic primitives (libsodium/Ed25519/SHA-256) are secure.
- <1/3 Byzantine voting power for PoI+Verifier safety (when full PoI+Verifier implemented).
- Network eventually delivers messages for liveness (partial synchrony).
- PoI evidence validity depends on verifier independence, freshness, and measurement design.

## Out of Scope / Non-Claims
- No smart contract VM in initial release.
- No cross-chain bridge in initial release.
- No mainnet security claim before independent audit and adversarial testing.
- PoI is not Sybil-proof by itself.

## Testing Requirements
- Unit, integration, multi-node regtest, property, fuzz, chaos, partition, Byzantine verifier, verifier collusion, aggregator failure, restart recovery, long-running tests. Do not claim pass without actual execution evidence.
