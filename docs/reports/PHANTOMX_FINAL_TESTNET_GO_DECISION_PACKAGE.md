# PHANTOMX — Final Testnet GO Decision Package

**Date:** 2026-11-26  
**Mode:** PLAN ONLY — no implementation, no experiments, no VPS, no deployment.  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9`, branch `encryption`, dirty 115.  
**Post-fix binary (exec7b):** SHA256 `489a1bcdb403d0aa94aa1100b1980ca191e61fa63563c22b93c5aa6298379293`.  
**Current verdict:** TESTNET NO-GO. VPS FROZEN. TB-001 OPEN/BLOCKED.

---

## 1. Evidence consolidation (gate matrix)

| Gate | Status | Strongest evidence | Location | Scope | Remaining gap | Independently audited | Owner approval needed |
|---|---|---|---|---|---|---|---|
| TB-001 SQLite BR261 | OPEN/BLOCKED | 115 repro, 0 integrity | `/tmp/phantomx_b2r6_out/`, `PHASE_B2R6_*` | Reproduced; root cause LIKELY/UNCONFIRMED | Root cause unconfirmed; no remediation | Yes (exec evidence) | Yes (remediation) |
| TB-002/022 POI Pool | OPEN (exclusion verified) | Pool audit PA-001..PA-011; healthcheck gated | `PHANTOMX_POOL_REMEDIATION_STATUS.md`, `PHANTOMX_POOL_EXCLUSION_VERIFICATION.md` | Audit + exclusion boundary | Hardening deferred; PA-001..PA-011 open | Yes | Yes (scope) |
| TB-003 Deployment/VPS | OPEN | VPS frozen; no live 2-VPS run | `TV_FINAL_READINESS.md` | Not run | Live deployment not authorized | N/A | Yes |
| TB-004 Soak | OPEN | Not run | `TV_FINAL_READINESS.md` | Not run | 14-28 day soak | N/A | Yes (deployment) |
| TB-005 Config/topology | OPEN | Canonical config template exists, not owner-accepted | `config/testnet-bft-canonical.json`, `TV_FINAL_READINESS.md` | Template only | Owner acceptance of topology/keys | Yes | Yes |
| TB-006 Consensus/recovery evidence | PARTIAL | Exec6 3-trial partition + 6/6 instrumented recovery | `PHANTOMX_BFT_RECOVERY_FAILURE_ANALYSIS.md` | Local only | Real 2-VPS equivalence | Partial | No (local) |
| TB-007 Build warnings | PARTIAL | Build now 0 warnings (exec7b) | build logs | Fixed in touched files | Full inventory not done | Partial | No |
| TB-008 Sanitizers/CI | PARTIAL | ASAN/UBSAN/TSan 135/135 PASS (exec2-5) | sanitizer reports | Specific builds | CI coverage/recency | Partial | No |
| TB-009 SQLite observability | OPEN | `sqlite3_extended_result_codes()` not called | `src/database/sqlite_db.cpp` | Confirmed gap | Owner approval for DB change | Yes | Yes |
| TB-010 4-val E2E reproducibility | PARTIAL | E2E smoke PASS (exec6/exec7b) | e2e logs | Local | Fresh reproducible record | Partial | No |
| TB-011 Runbook/ops | PARTIAL | Runbook exists, VPS-only steps untested | `TV_RUNBOOK.md`, deploy/ | Local checks | VPS validation | Partial | No |
| TB-012 Evidence hygiene | IN PROGRESS | Evidence dirs indexed | reports/ | Ongoing | Index consistency | Partial | No |
| TB-013 Harness defects | PARTIAL | Layer B defect fixed in 3B | B2-R6 reports | Scoped | Audit other harnesses | Partial | No |
| TB-014 RPC/DoS hardening | PARTIAL | Rate limit/auth/TLS reviewed partially | `PHANTOMX_RPC_P2P_RELIABILITY_REPORT.md` | Read-only | Full review | Partial | No |
| TB-015 P2P connectivity | PARTIAL | Local multi-node tests pass | partition/dedup evidence | Local only | Live connectivity | Partial | No |
| TB-016 Docs consistency | OPEN | Stale NO-GO statements possible | docs/ | Partial | Careful doc updates | Partial | No |
| TB-017 CI ergonomics | OPEN | Unknown | `.github/workflows/` | Not tested | Inspect CI | No | No |
| TB-018 Report formatting | OPEN | Minor | docs/reports/ | Ongoing | Minor | No | No |
| TB-019 BFT recovery | PARTIAL (Option A approved) | 6/6 instrumented PASS; no false finality; dedup confounder removed | `PHANTOMX_BFT_RECOVERY_FAILURE_ANALYSIS.md`, exec5/6 evidence | Local | Root cause unconfirmed; monitoring required | Yes (exec evidence) | Yes (disposition) |
| TB-020 Partition isolation | PARTIAL | 3 trials on one clean binary: 0 cross-group, no false finality, heal/reconnect converged | `PHANTOMX_BFT_PARTITION_ISOLATION_AUDIT.md`, exec6 v3 | Process/socket-level only | OS-level (firewall/netns) proof | Yes (exec evidence) | Yes (if OS proof needed) |
| TB-021 RPC/P2P | PARTIAL | RPC smoke 13/13; dedup smoke; PoI relay probe PASS; TB021 load 0 errors; timeout/resource probe PASS; adversarial probe PASS | `PHANTOMX_RPC_P2P_RELIABILITY_REPORT.md`, `PHANTOMX_RPC_P2P_TEST_MATRIX.md` | Common paths | AuthZ-on-daemon, full adversarial fuzz, 900k policy, timeout controlled tests | Yes (exec evidence) | No (policy) |
| TB-022 POI Pool | OPEN (exclusion verified) | Pool audit; exclusion verified; healthcheck gated | `PHANTOMX_POOL_REMEDIATION_STATUS.md`, `PHANTOMX_POOL_EXCLUSION_VERIFICATION.md` | Audit + exclusion boundary | Hardening deferred (PA-001..PA-011) | Yes | Yes (scope) |
| TB-023 Network identity | CLOSED (verified) | Cross-network rejection verified; same-network connect; 3 new tests | `PHANTOMX_EXEC7B_NETWORK_IDENTITY_REMEDIATION_REPORT.md` | Daemon-level + unit/network tests | None for initial testnet | Yes | Obtained |

---

## 2. Blocker classification

**A. Mandatory technical blocker before initial testnet**
- TB-005 (canonical 2-VPS config/topology/keys) — operational prerequisite; owner acceptance.
- TB-020 (process-level isolation proven, but OS-level not proven) — acceptable as PARTIAL with documented limitation for isolated testnet; owner must accept process-level evidence or require OS proof.
- TB-019 (bounded risk accepted under Option A) — acceptable with monitoring; remains open for mainnet.

**B. Acceptable bounded risk with owner approval and monitoring**
- TB-019 (Option A approved).
- TB-020 (process-level evidence, OS-level deferred).
- TB-021 (common paths proven; adversarial/authZ/timeout/policy gaps remain as bounded risk with monitoring).

**C. Environment blocker**
- TB-003 (VPS frozen / no live 2-VPS).
- TB-004 (soak not run).
- TB-020 OS-level (requires root-privileged isolated environment).
- TB-017 (CI unknown).

**D. Deferred feature**
- TB-022 POI Pool (excluded from initial testnet; hardening deferred).
- TB-011 (runbook VPS validation deferred).

**E. Deployment-only requirement**
- TB-003, TB-004, TB-005 (deployment/soak/config acceptance).
- TB-011 (VPS runbook validation).

**F. Requires separate owner decision**
- TB-001 (SQLite remediation authorization).
- TB-009 (SQLite observability change).
- TB-021 (900k-param policy).
- TB-005 (canonical topology/keys).
- TB-022 (pool scope).
- TB-019 (disposition — Option A already approved; monitoring acceptance).

---

## 3. TB-001 SQLite BR261 — owner decision package

**Evidence:** 115 confirmed reproductions (C pilot 7 + C esc 97 + B fixed 11), 0 integrity failures. Root cause LIKELY/UNCONFIRMED (concurrent open/init PRAGMA contention). Read-only DB evidence preserved. Extended result code fidelity report exists.

**Failure signature:** `sqlite3_exec PRAGMA journal_mode=WAL`, p=5 e=261, in ChainDatabase/SqliteDb open/init path.

**Production-path relevance:** Affects node startup/recovery reliability; not a consensus-rule change itself.

**Risk if testnet proceeds without remediation:** Intermittent node startup failures under concurrent open/init; no data integrity failure observed in experiments. Operational risk, not safety violation.

**Possible mitigation without production source changes:** monitoring node startup failures, restart automation, avoid concurrent DB open, operational runbook.

**Conditions requiring immediate testnet stop:** any integrity failure, unrecoverable DB, or data loss.

**Three distinct decisions (none presumed approved):**
1. Approval to investigate (read-only root-cause confirmation).
2. Approval to implement (minimal fix + validation).
3. Approval to accept risk (proceed with monitoring).

**Recommendation:** Separate investigation approval first; no implementation without explicit authorization. TB-001 remains OPEN/BLOCKED.

---

## 4. TB-020 OS-level isolation — minimum environment

**Required privileges:** root or CAP_NET_ADMIN in an isolated local environment (not VPS).

**Isolation mechanism:** Linux network namespaces (`ip netns`) + veth pair + iptables/nftables rules.

**Evidence needed:** firewall counters, TCP socket states per namespace, node peer readiness, finality state, zero cross-partition connections, heal/reconnect convergence.

**Test topology:** 2 namespaces (nsA, nsB), veth pair (10.200.1.1/24, 10.200.1.2/24), one phantomxd per namespace, iptables DROP rule simulating partition.

**Acceptance criteria:** zero cross-group TCP connections under kernel enforcement; no ready peers across groups; no false finality; heal converges.

**Safe cleanup:** delete namespaces, remove veth, flush rules.

**Current status:** NOT RUN (no root). Method documented in `PHANTOMX_OS_LEVEL_ISOLATION_METHOD.md` with runnable template script. If unavailable, gate remains PARTIAL/BLOCKED.

---

## 5. TB-021 remaining tests

**Policy 900k request:**
- Why: oversized param values accepted under 1 MiB limit; resource/DoS concern.
- Minimum acceptance: define per-field and per-request size policy; reject or accept with documented rationale.
- Required environment: local node + curl/script.
- Duration: minutes.
- Evidence format: request/response logs, resource measurements.

**Full adversarial P2P fuzzing:**
- Why: validate parser/handshake robustness against malformed inputs.
- Minimum acceptance: bounded fuzz corpus (bad magic, checksum, length, truncation, unknown type, boundary values, slow handshake, oversized input); no crash; no memory exhaustion; no state corruption.
- Required environment: local node + raw socket harness + sanitizer build (optional).
- Duration: hours (bounded).
- Evidence format: corpus, seeds, iteration count, duration, sanitizer findings, crash logs.

**Resource boundary validation:**
- Why: validate connection limits (64), rate limit (500/s), idle timeout (30s), handshake timeout (5s), memory behavior, recovery.
- Minimum acceptance: controlled tests with measured thresholds and recovery.
- Required environment: local node + load harness.
- Duration: minutes to hours.
- Evidence format: metrics, logs, exit codes.

No test execution authorized in this planning task.

---

## 6. TB-019 risk acceptance (Option A)

**Monitoring (required):**
- Finalized height/hash divergence across nodes.
- Ready peer count < N-1 for > 30s.
- Restart catch-up failure (> 60s).
- Conflicting finalized hash (critical).
- Stale finalized metadata.

**Warning thresholds:** finalized height lag ≥ 1 block for > 30s; ready peers < N-1 for > 30s.

**Critical thresholds:** conflicting finalized hash; false finality; any safety violation.

**Escalation:** conflicting finalized hash or false finality → critical incident; invalidates bounded-risk acceptance for affected release.

**Operator actions:** restart node, verify P2P connectivity/peer counts, escalate if no convergence in 60s.

**Recovery procedure:** documented in `PHANTOMX_BFT_MONITORING_ESCALATION.md`.

**Sufficient for initial testnet?** Yes, with monitoring in place and owner acceptance. Root cause remains UNCONFIRMED; TB-019 stays OPEN for mainnet.

---

## 7. TB-022 POI Pool exclusion — confirmed scope

**Verified exclusion (exec7c/7d):**
- No Pool daemon in initial testnet startup.
- No public Pool RPC.
- No nginx Pool route (example only).
- No Pool systemd service enabled (`deploy.sh` commented out).
- `POOL_ENABLED` defaults to 0; healthcheck gates pool check.
- Pool OUT OF SCOPE for initial isolated blockchain testnet.

**Documentation references (not defects):** Pool mentioned in docs/reports and deploy examples with "disabled/secured" qualifiers.

**Stale instructions (minor):** healthcheck.sh previously required pool (fixed in exec7d); README line 24 mentions pool env templates (acceptable, informational).

**Deferred hardening:** PA-001..PA-011 remain OPEN for a future separately approved Pool phase.

**Remaining operator ambiguity:** Ensure runbook explicitly states "POOL OUT OF SCOPE / NOT EXPOSED" for initial testnet profile (minor doc hardening).

---

## 8. Initial testnet architecture (plan only)

**Owner preference:** Two VPS maximum for POI Pool infrastructure (if/when authorized). Blockchain nodes on separate infrastructure. POI Pool excluded from initial testnet. VPS frozen until TESTNET GO.

**Source of truth:** Verified existing configuration (`config/testnet-bft-canonical.json`, `config/testnet.json`, `config/testnet-bft-validation.json`). No new canonical values invented.

**Missing decisions:**
- Canonical 2-VPS topology and validator assignment.
- Operator key management and custody.
- Node count and validator set for initial testnet.
- Canonical config owner acceptance (TB-005).
- Network identity wiring verified (TB-023 closed) — testnet magic `0x54505801`, genesis `98a75772...`.

**No deployment commands. No provisioning.**

---

## 9. Deployment readiness requirements (future phase)

- Infrastructure: VPS provisioning (owner-approved only), separate blockchain node hosts.
- Network topology: canonical config, peer mesh, firewall rules.
- Firewall: `deploy/testnet/firewall/ufw.sh` + OS-level isolation proof.
- Key custody: operator signing keys, backup/restore.
- Secrets management: RPC tokens, TLS certs, env files (outside git).
- Canonical config: owner-accepted `testnet-bft-canonical.json`.
- Network identity: verified (TB-023 closed).
- Monitoring: BFT monitoring/escalation (TB-019 Option A).
- Backup/restore: scripts exist, VPS validation pending.
- Recovery: restart procedures, catch-up verification.
- Rollback: documented procedure.
- Logging: structured logs, evidence capture.
- Security: TLS, auth, rate limits, ban scoring.
- Soak testing: 30 minutes (owner-approved, reduced from 14-28 days). Note: 30-minute soak detects immediate stability issues but cannot detect long-term memory leaks, WAL growth, or resource exhaustion over days/weeks.
- Incident response: escalation matrix, stop conditions.
- Operational acceptance: runbook validation on VPS.

---

## 10. Testnet GO acceptance checklist

| # | Criterion | PASS requirement | Evidence required | Owner | Gate | Formal owner decision? |
|---:|---|---|---|---|---|---|
| 1 | Baseline preserved | HEAD unchanged, no destructive git | git status, HEAD | Engineering | All | No |
| 2 | TB-023 network identity | Closed (verified) | exec7b report, tests | Engineering | Technical | Obtained |
| 3 | TB-019 recovery | Option A accepted + monitoring | Recovery reports, monitoring doc | Owner | Risk acceptance | Yes (obtained) |
| 4 | TB-020 isolation | Process-level proven + OS-level or accepted limitation | Partition audit, OS method doc | Engineering/Owner | Technical + risk | Yes (if OS-level required) |
| 5 | TB-021 RPC/P2P | Common paths + policy decided | RPC/P2P reports, matrix | Engineering | Technical | Yes (policy) |
| 6 | TB-022 Pool exclusion | Exclusion verified | Pool exclusion verification | Owner | Scope | Yes (obtained) |
| 7 | TB-001 SQLite | Remediation authorized or risk accepted | BR261 reports, proposal | Owner | Risk acceptance | Yes |
| 8 | TB-005 Config/topology | Owner-accepted canonical config + keys | Config files, checklist | Owner | Deployment | Yes |
| 9 | TB-003 Live 2-VPS | Deployment executed with evidence | Deployment logs | Owner | Deployment | Yes |
| 10 | TB-004 Soak | 30-minute soak completed (owner-approved, reduced from 14-28 days) | Soak evidence | Owner | Operational | Yes |
| 11 | TB-008 Sanitizers | Fresh ASAN/UBSAN/TSan evidence | Sanitizer logs | Engineering | Technical | No |
| 12 | TB-010 4-val E2E | Fresh reproducible record | E2E logs | Engineering | Technical | No |
| 13 | TB-011 Runbook | VPS-validated | Runbook evidence | Ops | Operational | Yes |
| 14 | TB-014 RPC/DoS | Full review completed | Audit report | Engineering | Technical | No |
| 15 | Independent evidence review | Audit PASS | Review report | Independent reviewer | Governance | Yes |

**Separation of concerns:**
- Technical verification ≠ Risk acceptance ≠ Deployment authorization ≠ TESTNET GO authorization.
- A passing technical audit does not automatically authorize deployment.
- TESTNET GO requires all of the above plus explicit owner authorization.

---

## 11. Final verdict

### Current readiness
NOT READY for TESTNET GO. Multiple gates remain PARTIAL/OPEN; owner decisions outstanding.

### Mandatory blockers before initial testnet
- TB-005 (canonical config/topology/keys) — owner acceptance.
- TB-003/004 (deployment + soak) — owner authorization.
- TB-001 (SQLite remediation or risk acceptance) — owner decision.

### Acceptable bounded risks
- TB-019 (Option A approved, monitoring required).
- TB-020 (process-level isolation, OS-level deferred).
- TB-021 (common paths proven; adversarial/policy gaps bounded).
- TB-022 (Pool excluded).

### Outstanding owner decisions
1. TB-001: remediation authorization or risk acceptance.
2. TB-005: canonical topology/keys acceptance.
3. TB-021: 900k-param policy.
4. TB-020: whether OS-level proof is required or process-level accepted.
5. Deployment authorization (VPS unfreeze).

### Environment blockers
- TB-003/004 (VPS frozen).
- TB-020 OS-level (root unavailable).
- TB-017 (CI unknown).

### Recommended order of operations
1. Owner decisions on TB-001, TB-005, TB-021 policy, TB-020 OS-level requirement.
2. Complete TB-021 adversarial/authZ/timeout matrix (local).
3. Complete TB-008 (sanitizers) and TB-010 (fresh E2E) evidence.
4. Obtain deployment authorization → unfreeze VPS.
5. Execute deployment + soak (TB-003/004/011).
6. Independent evidence review.
7. TESTNET GO decision (explicit owner authorization).

### Earliest conditions for TESTNET GO
All mandatory blockers closed or explicitly accepted as bounded risk; all owner decisions obtained; deployment + soak evidence collected; independent review PASS; explicit TESTNET GO authorization.

---

**Current state: TESTNET NO-GO. VPS FROZEN. TB-001 OPEN/BLOCKED. No deployment authorization.**

Stop after this decision package. No implementation, no experiments, no VPS, no deployment without further owner authorization.