# PhantomX Release Checklist

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Release Decision

**Current decision: NO-GO (Mainnet)**  
**Testnet/public release readiness: NOT PROVEN**

Decision rule: **GO only with evidence.** Any missing evidence for a critical item = NO-GO.

## Evidence Legend

- `PASS` - verified with artifact link/hash/log
- `FAIL` - verified failure
- `N/A` - justified not applicable
- `UNVERIFIED` - no evidence

## 1. Build & CI

| Item | Status | Evidence | Owner |
|---|---|---|---|
| Clean Debug build (Win/Linux/macOS) | UNVERIFIED | No local compiler; CI not reverified in this assessment | Release Eng |
| Clean Release build (Win/Linux/macOS) | UNVERIFIED | Same as above | Release Eng |
| Unit tests pass | UNVERIFIED | Not run in this environment | QA |
| Integration tests pass | UNVERIFIED | Not run | QA |
| Network/P2P tests pass | UNVERIFIED | Not run | QA |
| Smoke tests pass | UNVERIFIED | Not run | QA |
| Reproducible builds | UNVERIFIED | No reproducible build artifacts | Release Eng |

## 2. Consensus / PoI+Verifier / PoI

| Item | Status | Evidence | Owner |
|---|---|---|---|
| Consensus spec final | UNVERIFIED | Draft/foundation docs only | Protocol |
| PoI+Verifier threat model external review | UNVERIFIED | No external review | Security |
| PoI anti-manipulation tests | UNVERIFIED | Foundation tests only | Protocol/QA |
| Equivocation/slashing policy enforced | UNVERIFIED | Detection foundation; enforcement incomplete | Protocol |
| Partition/adversarial simulation | UNVERIFIED | Not run | QA |
| Finality recovery/crash tests | UNVERIFIED | Not run | QA |

## 3. Storage / Marketplace

| Item | Status | Evidence | Owner |
|---|---|---|---|
| Storage proof integrity | UNVERIFIED | Foundation tests only | Storage |
| Corruption/missing chunk handling | UNVERIFIED | Not adversarially proven | Storage/QA |
| Replication/repair | UNVERIFIED | Foundation only | Storage |
| User-funded storage settlement | UNVERIFIED | Escrow foundation only | Storage/Econ |
| No plaintext/key leakage | UNVERIFIED | Design rules; needs audit | Security |

## 4. Tokenomics / Rewards

| Item | Status | Evidence | Owner |
|---|---|---|---|
| Halving/supply invariant | UNVERIFIED | Unit tests exist; no independent proof/CI evidence here | Econ/Protocol |
| No double claim | UNVERIFIED | In-memory foundation; persistence/integration needed | Protocol |
| Pool/verifier abuse controls | UNVERIFIED | Foundation only | Protocol/Security |
| Integer/fixed-point audit | UNVERIFIED | No formal audit/fuzz report | Security |

## 5. Wallet / RPC / Apps

| Item | Status | Evidence | Owner |
|---|---|---|---|
| Wallet key/seed protection | UNVERIFIED | Foundation; encrypted keystore/secure enclave incomplete | Wallet/Security |
| RPC auth/TLS/hardening | UNVERIFIED | Auth/rate-limit foundation only | Infra/Security |
| Mobile light-client trust mitigations | UNVERIFIED | Docs/skeleton only | Mobile/Security |

## 6. Operations / Release

| Item | Status | Evidence | Owner |
|---|---|---|---|
| Testnet acceptance 14/14 | UNVERIFIED | No evidence | Release |
| Genesis independently verified | UNVERIFIED | No artifact | Release/Protocol |
| Backup/recovery drills | UNVERIFIED | No evidence | Ops |
| Monitoring/alerts/runbooks | UNVERIFIED | No evidence | Ops |
| Independent security audit | UNVERIFIED | Not completed | Security |
| Final GO/NO-GO signoff | NO-GO | Missing evidence across critical items | Release Board |

## Signoff

- Protocol: ☐ GO ☐ NO-GO
- Security: ☐ GO ☐ NO-GO
- QA: ☐ GO ☐ NO-GO
- Ops: ☐ GO ☐ NO-GO
- Release: ☐ GO ☐ NO-GO

**Current aggregate: NO-GO.**