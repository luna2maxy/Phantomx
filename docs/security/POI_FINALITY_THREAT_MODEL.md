# PoI Finality Threat Model

> Status: INVESTIGATING / DESIGN  
> As of: 2026-11-26  
> Scope: PoI block finality/quorum path in `src/rpc/node_handlers.cpp` and persisted PoI state.  

## 1. Call Graph / Data Flow

| Entry | Auth | Key checks | State mutation | Finality effect |
|---|---|---|---|---|
| `verifier_register` (1212) / `registerverifier` (2364) | RPC token; optional `PHANTOMX_VERIFIER_REG_TOKEN` | id/address/endpoint format; regtest HRP address format; optional pubkey hex; id/address conflict rejection | persists verifier registry file | affects registered verifier set used by attestation/finality |
| `verifier_heartbeat` (1277) | RPC token; optional reg token | verifier must exist by id/address | updates verifier last_heartbeat/status=online; recalculates `g_poi_verifiers_online`, `g_poi_finality_quorum`; may finalize pending blocks in-memory; persists registry | **Can trigger finalization** when pending attestations >= quorum. No verifier signature/freshness proof. |
| `poi_submit_attestation` (1342, RPC) | RPC token | verifier registered by pubkey; attestation signature verified; block_hash format | stores attestation in `g_poi_block_finality`; may set finalized/finalized_at/height/hash; persists poi_state; broadcasts P2P | **Can trigger finalization** when att_count >= quorum_required |
| P2P attestation handler (~1455) | P2P message (no RPC token) | verifier registered; signature verified | same as above, minus RPC response | **Can trigger finalization** |
| `setpoiquorum` (1489) | RPC token only | q_min >=1 parse | sets `g_poi_q_min`, `g_poi_finality_quorum=max(current,qmin)`, persists | changes quorum/finality policy; can disrupt liveness/finality assumptions |
| `stoppoi` (2228) | RPC token only | none | sets global `g_poi_running=false`, marks participant not running, persists | node-wide PoI running flag affected; can impact PoI service/production assumptions |

## 2. Key State

- `g_poi_q_min` (atomic u64, default 1): minimum quorum.
- `g_poi_finality_quorum` (atomic u64, default 1): effective quorum.
- `g_poi_block_finality`: `block_hash -> {height, quorum_required, finalized, finalized_at, attestations[]}`.
- `g_poi_finalized_height/hash`: latest finalized PoI block.
- Persisted in `poi_state_json` chain metadata; loaded at startup.

## 3. Quorum Calculation

- `poi_required_quorum(online)`: `max(1, max(g_poi_q_min, online==0?g_poi_q_min:((online*2+2)/3)))` (observed in heartbeat path: `q = max(1, max(g_poi_q_min.load(), on==0?g_poi_q_min.load():((on*2+2)/3)))`).
- Heartbeat recalculates quorum and may finalize pending non-finalized blocks whose attestation count >= new quorum.
- Attestation submission uses stored `bf.quorum_required` (initialized from q_min or `poi_required_quorum(online)` in P2P path).

## 4. Threat Findings

| ID | Severity | Finding | Evidence | Status |
|---|---|---|---|---|
| PTM-001 | Critical | **Unauthenticated heartbeat can influence finality**. `verifier_heartbeat` requires no verifier key signature/freshness proof; it updates online count, recalculates quorum, and can finalize pending blocks. | `node_handlers.cpp:1277-1320` | INVESTIGATING |
| PTM-002 | High | **`setpoiquorum` is RPC-token-only admin mutation** affecting quorum/finality policy and persisted state. | `node_handlers.cpp:1489-1499` | INVESTIGATING |
| PTM-003 | High | **Verifier registration lacks PoP**. Pubkey format/address HRP is not key ownership proof; regtest HRP hardcoded. | `node_handlers.cpp:1212-1275` | INVESTIGATING |
| PTM-004 | High | **Attestation freshness/replay/revocation incomplete**. Signature verified and verifier registered checked, but epoch/freshness/revocation/duplicate-cross-restart policy needs hardening. | `node_handlers.cpp:1342-1455` | INVESTIGATING |
| PTM-005 | High | **`stoppoi` has global side effect** (`g_poi_running=false`) and no owner authorization; can affect node-wide PoI service. | `node_handlers.cpp:2228-2243` | INVESTIGATING |
| PTM-006 | Medium | **q_min default/bootstrap is 1**, allowing low-quorum finalization in degenerate/early states. | `g_poi_q_min{1}`, quorum calc | INVESTIGATING |
| PTM-007 | Medium | **Finality is PoI attested, not BFT consensus finality**. Must not be presented as equivalent to BFT/chain finality. | method returns `finality_model: poi_attested` | DESIGN |

## 5. Required Security Properties (target)

- Heartbeat MUST NOT finalize evidence or change eligibility without authenticated, fresh, verifier-bound proof.
- Attestation MUST bind challenge/block/epoch/verifier key, be fresh, deduped, revocation-aware, and quorum-calculated deterministically.
- Quorum policy changes MUST require separate admin capability and audit log.
- Finality MUST be based on authenticated independent verifier evidence; registration/count alone is insufficient.
- Restart recovery MUST preserve finalized state and reject stale/replayed evidence.

## 6. Status

- Threat model: **DESIGN/INVESTIGATING** (code-traced, not independently audited).
- Fixes: **NOT IMPLEMENTED** (require careful protocol/operator design; some may need protocol authorization).
