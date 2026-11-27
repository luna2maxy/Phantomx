# PhantomX Mainnet Guide

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Decision

**GO/NO-GO: NO-GO (Mainnet).**

Decision is based on current evidence, not assumptions. Mainnet launch is **not approved**.

## Why NO-GO

Current repository contains substantial foundation code and docs, but mainnet readiness is not proven. Critical evidence is missing:

- Consensus spec final: **not finalized/reviewed externally**.
- PoI+Verifier threat model reviewed: **internal draft only, no external formal review**.
- Supply invariant proven: **unit tests exist, but no formal proof / independent verification / CI-green evidence in this assessment**.
- Storage integrity tested: **foundation tests exist, but no adversarial/end-to-end/public testnet evidence**.
- Public testnet stable: **not proven**.
- Genesis independently verified: **not proven**.
- Reproducible builds available: **not proven**.
- Backup and recovery tested: **not proven**.
- Documentation complete: **partial/foundation; operational runbooks incomplete**.
- Security audit complete: **not complete; no independent third-party audit**.

## Do Not Launch Mainnet Before

- [ ] Consensus spec final and versioned.
- [ ] PoI+Verifier threat model reviewed by qualified external reviewers.
- [ ] Supply invariant proven (formal + test vectors + multi-node validation).
- [ ] Storage integrity tested (challenge, corruption, missing data, collusion, recovery).
- [ ] Public testnet stable for sustained period with published metrics.
- [ ] Genesis verified independently by multiple parties.
- [ ] Reproducible builds available and verified.
- [ ] Backup and recovery tested (node, wallet, indexer, storage metadata).
- [ ] Documentation complete (operator, security, incident response, upgrade, RPC, wallet, storage).
- [ ] Independent security audit completed and critical/high findings resolved.

## Mainnet Launch Prerequisites

### Technical
- CI green on Windows/Linux/macOS for Debug+Release.
- Deterministic test vectors for genesis, tx, merkle, address, consensus certificates, reward commitments.
- Multi-node adversarial simulations pass.
- Crash recovery/fuzz/property tests pass.
- Monitoring/alerting/runbooks ready.

### Security
- External audit report published (or responsibly disclosed summary).
- Key management and RPC hardening reviewed.
- Incident response and emergency patch process tested.

### Governance/Operations
- Release signing keys and reproducible build process documented.
- Genesis ceremony and verification artifacts published.
- Rollback/halts/upgrade policy documented.

## GO Criteria (Future)

Mainnet may move to **GO** only when every checklist item above has verifiable evidence, owners, dates, and artifacts (logs, reports, hashes, signatures, test results). Absence of evidence is NO-GO.