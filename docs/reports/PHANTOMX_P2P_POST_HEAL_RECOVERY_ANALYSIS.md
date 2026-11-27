# PHANTOMX — P2P Post-Heal Peer-Mesh Recovery Analysis (TB-020)

**Date:** 2026-11-26  
**Scope:** Read-only analysis of EXEC6 final-binary 3-trial partition matrix. No P2P code changes.  
**Evidence:** `/tmp/phx_testnet_exec6_20261126/bft_partition_v3/` (summary.log, trial_1/2/3/snap.log), `analyze_partition.py`, `src/network/node.cpp`.

## Clarification of an apparent discrepancy

Earlier concern: "peer counts after healing were 1/1/1/1 or 2/2/2/2." This was a **misreading**: the matrix `HEAL=`/`RECONNECT=` lines report **block counts** (`h0..h3 = count ...`), not peer counts. The snap logs record peer counts separately.

Verified from snap logs (all 3 trials):

| Trial | Phase | peer_count/ready (all 4 nodes) | ss pairs |
|---|---|---|---|
| T1 | baseline | 3/3 | 6 (full mesh) |
| T1 | partitioned | 1/1 | 2 |
| T1 | healed | 3/3 | 6 |
| T1 | reconnect_n3 | 3/3 | 6 |
| T2 | baseline | 3/3 | 6 |
| T2 | partitioned | 1/1 | 2 |
| T2 | healed | 3/3 | 6 |
| T2 | reconnect_n3 | 3/3 | 6 |
| T3 | baseline | 3/3 | 6 |
| T3 | partitioned | 1/1 | 2 |
| T3 | healed | 3/3 | 6 |
| T3 | reconnect_n3 | 3/3 | 6 |

So peer-mesh recovery is **complete** in every trial (3 logical peers, 3 ready, 6 TCP connections = full mesh). No incomplete recovery.

## Dedup impact

The dedup implementation does not suppress valid reconnections:

- After partition, each node holds exactly 1 connection (correct for 2-node groups).
- After heal full-mesh restart, dedup collapses duplicate dial races to 1 connection per logical peer; all 3 peers become ready.
- Duplicate events are logged and resolved (T1 10/12, T2 9/7, T3 7/6); no mutual-drop thrash (heal converges).
- Reconnect after intentional n3 disconnect also reaches 3/3 in all trials.

## Does dedup unintentionally suppress valid reconnections?

No evidence of suppression in 3 trials. The reconnect loop (`maintenance_loop`) checks `p->info.host == seed.host && p->info.listen_port == seed.port` under `mu_`; if a peer was removed by dedup, it is absent from `peers_`, so the next tick dials. The 3/3 recovery in all trials confirms this.

## Residual considerations

- Startup dial race can still open duplicate connections; dedup collapses them after handshake. This is by design and observed to resolve.
- Shutdown/join lifecycle race (late accept-loop peers) remains a theoretical residual; no observed crash.
- Process-level only; not OS firewall/network namespace.

## TB-020 status recommendation

TB-020 remains **PARTIAL (improved, not closed)**. Chain convergence alone is not sufficient for closure, and the audit confirms peer-mesh recovery is complete, but the following are still required for closure:
1. OS-level (firewall/network namespace) isolation proof, or explicit owner acceptance of process-level evidence.
2. Resolution or explicit acceptance of the shutdown/join lifecycle residual.
3. Wiring of NetworkConfig magic/genesis (TB-023) so cross-network isolation is actually enforced.