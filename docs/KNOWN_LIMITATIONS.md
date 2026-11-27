# PhantomX Known Limitations

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Status

This document lists known limitations as of the current foundation state. It is not exhaustive. Absence from this list does not imply readiness.

## Build / Verification

- Local compile/test not executed in the assessment environment (no C++ compiler).
- CI green status not reverified during this assessment; prior observed CI run was old/cancelled before recent fixes.
- Reproducible builds not configured/proven.
- Cross-platform packaging/signing not complete.

## Consensus / PoI+Verifier / PoI

- PoI+Verifier implementation is a foundation, not fully production-hardened. 2026-11-25 hardening passes: (1) equivocation auto-slash, slashed-validator rejection, WAL v3 persistence for slashed/equivocation evidence (v1/v2 load-compatible). (2) Pacemaker with armed progress, exponential backoff, timeout cap, view-advance on timeout. (3) Commit-reveal randomness beacon with reveal deadline + `enforce_randomness_deadline` reveal-or-slash foundation; **still not unbiased production VRF** (last-revealer bias mitigated by deadline/slash, not eliminated). (4) Governance/slashing records with penalty atoms, appeal/resolve, settlement accounting (`settle_slashing`, total/unsettled penalty atoms); **engine-level accounting hook, not full on-chain economic enforcement/settlement tx execution**. (5) Formal/model artifacts: `docs/formal/bft_safety.tla` and executable bounded checker `tools/model_check_bft.py` (passes). (6) Adversarial cluster script `scripts/adversarial-bft-cluster.sh` added; smoke run in current dirty tree hit E2E RPC/script mismatch (`bft_local_pubkey` method not found) so long-running independent cluster evidence is **not yet produced here**. Remaining for production claim: TLC/Apalache or equivalent mechanically checked proof, independent security audit, successful long-running multi-node adversarial/partition/Byzantine cluster evidence, production VRF/randomness, and full on-chain slashing settlement economics. **Mainnet NO-GO**.
- PoI is not Sybil-resistant; anti-Sybil controls are heuristic/policy foundation.
- Verifier/operator cap enforcement needs consensus/governance integration and monitoring.

## P2P / Network

- P2P is a foundation (thread-per-peer, simple sync, basic rate limits/replay cache).
- No full transport encryption/authentication for peer channels.
- Eclipse/DoS resistance needs adversarial testing and hardening.
- Persistent replay/ban state and addrman hardening incomplete.

## Storage

- Storage modules are foundations: real network upload/download, repair, replication rebalance, deletion proofs, and on-chain settlement integration incomplete.
- Capacity verification is registry/state foundation; needs independent challenge/audit enforcement.
- Storage payments/escrow are not fully on-chain settled; dispute arbitration incomplete.
- Encryption uses libsodium secretbox but key lifecycle, nonce strategy, backup/recovery, and secure deletion need production audit.

## Rewards / Tokenomics

- Reward settlement is foundation; persistent claim nullifiers/on-chain settlement integration incomplete.
- Unclaimed reward policy (burn/carry/treasury/expiry) must be explicitly finalized.
- Category with no eligible participants is not redistributed by foundation logic; governance rule required.
- Supply/reward invariants need formal proof and multi-node consensus integration tests.

## Wallet / Keys

- Keystore is foundation; encrypted at-rest keystore, OS secure enclave/keychain integration, memory hygiene, and backup hardening incomplete.
- Desktop Qt wallet is skeleton; most features require live RPC/storage integration.
- Mobile wallets are skeletons only; no production secure storage/networking/UX flows implemented.
- Seed/private key handling requires security audit before any production release.

## RPC / CLI / APIs

- RPC handlers are largely foundation/stubbed; not fully wired to live chainstate/wallet/storage/verifier/pool backends.
- RPC transport security (TLS, hardened bind/auth, secret management) not complete.
- Audit log is in-memory; needs append-only persistent tamper-evident storage.
- Rate limiting is simple in-memory window; needs production gateway/distributed controls.


## Pool

- Pool ledger is in-memory foundation; no persistent DB/audit trail/settlement integration.
- API/WebSocket server, strong auth, rate limiting, withdrawal protections, and evidence validation integration incomplete.

## Security / Operations

- No independent third-party security audit completed.
- No public testnet stability evidence.
- No incident response/runbook/monitoring/alerting package proven.
- No verified backup/restore drills for node/wallet/indexer/storage metadata.
- Mainnet release criteria are not met. Current decision: **NO-GO**.