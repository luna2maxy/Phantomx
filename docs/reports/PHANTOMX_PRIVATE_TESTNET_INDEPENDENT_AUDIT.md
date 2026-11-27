# PHANTOMX — Private Testnet Independent Audit

**Date:** 2026-11-26  
**Mode:** Independent evidence review (read-only).  
**Auditor:** Agent (independent from implementation).

## Audit scope
All EXEC2-EXEC7E evidence, reports, source code, tests, and configurations.

## Gate-by-gate audit

| Gate | Status | Evidence reviewed | Verdict | Notes |
|---|---|---|---|---|
| TB-023 Network identity | CLOSED | exec7b smoke, cross-network rejection, 3 new tests, unit/CTest | PASS | No consensus change; canonical values wired |
| TB-019 BFT recovery | PARTIAL | 6/6 instrumented PASS, 0 false finality, monitoring doc | PARTIAL | Root cause unconfirmed; bounded risk accepted |
| TB-020 Partition isolation | PARTIAL | 3 trials, 0 cross-group, no false finality, heal converged | PARTIAL | Process-level only; OS-level not proven |
| TB-021 RPC/P2P | PARTIAL | RPC smoke 13/13, dedup, relay probe, load 0 errors, adversarial probe, timeout/resource probe | PARTIAL | Full fuzzing/authZ-on-daemon/900k policy open |
| TB-022 POI Pool | OPEN (exclusion verified) | Pool audit, healthcheck gate, README update | PASS (exclusion) | PA-001..PA-011 open for future |
| TB-001 SQLite BR261 | PARTIAL | 115 repro, 0 integrity, Alternative F implemented, stress 2000/0 BR261, multi-process validated | PARTIAL | Root cause unconfirmed; independent audit PARTIAL |
| TB-003 Deployment | OPEN | Not run (VPS frozen) | BLOCKED | Environment blocker |
| TB-004 Soak | OPEN | Not run | BLOCKED | Environment blocker |
| TB-005 Config/topology | OPEN | Template exists, not owner-accepted for deployment | PARTIAL | Logical topology approved (exec7 owner decision) |
| TB-006 Recovery evidence | PARTIAL | Exec6 3-trial partition + 6/6 recovery | PARTIAL | Local only |
| TB-007 Warnings | PARTIAL | 0 warnings in touched files | PARTIAL | Full inventory not done |
| TB-008 Sanitizers | PARTIAL | ASAN/UBSAN 137/137, 0 findings | PARTIAL | CI coverage not independently confirmed |
| TB-009 SQLite observability | OPEN | Extended codes not enabled (pre-fix) | OPEN | Now enabled in Alternative F |
| TB-010 4-val E2E | PARTIAL | E2E smoke PASS (exec6/exec7b) | PARTIAL | Fresh reproducible record |
| TB-011 Runbook | PARTIAL | Runbook exists, VPS steps untested | PARTIAL | VPS validation pending |
| TB-014 RPC/DoS | PARTIAL | Rate limit/auth/TLS reviewed partially | PARTIAL | Full review pending |
| TB-015 P2P connectivity | PARTIAL | Local multi-node tests pass | PARTIAL | Live connectivity not tested |
| TB-016 Docs consistency | OPEN | Stale statements possible | PARTIAL | Careful updates needed |
| TB-017 CI | OPEN | Unknown | NOT RUN | Inspect CI |
| TB-018 Report formatting | OPEN | Minor | PARTIAL | Minor |

## Source code audit
- Alternative F: `src/database/sqlite_db.cpp` — reorder + retry + extended codes. ✓
- Dedup: `src/network/node.cpp` — symmetric canonical key. ✓
- Network identity: `src/daemon/main.cpp` — wired canonical values. ✓
- No consensus/genesis/monetary/PoI semantic changes. ✓ (verified via diff review)

## Consensus/genesis change audit
- No changes to: consensus algorithms, BFT finality, QC validation, quorum rules, genesis content, monetary/reward rules, block format, PoI consensus semantics.
- Only non-consensus changes: SQLite open/init (Alternative F), P2P dedup (transport), network identity wiring (config), RPC input validation.

## Evidence integrity
- All raw evidence directories preserved.
- SHA256 inventories generated.
- No evidence overwritten or deleted.
- Independent evidence review (exec7e) PASS.

## Overall verdict
**PARTIAL — NOT READY for TESTNET GO.** TB-023 closed; TB-019/TB-020/TB-021/TB-001 PARTIAL; TB-003/004/005 OPEN. Multiple owner decisions outstanding. VPS FROZEN. No deployment authorization.