# PHANTOMX — BFT Recovery Failure Analysis

**Date:** 2026-11-26  
**Scope:** Exec2 recovery trials + exec4 instrumented repro + exec5 repeated instrumented trials. No consensus/BFT semantic changes.  
**Raw logs:** `/tmp/phx_testnet_exec2_20261126/bft/`, `/tmp/phx_testnet_exec4_20261126/bft_recovery/`, `/tmp/phx_testnet_exec5_20261126/bft_recovery/`

## Summary

| Run | Scenario | Final counts | bft height/view | Result |
|---|---|---|---|---|
| exec2 trial 1 | 2-validator shutdown/restart/heal | 4,4,4,4 | n/a | PASS |
| exec2 trial 2 | same | 2,2,2,2 | n/a | **FAIL** |
| exec2 trial 3 | same | 4,4,4,4 | n/a | PASS |
| exec4 instrumented repro | same, append logs/snapshots | 4,4,4,4 | 5/3,5/3,5/3,5/3 | PASS |
| exec5 trial 1 | same, instrumented, unique ports | 4,4,4,4 | 5/3,5/3,5/3,5/3 | PASS |
| exec5 trial 2 | same | 4,4,4,4 | 5/3,5/3,5/3,5/3 | PASS |
| exec5 trial 3 | same | 3,3,3,3 | 4/3,4/3,4/3,4/3 | PASS (consistent) |
| exec5 trial 4 | same | 4,4,4,4 | 5/3,5/3,5/3,5/3 | PASS |
| exec5 trial 5 | same | 3,3,3,3 | 4/3,4/3,4/3,4/3 | PASS (consistent) |

**Observed:** 9/10 completed runs PASS, 1/10 FAIL (exec2 trial 2). 6/6 completed instrumented runs PASS. No conflicting/false finality observed in any run. All exec5 trials: `/tmp/phx_testnet_exec5_20261126/bft_recovery/summary.log`, EXIT=0.

## Exec2 Trial 2 root-cause evidence (read-only)

- Script overwrote restarted n2/n3 logs (`>`). Evidence limited.
- DB (read-only): n0-n2 finalized_height 2 same hash `df6f64f9aae57aeaefe15b0015e2bfd90592d2ae9b3b7dcb0ef2e1261287eb83`; n3 finalized_height 1 hash `0ca511b5ca29a7aeb0b1d2447317e713ca391e27a0cadb7c8746a50e02abe6d6`, tip metadata 2, block 3 present.
- Logs: n3 `no ready peers` / view timer; n1/n2 missing peer targets after restart.
- Interpretation: P2P mesh recovery/liveness timing failure; n3 stale finalized metadata observed. Not a confirmed consensus-rule violation; not a confirmed safety violation.

## Exec4/5 instrumented observations

- N3 may restart with stale BFT h2/v1 while others at h3; catches up after peer reconnection and propose. No conflicting finality.
- Peer counts sometimes > expected (5-6) due to duplicate inbound/outbound connections - P2P dedup observation, not partition leakage in isolation tests.
- All completed instrumented runs converged to identical finalized height/hash across 4 nodes.

## Open concerns (TB-019 remains PARTIAL)

- Original intermittent failure not reproduced in instrumented runs; cause UNCONFIRMED.
- N3 stale finalized metadata in exec2 trial 2 DB needs root-cause analysis (read-only first; no production DB/BFT semantic change without approval).
- Duplicate P2P connections inflate peer counts; investigate dedup/readiness semantics.
- Repeated trials improve confidence but do not prove absence of intermittent failure.

## Exec6 update — duplicate-connection dedup (2026-11-26)

A plausible contributor to the "no ready peers" / post-restart mesh recovery timing was confirmed as a real but non-consensus defect: the reconnect loop could open a second connection to the same logical peer during the startup dial race, and simultaneous bidirectional dialing added an inbound+outbound pair. `peer_count`/`ready_peer_count` could therefore exceed the true mesh degree.

- Implemented symmetric duplicate-peer resolution in `src/network/node.cpp` (`handle_hello`), keyed on `(initiator_nonce, responder_nonce, initiator_ephemeral_port, responder_listen_port)`; both endpoints keep the lexicographically smallest key. Transport/peer bookkeeping only — no BFT/consensus/finality/genesis/block-format change.
- Added `PeerInfo::local_port` (captured via `getsockname` for outbound peers).
- Post-fix 4-node smoke: each node `peers=3 ready=3`, no crashes, duplicate events resolved (`/tmp/phx_testnet_exec6_20261126/dedup_smoke2/`).
- Post-fix partition trial 1 (pre-final-binary): baseline 3/3 ready on all nodes; partition 1/1 each; no cross-group sockets; heal+reconnect 2,2,2,2 converged. See `PHANTOMX_BFT_PARTITION_ISOLATION_AUDIT.md`.

This **removes duplicate connections as a confounder** but does **not** prove the exec2 trial-2 failure (N3 stale finalized h1 vs h2) cannot recur. TB-019 stays PARTIAL until the owner accepts a disposition or a root cause is confirmed. No consensus fix was applied.

## Proposed disposition (owner decision required)

**Option A (recommended for isolated testnet): accept as bounded operational risk, keep OPEN for mainnet.** Rationale: 6/6 completed instrumented recovery runs PASS, 0 false/conflicting finality across all runs, exec2 N3 evidence is consistent with lag/stale metadata rather than a safety violation, and a duplicate-connection confounder has now been removed. Conditions: monitor ready-peer count and cross-node finalized height/hash divergence; treat any conflicting finality as a stop-ship; re-run recovery before mainnet and before any live 2-VPS soak.

**Option B (conservative): keep TB-019 BLOCKER and require a confirmed root cause before any testnet.** Implication: any fix touching persistent state/consensus-adjacent recovery needs separate owner approval and a full before/after evidence package.

Either way: **VPS remains FROZEN; TESTNET remains NO-GO** until this and the other gates are explicitly accepted.