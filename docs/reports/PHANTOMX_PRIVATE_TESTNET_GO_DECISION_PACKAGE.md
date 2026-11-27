# PHANTOMX — Private Testnet GO Decision Package

**Date:** 2026-11-26  
**Mode:** PLAN ONLY — no implementation, no deployment, no VPS.  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9` (original) / `4d24d21` (current, auto-committed).  
**Binary:** SHA256 `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`.

---

## 1. Executive summary

PHANTOMX has completed significant technical remediation (Alternative F SQLite fix, P2P dedup, network identity wiring, RPC validation, partition isolation proof, PoI relay verification). TB-023 is CLOSED. However, multiple gates remain PARTIAL or OPEN, and several owner decisions are outstanding. **PRIVATE TESTNET GO is NOT recommended at this time.**

## 2. Trusted baseline and Git incident

- Original baseline: `d094596549a53eb0695527df06729eca41e737e9` (dangling, preserved via `recovery` branch).
- Current HEAD: `4d24d21` (auto-committed, unauthorized). Forensic audit: `PHANTOMX_GIT_STATE_INCIDENT_FORENSIC_AUDIT.md`.
- Recovery branch created (non-destructive). Owner must decide on baseline restoration.

## 3. Final source and binary identity

- Post-fix binary SHA256: `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`.
- Modified files (approved): `src/database/sqlite_db.cpp`, `src/network/node.cpp`, `src/daemon/main.cpp`, `include/phantomx/network/peer.hpp`, `tests/network/test_p2p.cpp`, `src/rpc/http_server.cpp`, `src/rpc/node_handlers.cpp`, `tests/unit/test_poi.cpp`.
- No consensus/genesis/monetary/PoI semantic changes.

## 4. All gate statuses

| Gate | Status | Evidence |
|---|---|---|
| TB-001 SQLite BR261 | PARTIAL (implemented + validated) | `PHANTOMX_TB001_*` reports, stress tests |
| TB-003 Deployment | OPEN | VPS frozen |
| TB-004 Soak | OPEN | Not run |
| TB-005 Config/topology | OPEN (logical approved) | `config/testnet-bft-canonical.json`, owner decision |
| TB-019 BFT recovery | PARTIAL (Option A approved) | Recovery reports, monitoring doc |
| TB-020 Partition isolation | PARTIAL (process-level proven) | Partition audit, 3 trials |
| TB-021 RPC/P2P | PARTIAL (common paths proven) | RPC/P2P reports, matrix |
| TB-022 POI Pool | OPEN (exclusion verified) | Pool remediation + exclusion verification |
| TB-023 Network identity | CLOSED (verified) | Exec7b report, tests |

## 5. TB-001 decision and residual risk

- Alternative F implemented and validated (multi-process: 0 BR261 in 2000 opens).
- Root cause LIKELY/UNCONFIRMED.
- Independent audit: PARTIAL (6/8 acceptance criteria).
- Residual risk: BR261 may still occur under extreme contention; no integrity failures observed.
- Monitoring and stop criteria documented.

## 6. TB-005 topology verification

- 4 validators, single-site, quorum 3/4, tolerates 1 node down.
- RPC localhost only. P2P private network.
- Single-site failure not tolerated (documented limitation).
- Operator keys: existing canonical keys as evidence; deployment keys require secure generation.
- Canonical config: `config/testnet-bft-canonical.json` (not yet owner-accepted for deployment).

## 7. TB-019 BFT recovery

- 6/6 instrumented recovery runs PASS. 0 false/conflicting finality.
- Option A approved: bounded risk with monitoring and escalation.
- Root cause UNCONFIRMED. Remains OPEN for mainnet.
- Monitoring plan documented (`PHANTOMX_BFT_MONITORING_ESCALATION.md`).

## 8. TB-020 isolation

- Process/socket-level isolation proven (3 trials, 0 cross-group).
- OS-level (kernel firewall) NOT proven (requires root).
- Method documented (`PHANTOMX_OS_LEVEL_ISOLATION_METHOD.md`).
- TB-023 network identity adds application-layer cross-network rejection.

## 9. TB-021 RPC/P2P

- RPC smoke 13/13 PASS. Dedup smoke PASS. PoI relay probe PASS. TB021 load 0 errors. Timeout/resource PASS. Adversarial probe PASS.
- AuthZ unit-tested. 900k policy documented (≤1 MiB body).
- Deferred: full fuzzing, authZ-on-daemon matrix, controlled timeout/resource tests (mandatory before public exposure).

## 10. TB-022 POI Pool exclusion

- Excluded from initial testnet. No daemon/RPC/nginx/systemd/endpoint.
- `POOL_ENABLED` defaults to 0. Healthcheck gated.
- PA-001..PA-011 OPEN for future phase.

## 11. TB-023 network identity

- CLOSED (verified). Canonical testnet magic `0x54505801` + genesis `98a75772...` wired. Cross-network rejection verified. 3 new tests PASS.

## 12. Build and regression evidence

- Build: EXIT=0, warnings 1 (pre-existing). Unit 137/137. CTest 11/11. Network 4/4. E2E PASS. ASAN 137/137 (0 findings).
- Binary SHA256: `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`.

## 13. Four-node E2E evidence

- E2E 4-node smoke PASS (all finalized height 1, same hash, consensus_mode=bft, finality_status=finalized).
- Partition matrix: 3 trials, 0 cross-group, no false finality, heal/reconnect converged.
- PoI relay probe: PASS=true.

## 14. Operational runbook

- Created: `docs/operations/PRIVATE_TESTNET_RUNBOOK.md`, `PRIVATE_TESTNET_TROUBLESHOOTING.md`, `PRIVATE_TESTNET_RECOVERY.md`.
- VPS validation pending (VPS frozen).

## 15. Independent audit

- `PHANTOMX_PRIVATE_TESTNET_INDEPENDENT_AUDIT.md`: gate-by-gate review, PASS/PARTIAL/FAIL/UNVERIFIED/BLOCKED.
- Multiple gates PARTIAL or OPEN. Not ready for GO.

## 16. Remaining limitations

- TB-001 root cause unconfirmed (LIKELY).
- TB-020 OS-level isolation unproven.
- TB-021 full fuzzing/authZ/policy open.
- TB-003/004/005 deployment/soak/config pending owner.
- TB-007/TB-008/TB-010/TB-011/TB-014/TB-015/TB-016/TB-017/TB-018 partially open.
- Shutdown/join lifecycle residual (theoretical).
- Config strings not wired to P2P (uses authoritative genesis_block-derived values).

## 17. Explicit owner decisions required

1. TB-001: remediation authorization or risk acceptance (3 distinct decisions).
2. TB-005: canonical topology/keys acceptance.
3. TB-021: 900k-param policy.
4. TB-020: whether OS-level proof is required or process-level accepted.
5. TB-003: deployment authorization (VPS unfreeze).
6. TB-004: soak authorization.
7. TB-019: monitoring acceptance.
8. Git incident: baseline restoration approach.

## 18. PRIVATE TESTNET GO recommendation

**NOT READY.** Multiple gates PARTIAL or OPEN. Owner decisions outstanding. No deployment authorization. VPS FROZEN.

**Earliest conditions for PRIVATE TESTNET GO:**
1. TB-001 remediation or risk acceptance (owner decision).
2. TB-005 canonical config/keys acceptance (owner decision).
3. TB-020 OS-level or accepted limitation (owner decision).
4. TB-021 policy decision (owner decision).
5. Deployment authorization (owner decision).
6. Independent evidence review PASS.
7. Explicit PRIVATE TESTNET GO authorization (owner).

---

**Current state: TESTNET NO-GO. VPS FROZEN. TB-001 OPEN/BLOCKED (PARTIAL after implementation). No deployment authorization.**

Stop after this decision package. No implementation, no experiments, no VPS, no deployment without further owner authorization.