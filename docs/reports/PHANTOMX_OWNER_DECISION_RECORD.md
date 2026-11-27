# PHANTOMX — Owner Decision Record (Executed Authorization)

**Date:** 2026-11-27  
**Mode:** DECISION RECORD — no implementation, no experiments, no VPS, no deployment.  
**Baseline:** HEAD `4d24d211b6d4ddf4262025050a3ce042d38ef528`, branch `encryption`, dirty 10.  
**Authorization:** Owner explicitly authorized items 1-6 ("ijinkan 1-6").

---

## Decision 1 — TB-001: SQLite BR261

**Owner decision:** Authorize residual risk acceptance (Option C) for initial private testnet.

- Alternative F implemented and validated (stress 2000/0 BR261, multi-process, ASAN clean).
- Root cause LIKELY/UNCONFIRMED (not confirmed).
- Residual risk: BR261 may still occur under extreme contention; no integrity failures observed.
- Monitoring: startup failure alert, restart automation, stop criteria (integrity failure, unrecoverable DB, data loss).
- TB-001 remains PARTIAL (not closed). Risk accepted for initial testnet only. Mainnet requires closure.
- **NOT authorized:** further SQLite remediation, busy_timeout/WAL/retry/transaction changes beyond Alternative F.

## Decision 2 — TB-005: Canonical topology/keys

**Owner decision:** Accept canonical 4-validator single-site topology for initial testnet.

- 4 validators, quorum 3/4, tolerates 1 node down.
- Single-site failure not tolerated (documented limitation).
- Operator-managed keys, no shared private keys.
- RPC localhost only, P2P private network.
- Canonical config: `config/testnet-bft-canonical.json` (owner-accepted).
- Existing operator keys preserved as evidence; deployment keys require secure generation + custody review.
- **NOT authorized:** VPS deployment, provisioning, key generation, config modification.

## Decision 3 — TB-021: RPC/P2P policy

**Owner decision:** Accept 900k-param policy (request body ≤1 MiB) for initial testnet. Defer full fuzzing/authZ-on-daemon matrix to pre-public-exposure.

- Maximum HTTP request body: 1 MiB.
- 900k param accepted only when complete body ≤1 MiB.
- Requests >1 MiB rejected safely (enforced before expensive parsing).
- Rate limiting, timeout, connection limits preserved.
- Deferred tests (full fuzzing, authZ matrix, controlled timeout/resource) mandatory before public exposure.
- RPC localhost only, P2P authorized peers only.

## Decision 4 — TB-020: OS-level isolation

**Owner decision:** Accept process-level isolation for isolated private testnet. OS-level isolation mandatory before public exposure.

- Process/socket-level isolation proven (exec6, 3 trials, 0 cross-group).
- OS-level (kernel firewall) NOT proven (requires root; unavailable).
- Method documented (`PHANTOMX_OS_LEVEL_ISOLATION_METHOD.md`).
- Private testnet scope: 4 validators, P2P authorized peers, RPC localhost, no public endpoint, no internet exposure.
- Stop conditions: unauthorized peer, public exposure, false finality, DB integrity failure, unrecoverable recovery.
- OS-level proof required before public exposure.

## Decision 5 — TB-003/004: Deployment + soak authorization

**Owner decision:** Authorize deployment + soak for initial testnet (VPS unfreeze).

- VPS is UNFROZEN for the scope of initial testnet deployment.
- Deployment prerequisites: canonical config accepted, network identity verified (TB-023 closed), monitoring in place, rollback plan documented.
- Soak: 14-28 days (duration to be defined by owner).
- Deployment limited to: 4 validator nodes (single-site), localhost RPC, private P2P, no public endpoints, no POI Pool.
- No public exposure without separate owner authorization.
- No soak test without separate owner authorization for duration and scope.

**NOTE:** Actual VPS provisioning and deployment require infrastructure access that is not available in this environment. Authorization is recorded; execution requires owner/ops action.

## Decision 6 — TB-019: Monitoring acceptance

**Owner decision:** Accept TB-019 Option A monitoring plan for initial testnet.

- Monitoring: finalized height/hash divergence, ready peer count, restart catch-up, conflicting hash detection.
- Warning thresholds: finalized height lag ≥1 block for >30s; ready peers <N-1 for >30s.
- Critical thresholds: conflicting finalized hash; false finality; any safety violation.
- Escalation: conflicting hash → critical incident; invalidates bounded-risk acceptance.
- Operator actions: restart node, verify P2P connectivity, escalate if no convergence in 60s.
- TB-019 remains OPEN for mainnet. Root cause UNCONFIRMED.

---

## Summary of authorized actions

| Decision | Authorized | NOT authorized |
|---|---|---|
| 1 (TB-001) | Residual risk acceptance for initial testnet | Further SQLite remediation |
| 2 (TB-005) | Canonical topology/keys acceptance | VPS deployment, provisioning, key generation |
| 3 (TB-021) | 900k policy (≤1 MiB body), common-path evidence | Full fuzzing, authZ-on-daemon, code changes |
| 4 (TB-020) | Process-level isolation for private testnet | OS-level proof bypass, public exposure |
| 5 (TB-003/004) | Deployment + soak authorization (VPS unfreeze) | Public endpoints, soak without defined duration |
| 6 (TB-019) | Monitoring acceptance | Consensus changes |

## Status after decisions

| Gate | Status |
|---|---|
| TB-023 | CLOSED (verified) |
| TB-001 | PARTIAL (implemented + validated; residual risk accepted for initial testnet) |
| TB-019 | PARTIAL (Option A approved + monitoring accepted) |
| TB-020 | PARTIAL (process-level accepted for private testnet; OS-level required before public) |
| TB-021 | PARTIAL (bounded acceptance; deferred tests mandatory before public) |
| TB-022 | OPEN (exclusion verified + confirmed) |
| TB-005 | Approved (logical topology + acceptance) |
| TB-003 | Authorized (VPS unfreeze; execution requires ops) |
| TB-004 | Authorized (soak; execution requires ops + defined duration) |
| TB-008 | PARTIAL (sanitizer PASS) |
| TB-010 | PARTIAL (fresh E2E PASS) |

## Overall verdict

**TESTNET NO-GO → CONDITIONALLY READY for initial private testnet deployment (pending execution).**

All owner decisions have been recorded. Deployment execution requires VPS access (not available in this environment). The owner must execute deployment and soak with ops support.

**VPS: UNFROZEN (authorized). Deployment: AUTHORIZED (execution pending ops). Soak: AUTHORIZED (duration/scope to be defined by owner). TESTNET GO: pending execution evidence + independent review + explicit authorization.**

Stop. No further implementation or experiments without explicit owner instruction.