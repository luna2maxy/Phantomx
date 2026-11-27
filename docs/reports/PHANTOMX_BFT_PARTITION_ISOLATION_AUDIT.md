# PHANTOMX — BFT Partition Isolation Audit

**Date:** 2026-11-26  
**Scope:** Audit of exec2 2+2 process/connect isolation test, exec5 diagnostic repro, and exec6 final-binary 3-trial matrix. No consensus/BFT semantic changes.  
**Raw logs:** `/tmp/phx_testnet_exec2_20261126/bft/partition.log`, `/tmp/phx_testnet_exec5_20261126/bft_partition/diag.log`, `/tmp/phx_testnet_exec6_20261126/bft_partition_v3/` (3 trials), `/tmp/phx_testnet_exec6_20261126/dedup_smoke2/`, `/tmp/phx_testnet_exec6_20261126/poi_relay/`

## Exec2 observed result (PARTIAL)

| Phase | Counts | Peer counts (from getnetworkinfo) | Outcome |
|---|---|---|---|
| Full mesh baseline | 1,1,1,1 after propose | n/a | baseline |
| 2+2 partition ({0,1},{2,3}) | before 1,1,1,1; after 1,1,1,1 | 30641:3, 30642:3, 30643:2, 30644:2 | `NO_FALSE_FINALITY=PASS` |
| Heal | after heal 1,1,1,1; after propose 2,2,2,2 | n/a | converged; same hash observed |

Exec2 limitation: peer counts 3,3,2,2 contradicted strict 2+2 isolation; partition-phase detailed logs overwritten on heal restart.

## Exec5 diagnostic repro (process/connect, socket-verified)

Script: `/tmp/phx_testnet_exec5_20261126/bft_partition/partition_diag.sh` (append logs, per-phase snapshots, `ss -tnp` capture).

| Phase | Counts | peer_count/connections/ready | ss socket evidence | Outcome |
|---|---|---|---|---|
| Full mesh after propose | 1,1,1,1 | mixed 4-6 observed | full mesh ESTAB | baseline |
| Partitioned ({0,1},{2,3}), sleep 5s | 1,1,1,1 | 2,2,2,2 / ready 2 | 8 ESTAB total: only 31651<->31652 and 31653<->31654 pairs; **no cross-group P2P sockets** | isolation clean at socket level |
| After partition propose (+10s) | 1,1,1,1 | 2,2,2,2 | same 8 ESTAB, no cross-group | **NO_FALSE_FINALITY=PASS** |
| Healed full mesh | converged to 3,3,3,3, bft h4/v2 | 5-6 | full mesh ESTAB | converged, same height/view |

### Key finding (exec5)

- **Socket-level isolation was clean** in exec5: no cross-partition ESTAB between {31651,31652} and {31653,31654} during partition.
- **peer_count=2 per node** during partition is explained by duplicate connections per pair (outbound+inbound both present as peers), not by cross-partition leakage. This is a P2P connection-dedup observation, not a partition-safety failure.
- No false finality observed. Heal converged to identical finalized height.

## Exec6 final-binary 3-trial matrix (2026-11-26)

**Binary identity (all 3 trials on this exact binary):**
- Path: `build-ubuntu/bin/phantomxd`
- SHA256: `7338730b1240ac9b2d7ba8e0ffb320b95cc5e7ac7234c5b21e1f7e7ff461121d`
- Size: 34489784 bytes; mtime 2026-11-26 15:07:05Z
- HEAD: `d094596549a53eb0695527df06729eca41e737e9`, branch `encryption`, dirty 115+
- Build: `cmake --build build-ubuntu -j6` EXIT=0, warnings 0; unit 137/137 PASS; CTest 11/11 PASS (logs `/tmp/phx_testnet_exec6_20261126/rpc_p2p/{build_v4.log,unit_v4.log,ctest_v4.log}`).
- Evidence: `/tmp/phx_testnet_exec6_20261126/bft_partition_v3/` (summary.log, trial_1/2/3/snap.log, run.log).

### Baseline (full mesh), all 3 trials

| Trial | Node peer_count/ready | peer direction (from getpeerinfo) | Outcome |
|---|---|---|---|
| T1/T2/T3 | 3/3 on all 4 nodes | mixture of inbound+outbound to the other 3 logical peers; no duplicate ports per node | PASS (3 logical peers, 3 ready) |
| T1/T2/T3 | ss: 6 ESTAB per trial = complete mesh (degree 3 per node) | — | full mesh ESTAB |

### Partition ({0,1} vs {2,3}), all 3 trials

| Trial | Node peer_count/ready | before/after block count | BFT h/v/locked | ss socket evidence | Cross-group edges |
|---|---|---|---|---|---|
| T1 | 1/1 on all nodes | 1,1,1,1 → 1,1,1,1 | h=2/v=1/locked=true unchanged (all nodes) | 2 ESTAB pairs: (0,1) and (2,3) only | 0 |
| T2 | 1/1 on all nodes | 1,1,1,1 → 1,1,1,1 | h=2/v=1/locked=true unchanged (all nodes) | 2 ESTAB pairs: (0,1) and (2,3) only | 0 |
| T3 | 1/1 on all nodes | 1,1,1,1 → 1,1,1,1 | h=2/v=1/locked=true unchanged (all nodes) | 2 ESTAB pairs: (0,1) and (2,3) only | 0 |

- `NO_FALSE_FINALITY=PASS` all 3 trials (block counts did not advance during partition; BFT height/view/locked unchanged before/after propose).
- `NO_CROSS_GROUP_ALL_TRIALS=PASS` (analyzer `/tmp/phx_testnet_exec6_20261126/bft_partition_v3/analyze_partition.py`): exactly 2 connections per trial, edges (0,1) and (2,3), degree 1 per node, 0 cross-group, 0 unresolved.
- Each partitioned node has exactly 1 logical peer / 1 ready, with the peer being its in-group partner (inbound or outbound direction recorded in `peers_json`).

### Heal + reconnect, all 3 trials

| Trial | After heal counts | After reconnect (n3) counts | Converged | Finalized hash (all 4 nodes) |
|---|---|---|---|---|
| T1 | 3,3,3,3 | 3,3,3,3 | PASS | `a014b775913c0e0a4d67046925a1fa83ceddc63cab21553319336b9311173ea3` (h3) |
| T2 | 1,1,1,1 | 1,1,1,1 | PASS | `c949a70027fc55df17a21ad12a72e5b6de45a5d647d5377d72f6cee6bedaad06` (h1) |
| T3 | 2,2,2,2 | 2,2,2,2 | PASS | `39fddb50c4a52fe2aa68887153bd426619d859ccf145000ceaa12e72d35faf5c` (h2) |

- All 4 nodes within each trial share identical `finalized_height` and `finalized_hash` (read-only SQLite check, no conflicting finality).
- `DUP_EVENTS`: T1 replaced=10 dropped=12; T2 replaced=9 dropped=7; T3 replaced=7 dropped=6 (dedup events logged and resolved; see `n*.log`).
- No crashes, `terminate called`, or segfaults in any trial (`grep` over `trial_*/n*.log`).

### Key finding (exec6)

- **Socket-level isolation proven** for all 3 trials: partition groups {0,1} and {2,3} have zero cross-group TCP connections; each node holds exactly 1 connection to its partner.
- **Dedup now collapses duplicate connections**: baseline peer_count=3/ready=3 (vs 5-6 before), partition 1/1 (vs 2/2 before). Duplicate events are logged and resolved; no mutual-drop thrash observed (heal converges).
- **No false finality** during partition; BFT height/view/locked unchanged; block counts did not advance; finalized hash consistent across all nodes after heal.
- **Reconnect after healing and after intentional n3 disconnect** converges in all 3 trials.

## Assessment

**PARTIAL (improved, not closed).** Process/connect isolation demonstrated with OS-level socket evidence in 3 repeated runs on a single clean binary; no false finality; heal/reconnect converged; peer counts now reflect logical degree. Remaining gaps:

1. Process-level only (not OS firewall/network namespace) — cannot claim real network partition.
2. Duplicate-connection observation improved but not eliminated as a class of startup race (reconnect loop can still dial twice; dedup collapses after handshake).
3. Shutdown/join lifecycle residual risk: `P2PNode::stop()` joins snapshot peer threads and detached self-thread handles may still be in flight; late accept-loop peers may not be joined before `peers_.clear()`. No crash observed in 3 trials + smoke + probe, but the audit identifies a follow-up hardening item (detach→zombie tracking) requiring separate owner approval.
4. `NetworkConfig::network_magic` and `genesis_hash` are left at defaults (regtest magic, zero genesis) in `src/daemon/main.cpp` regardless of `--testnet`/config; P2P cross-network isolation is not wired. Reported as a configuration-isolation finding (see readiness report / master blocker register). No code change made.
5. Repeated runs improve confidence but do not prove absence of intermittent failure; TB-019 remains PARTIAL.

**Do not claim real network partition.** TB-020 remains PARTIAL.