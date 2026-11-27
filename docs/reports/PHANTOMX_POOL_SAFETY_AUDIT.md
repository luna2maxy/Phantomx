# PHANTOMX — POI Pool Read-Only Safety Audit

**Date:** 2026-11-26  
**Scope:** Read-only code audit (no deployment). Files inspected: `apps/phantomx-pool/main.cpp`, `src/pool/main.cpp`, `src/pool/pool.cpp`, `include/phantomx/pool/pool.hpp`, `src/rpc/node_handlers.cpp` (pool RPCs), `tests/unit/test_pool.cpp`, deploy templates.  
**Verdict:** **NOT SAFE FOR PUBLIC EXPOSURE**.

## Summary

The pool is an in-memory library with a stub daemon and limited RPC handlers. It lacks authentication/authorization for most operations, persistence/recovery, rate limiting, resource bounds, and evidence verification. Public exposure would be high risk.

## Findings

| ID | Severity | Area | Finding | Evidence | Consensus impact | Recommendation |
|---|---|---|---|---|---|---|
| PA-001 | BLOCKER | Deployment | `apps/phantomx-pool/main.cpp` and `src/pool/main.cpp` are stubs (parse args, log, exit). No API/service. | `apps/phantomx-pool/main.cpp:1-55`, `src/pool/main.cpp:1-32` | None | Do not deploy. Complete implementation in approved scope. |
| PA-002 | HIGH | AuthZ | `pool_register_worker` has no authentication/authorization; any RPC caller can register workers. | `src/rpc/node_handlers.cpp:2070-2089` | None | Require operator/admin auth or signed worker registration; rate limit. |
| PA-003 | HIGH | AuthZ | `pool_submit_share` / `submitpoievidence` have no worker authentication; only worker_id is supplied. | `node_handlers.cpp:2089-2205` | None | Require worker auth (token/signature) bound to worker_id. |
| PA-004 | HIGH | Validation | `std::stoull`/`stoul` used directly on user input; invalid input throws `std::exception`, potentially causing unhandled error/DoS depending on RPC wrapper. | `node_handlers.cpp:2099,2100,2161,2162,2194` etc. | None | Parse with checked conversion and return invalid-param error. |
| PA-005 | HIGH | Persistence | `PoolLedger` is in-memory only (`workers_`, `shares_`, `payouts_` vectors/maps). Restart loses all state. | `pool.hpp:70-88`, `pool.cpp` | None | Add durable storage/recovery before any real use. |
| PA-006 | HIGH | Resource limits | No bounds on workers/shares/payouts/evidence_hash length/units/epoch range. Memory exhaustion possible. | `pool.cpp`, handlers | None | Add max sizes, max items, string length limits, rate limiting. |
| PA-007 | MEDIUM | Evidence verification | `submit_share` requires evidence_hash_hex non-empty but does not verify evidence, signature, or PoI validity. `submitpoievidence` computes score from self-reported metrics. | `pool.cpp:50-69`, `node_handlers.cpp:2175-2205` | None | Treat as untrusted until verified by approved PoI verification path. |
| PA-008 | MEDIUM | AuthN weakness | `register_worker` defaults `auth_token_hash` to "none" if missing. `authenticate` compares plain hash string equality (no HMAC/timing-safe compare noted). | `node_handlers.cpp:2083`, `pool.cpp:35-47` | None | Require strong auth; avoid default "none"; use constant-time compare for secrets. |
| PA-009 | MEDIUM | Rate limiting | No pool-specific rate limiting in handlers. | node handlers | None | Add per-IP/per-worker rate limits. |
| PA-010 | MEDIUM | Observability | No audit log for worker registration/share submission/payout recording beyond RPC result. | pool code | None | Add structured audit events. |
| PA-011 | LOW | API semantics | `pool_getworker` estimates reward using `block_subsidy_atoms(1)` not requested epoch/category; may mislead. | `node_handlers.cpp:2129` | None | Clarify/document or fix calculation inputs. |

## Auto-fix eligibility

- PA-004 (stoull throwing) may be eligible as isolated non-consensus RPC validation fix with regression test. Needs careful patch in node_handlers.
- PA-002/003/005/006 require design decisions/scope; not auto-fixed.
- No fixes applied in this audit pass.

## Public exposure

**Do not expose POI Pool publicly.** Keep daemon stub non-listening/publicly inaccessible. Any future deployment requires auth, persistence, bounds, audit, and verification design review.