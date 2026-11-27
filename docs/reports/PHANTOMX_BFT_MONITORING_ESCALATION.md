# PHANTOMX — BFT Recovery Monitoring & Escalation (TB-019 Option A)

**Date:** 2026-11-26  
**Status:** Approved monitoring disposition (Option A). TB-019 remains OPEN for mainnet; root cause UNCONFIRMED.

## Monitored signals
- `bft_status` height/view/locked per node.
- `finalized_height` / `finalized_hash` per node.
- `getnetworkinfo` peer_count / ready_peer_count.
- Node process liveness and RPC responsiveness.

## Detection criteria
- Finalized height divergence: any node's finalized height differs from majority.
- Finalized hash conflict: any differing finalized_hash across nodes.
- Peer starvation: ready_peer_count < (N-1) for > 30s.
- Catch-up failure: restarted node not converging within 60s.
- Stale finalized metadata: node reports finalized height < chain tip height.

## Thresholds
- Warning: finalized height lag ≥ 1 block for > 30s; ready peers < N-1 for > 30s.
- Critical: conflicting finalized hash; false finality; any safety violation.

## Escalation
- Conflicting finalized hash or false finality → critical incident; invalidates bounded-risk acceptance for affected release.
- Operator actions: restart node, verify P2P connectivity/peer counts, escalate if no convergence in 60s.

## Diagnostic script
A read-only helper is provided: `/tmp/phx_testnet_exec7c_20261126/tb019/monitor.py` (queries nodes, reports divergence). Not a production change.