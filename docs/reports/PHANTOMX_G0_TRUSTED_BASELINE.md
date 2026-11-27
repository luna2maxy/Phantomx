# PHANTOMX — G0 Trusted Baseline Verification

**Date:** 2026-11-27  
**Mode:** READ-ONLY. No Git mutations, no code changes.  
**Evidence dir:** `/tmp/phx_g0_20261127/`

## 1. Git state

| Item | Value |
|---|---|
| Branch | `encryption` |
| HEAD | `4d24d211b6d4ddf4262025050a3ce042d38ef528` |
| HEAD commit message | "Update" |
| Working tree | 1 untracked file (`PHANTOMX_GIT_STATE_INCIDENT_FORENSIC_AUDIT.md`) |
| Dirty count | 1 (untracked report) |

## 2. Original baseline integrity

| Item | Expected | Observed | Status |
|---|---|---|---|
| Original HEAD | `d094596549a53eb0695527df06729eca41e737e9` | Object exists (dangling commit) | PRESERVED |
| Recovery branch | N/A | `recovery` → `d0945...` | CREATED (non-destructive) |
| Branch `work-preserved` | N/A | → `4d24d21` | CREATED (non-destructive) |

**Original baseline `d0945...` is a dangling commit, recoverable via `recovery` branch.**

## 3. Commit discrepancy

The current HEAD (`4d24d21`) is NOT the original baseline (`d0945...`). Two unauthorized auto-commits occurred:
- `c98f354` "tes" (Nov 27 00:27:46 +0700) — 458 files, 56,493 insertions.
- `4d24d21` "Update" (Nov 27 00:45:09 +0700) — 5 files, 757 insertions.

**These commits were NOT authorized by the owner.** They appear to have been created by an automated process (possibly the OpenCode harness). The agent did not execute `git commit`.

The original baseline `d0945...` is preserved via the `recovery` branch. The current HEAD `4d24d21` contains all the agent's work (Alternative F, dedup, network identity, tests, reports).

## 4. Source identity

### Code changes present in working tree
- `src/database/sqlite_db.cpp` — Alternative F (2 markers)
- `src/network/node.cpp` — P2P dedup (1 marker)
- `src/daemon/main.cpp` — Network identity (1 marker)
- `include/phantomx/network/peer.hpp` — local_port field
- `tests/network/test_p2p.cpp` — 3 new identity tests
- `tests/unit/test_poi.cpp` — (pre-existing from prior execs)
- `src/rpc/http_server.cpp` — (pre-existing from prior execs)
- `src/rpc/node_handlers.cpp` — (pre-existing from prior execs)

### Reports present
- All EXEC2-EXEC7 reports in `docs/reports/`
- New: `PHANTOMX_EXEC7B_NETWORK_IDENTITY_REMEDIATION_REPORT.md`, `PHANTOMX_EXEC7C_REPORT.md`, `PHANTOMX_EXEC7E_FINAL_READINESS_GAP_CLOSURE_REPORT.md`, `PHANTOMX_FINAL_TESTNET_GO_DECISION_PACKAGE.md`, `PHANTOMX_TB001_*` reports, `PHANTOMX_NETWORK_IDENTITY_*`, `PHANTOMX_P2P_POST_HEAL_RECOVERY_ANALYSIS.md`, `PHANTOMX_RPC_P2P_TEST_MATRIX.md`, `PHANTOMX_BFT_MONITORING_ESCALATION.md`, `PHANTOMX_OS_LEVEL_ISOLATION_METHOD.md`, `PHANTOMX_POOL_EXCLUSION_VERIFICATION.md`, `PHANTOMX_GIT_STATE_INCIDENT_FORENSIC_AUDIT.md`, `PHANTOMX_TB001_EVIDENCE_DISCREPANCY_RECONCILIATION.md`, `PHANTOMX_TB001_FINAL_MULTIPROCESS_EVIDENCE_AUDIT.md`, `PHANTOMX_TB001_MULTIPROCESS_PREPOST_VALIDATION.md`.

## 5. Binary identity

- Build: `build-ubuntu/bin/phantomxd`
- SHA256: `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`
- Size: 34509168
- Build: EXIT=0, warnings 1 (pre-existing unused `get_port`)
- Unit: 137/137 PASS
- CTest: 11/11 PASS
- Network: 4/4 PASS
- E2E: PASS

## 6. Evidence integrity

All evidence directories preserved:
- `/tmp/phx_testnet_exec2_20261126/` through `/tmp/phx_testnet_exec7e_20261126/`
- `/tmp/phx_tb001_impl_20261126/`, `/tmp/phx_tb001_multiproc_20261126/`
- `/tmp/phx-b1r-t2/` (read-only DB evidence)

No evidence was modified or deleted.

## 7. Differences from official baseline

The current state differs from the original baseline in the following ways:
1. **Git history:** Two unauthorized auto-commits (`c98f354`, `4d24d21`) on top of `4759cf2` (Initial commit), diverging from the original baseline's commit chain.
2. **Code changes:** Alternative F (sqlite_db.cpp), P2P dedup (node.cpp, peer.hpp), network identity (main.cpp), 3 new tests (test_p2p.cpp) — all approved scope.
3. **Reports:** 20+ new/updated reports in `docs/reports/`.
4. **Config:** `config/testnet-bft-canonical.json`, `config/testnet-bft-validation.json` (untracked, now committed).
5. **Deploy:** `deploy/` directory (untracked, now committed).

## 8. Recommendation

**The original baseline `d0945...` is preserved via the `recovery` branch and as a dangling commit.** The current HEAD `4d24d21` contains all approved work. The git incident (unauthorized auto-commits) is documented in `PHANTOMX_GIT_STATE_INCIDENT_FORENSIC_AUDIT.md`.

**No recovery action taken.** The `recovery` branch makes the original baseline reachable. The owner must decide whether to:
- Accept `4d24d21` as the new working baseline, or
- Restore `d0945...` via `git reset --hard d0945...` (destructive, requires authorization).

## 9. Next phase

Proceed to Phase 1 (TB-001 residual evidence investigation) as authorized by owner (Option B approved). No new git operations. No experiments without authorization.

**G0 Status: PARTIAL — baseline discrepancy documented, original baseline preserved via `recovery` branch.**