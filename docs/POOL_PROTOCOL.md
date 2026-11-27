# PhantomX Pool Protocol

## Overview

PhantomX pool manages PoI contributors, verifier workers, and aggregator operations under a pool operator. One operator may hold multiple roles, but identity, evidence, and accounting for each role **must be separated**.

This is a foundation/protocol specification with initial ledger/service implementation. Full production API/WebSocket/dashboard/UI requires additional runtime services.

## Roles

| Role | Description | Reward Source |
|---|---|---|
| Pool operator | Runs pool service, registers workers, manages accounting | 10% block subsidy share for valid pool service (policy-defined) |
| PoI worker | Contributes internet/PoI work | 70% PoI contributor block subsidy share, proportional to validated contribution |
| Verifier worker | Performs verification tasks | 20% verifier block subsidy share for valid, independent tasks only |
| Storage provider | Stores data for clients | User storage fees/contracts, **not** block reward |
| Storage client | Purchases/uses storage | Pays storage fees |

## Role Separation Rules

- Separate identities/keys per role context where applicable.
- Evidence and accounting are role-scoped (`role`, `epoch`, `evidence_hash`, `units`).
- A verifier operator cannot receive verifier reward for self-verification or non-independent tasks.
- A pool operator contributing internet receives contributor share only based on actual validated contribution.
- Storage provider payments come from storage contracts/settlement, not block subsidy.

## Pool Features (Foundation)

| Feature | Status | Notes |
|---|---|---|
| Worker registration | Implemented foundation | `PoolLedger::register_worker`, unique worker ID, operator ID, role, auth token hash |
| Authentication | Implemented foundation | Hash-based token check; production must use strong auth, TLS, rate limiting |
| Contribution submission | Implemented foundation | `submit_share` with role/worker validation |
| Evidence validation | Partial | Requires evidence hash; full cryptographic validation integrates PoI/verifier evidence modules |
| Verifier task management | Protocol/foundation | Task assignment should reuse verifier assignment/service with independence checks |
| Storage provider registration | Protocol/foundation | Register as `kStorageProvider`; storage capacity/lease uses storage modules |
| Storage assignment | Protocol/foundation | Uses storage node registry/lease/replication modules |
| Share accounting | Implemented foundation | Epoch/role/work-unit share records, canonical aggregation |
| Reward estimation | Implemented foundation | Integer floor estimate by role/category atoms |
| Payout | Implemented foundation | Payout records with duplicate payout prevention |
| Worker dashboard | Implemented foundation | Worker share units, estimated reward, paid atoms |
| Pool dashboard | Implemented foundation | Workers, active workers, total shares, total paid atoms |
| API | Protocol defined | REST/JSON endpoints recommended; not full runtime server in this foundation |
| WebSocket | Protocol defined | Event stream for share/payout/task updates; not full runtime server in this foundation |

## Suggested API / WebSocket (Protocol)

### API (authenticated)

- `POST /v1/workers/register` - register worker with role/operator identity.
- `POST /v1/workers/auth` - authenticate worker/session.
- `POST /v1/shares` - submit contribution share with evidence reference.
- `GET /v1/dashboard/pool` - pool dashboard.
- `GET /v1/dashboard/worker/:id` - worker dashboard.
- `GET /v1/rewards/estimate?epoch=&worker_id=&role=` - reward estimate.
- `POST /v1/payouts/claim` - claim/record payout (with chain/settlement validation).

### WebSocket

- `worker.registered`
- `share.accepted`
- `share.rejected`
- `task.assigned`
- `reward.estimated`
- `payout.recorded`

All API/WS endpoints must enforce authentication, authorization, rate limiting, audit logging, and input validation.

## Reward Rules

- Pool earns 10% block subsidy only for valid pool service as defined by consensus/policy.
- Verifier operator earns verifier share only for valid, independent verification tasks.
- Contributor earns PoI share based on actual validated contribution.
- Storage provider is paid from storage contracts/fees; no block reward emission for storage.

## Security Controls

| Control | Foundation | Production Requirement |
|---|---|---|
| No self-verification | Role/evidence separation policy | Enforce with verifier assignment conflict checks and signed evidence |
| No duplicate shares | Same worker+epoch+role+evidence rejected | Persist in DB, include evidence nullifier/index |
| No fake workers | Registration + auth hash required | Strong identity keys, challenge-response auth, Sybil controls |
| No duplicate payout | Payout key `(worker,epoch,category)` | Persisted ledger, on-chain settlement/nullifier |
| Rate limiting | Not implemented in core | API/WS gateway rate limits |
| API authentication | Hash check foundation | mTLS/token/JWT/HMAC + rotation + revocation |
| Withdrawal protection | Not implemented in core | Withdrawal limits, approvals, delays, on-chain settlement checks |
| Accounting audit trail | In-memory records | Append-only DB/log, signed events, exportable audits |

## Files

- `include/phantomx/pool/pool.hpp`
- `src/pool/pool.cpp`
- `apps/phantomx-pool/main.cpp`
- `tests/unit/test_pool.cpp`
- `docs/POOL_PROTOCOL.md`
