# PHANTOMX — POI Pool Remediation Status

**Date:** 2026-11-26  
**Scope:** Read-only audit + eligible non-consensus fixes only. No deployment.  
**Audit:** `docs/reports/PHANTOMX_POOL_SAFETY_AUDIT.md`

## Finding status

| ID | Severity | Finding | Status | Action/Evidence |
|---|---|---|---|---|
| PA-001 | BLOCKER | Stub daemon, no real API/service | OPEN / OWNER SCOPE | No deployment. Requires approved implementation scope. |
| PA-002 | HIGH | `pool_register_worker` missing authZ | OPEN | Design decision needed (operator auth/signed registration). |
| PA-003 | HIGH | `pool_submit_share`/`submitpoievidence` missing worker auth | OPEN | Design decision needed. |
| PA-004 | HIGH | Unchecked `stoull/stoul` throws on invalid input | **PARTIAL CLOSED (scoped)** | Pool handlers guarded in `src/rpc/node_handlers.cpp`; regression `ptm_pool_invalid_numeric_params_rejected_cleanly` PASS. Other non-pool stoull sites remain audit items. |
| PA-005 | HIGH | In-memory only, no persistence/recovery | OPEN | Architecture decision needed. |
| PA-006 | HIGH | No resource bounds/rate limits | OPEN | Design/limits needed. |
| PA-007 | MEDIUM | No evidence verification | OPEN | Depends on PoI verification path/scope. |
| PA-008 | MEDIUM | Default "none" auth hash / weak auth | OPEN | Design decision needed. |
| PA-009 | MEDIUM | No rate limiting | OPEN | Needs limits design. |
| PA-010 | MEDIUM | Limited audit observability | OPEN | Non-consensus improvement candidate. |
| PA-011 | LOW | Reward estimate semantics | OPEN | Doc/fix candidate. |

## Initial isolated blockchain testnet exclusion boundary (TB-022 design decision)

- **Recommendation:** Exclude POI Pool from initial isolated blockchain testnet exposure. Pool is not required to validate core BFT/chain/RPC/P2P behavior for a private isolated testnet.
- **If excluded:** no public/listening pool service, no pool RPC exposure beyond disabled/internal, no nginx/systemd pool deployment, docs/runbook must state "POOL OUT OF SCOPE / NOT EXPOSED". Requires explicit owner acceptance.
- **If included:** must resolve PA-001..PA-011 to acceptance criteria (real daemon/API, authZ/authN, persistence/recovery, bounds/rate limits, evidence verification, audit logging, regression tests, security review). Owner scope/design decision required.
- **Current status:** NOT SAFE FOR PUBLIC EXPOSURE. No deployment performed.

## Owner decision required (TB-022)

1. **Accept exclusion (recommended):** record POI Pool as OUT OF SCOPE for the initial isolated blockchain testnet; no pool daemon, RPC surface, nginx/systemd unit, or public docs. Core BFT/chain/RPC/P2P validation does not depend on the pool.
2. **Or authorize full hardening:** approve scope to resolve PA-001..PA-011 (real daemon/API, authZ/authN, persistence/recovery, bounds/rate limits, evidence verification, audit logging, security review, regression tests) before any exposure.

No option is selected yet; the pool remains non-deployed and non-exposed. Exec6 added no pool code changes.

## Numeric-parsing note (PA-004)

`RpcServer::call` (`src/rpc/rpc.cpp:103-110`) catches `std::exception` from handlers and returns JSON-RPC `-32603`. Remaining unguarded `std::stoull/std::stoul` sites therefore cannot crash the process; they return a generic internal error. Pool-facing handlers have explicit guarded parsing with clean invalid-param errors. A future pass may convert remaining sites to `-32602` for usability, but this is not a stability blocker.