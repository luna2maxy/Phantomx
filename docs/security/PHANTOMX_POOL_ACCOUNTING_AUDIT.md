# PhantomX Pool Accounting Audit

> Status: INVESTIGATING  
> As of: 2026-11-26  
> Scope: `submitpoievidence` -> `PoolLedger` -> payout/aggregation paths. Not consensus settlement.

## Data Flow

1. **RPC `submitpoievidence`** (`src/rpc/node_handlers.cpp:1915`)  
   - Auth: RPC token only. No submitter identity/owner proof. No worker ownership binding.  
   - Input: `worker_id`, `epoch`, `evidence_hash`, bandwidth/uptime/latency/relay milli (clamped by `clamp_score`).  
   - Computes `compute_poi_score`; creates `ShareRecord{worker_id, role=kPoiWorker, epoch, units=score.total||1, evidence_hash_hex}`.  
   - Calls `pool.submit_share(s)`.

2. **`PoolLedger::submit_share`** (`src/pool/pool.cpp:44`)  
   - Requires worker registered and active; role match; units > 0; evidence_hash non-empty.  
   - Rejects duplicate share by exact `(epoch, worker_id, role, evidence_hash_hex)`.  
   - **No ownership proof at submit time**; relies on worker registration/auth token model, but RPC handler does not authenticate submitter to worker.  
   - **No epoch freshness/finality bound**, no per-worker rate limit, no evidence signature/proof.

3. **`PoolLedger::record_payout`** (`src/pool/pool.cpp:64`)  
   - Rejects zero amount, epoch > finalized_epoch, duplicate payout key `(worker_id,epoch,category)`.  
   - Payout marked paid and stored.

4. **RPC `pool_record_payout`** (`src/rpc/node_handlers.cpp:1895`)  
   - Auth: RPC token only. No admin/operator/worker ownership proof.  
   - Uses `g_poi_finalized_height.load()` as finalized_epoch gate.  
   - Records payout in pool ledger; does **not** create on-chain transaction/consensus settlement.

## Trust Boundary

| Layer | Trust | Controls | Gap |
|---|---|---|---|
| Submitted evidence | Untrusted | score clamping, parse checks | no submitter auth, no proof binding owner/machine/epoch |
| Pool share | Semi-trusted ledger entry | worker registered/active, role match, duplicate share key | no caller auth to worker; duplicate possible with different evidence hash |
| Estimated reward | Derived | integer math | depends on share integrity |
| Recorded payout | Pool accounting record | finalized_epoch gate, duplicate payout key | RPC caller unauthorized; no destination ownership proof |
| On-chain settlement | N/A | none in this path | must remain separate |

## Findings

| ID | Severity | Finding | Status |
|---|---|---|---|
| PAC-001 | High | `submitpoievidence` lacks submitter authentication/ownership binding; any RPC token holder can submit shares for a registered worker_id. | INVESTIGATING |
| PAC-002 | High | `pool_record_payout` lacks admin/operator authorization and payout destination ownership proof. | INVESTIGATING |
| PAC-003 | Medium | Duplicate shares rejected only by exact evidence_hash; different hashes for same epoch/worker can inflate units. | INVESTIGATING |
| PAC-004 | Medium | No epoch bounds/freshness/rate limits on evidence submission. | INVESTIGATING |
| PAC-005 | Info | Pool accounting is not on-chain consensus settlement; must not be represented as finalized chain payout. | DESIGN |

## Required Fixes

- Authenticate submitter to registered worker (auth_token_hash or signed proof) before accepting share.
- Bind evidence to owner/machine/epoch and prevent replay/inflation.
- Authorize `pool_record_payout` with operator/admin capability; bind payout destination to worker/owner.
- Add idempotency keys, rate limits, epoch bounds, and restart-recovery tests.
