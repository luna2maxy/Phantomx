# PHANTOMX — RPC/P2P Reliability Report

**Date:** 2026-11-26  
**Scope:** Exec2 smoke + exec3/exec4/exec5 validation fixes + exec6 dedup/post-fix verification. Not full RPC/P2P completion.  
**Raw logs:** `/tmp/phx_testnet_exec2_20261126/rpc_p2p/smoke.log`, `/tmp/phx_testnet_exec3_20261126/pool/*.log`, `/tmp/phx_testnet_exec4_20261126/rpc_p2p/`, `/tmp/phx_testnet_exec5_20261126/rpc_p2p/`, `/tmp/phx_testnet_exec6_20261126/rpc_p2p/`, `/tmp/phx_testnet_exec6_20261126/dedup_smoke2/`

## Completed tests

| Test | Result | Evidence |
|---|---|---|
| 20 concurrent `getblockcount` (temp regtest node) | PASS 20/20 HTTP 200 | exec2 smoke.log |
| Malformed JSON | PASS rejected HTTP 400 | smoke.log |
| Missing method | PASS rejected HTTP 400 | smoke.log |
| Unknown method | PASS JSON-RPC -32601 | smoke.log |
| Bad token | PASS JSON-RPC -32001 | smoke.log |
| `params=[]` on getblockcount | **FIXED** — now HTTP 400 "params must be an object" | exec4 params_smoke.log |
| `params=null` | PASS rejected HTTP 400 | exec4 params_smoke.log |
| Oversized param value (900k, under 1MiB) | ACCEPTED by parser/handler (ignored by getblockcount) — policy gap, not crash | exec5 rpc_tests.log |
| Request body >1MiB | Rejected/ERR (no crash) | exec5 rpc_tests.log |
| Pool RPC invalid numeric params | **FIXED** (PA-004 scoped) — clean invalid-param errors, no throw | unit regressions |
| exec6 build + unit + CTest after dedup changes | PASS: build EXIT=0, unit 137/137, CTest 11/11 | `/tmp/phx_testnet_exec6_20261126/rpc_p2p/{build_v3.log,unit_v3.log,ctest_v3.log}` |
| exec6 4-node full-mesh dedup smoke | PASS: each node `peers=3 ready=3`, all 4 alive, no crash | `/tmp/phx_testnet_exec6_20261126/dedup_smoke2/` |

## Fixes applied (auto-approved, non-consensus)

- `src/rpc/http_server.cpp`: reject non-object `params` when present (`params` must be object). Regression smoke: params_array/params_null -> HTTP 400.
- `src/rpc/node_handlers.cpp`: guarded `stoull/stoul` in pool handlers (`pool_submit_share`, `pool_getworker`, `pool_record_payout`, `submitpoievidence`); added `getblockhash`/`generatetoaddress` numeric validation.
- `tests/unit/test_poi.cpp`: `ptm_pool_invalid_numeric_params_rejected_cleanly`, `rpc_invalid_height_and_nblocks_rejected_cleanly`, lifecycle guard.
- `src/network/node.cpp` + `include/phantomx/network/peer.hpp`: duplicate-P2P-connection dedup (see below).
- Consensus impact: none. RPC input validation and transport peer bookkeeping only; no BFT/consensus/finality/quorum/genesis/block-format change.

## P2P duplicate-connection dedup (exec6)

**Problem (TB-020/TB-019 observation):** a startup race between the initial seed dials and the reconnect loop (`connect()` in progress, peer not yet in `peers_`) can open two TCP connections in the same direction. Simultaneous bidirectional dialing adds an inbound+outbound pair. This inflated `peer_count`/`ready_peer_count` (e.g. 5–6 instead of 3).

**Design requirement:** both endpoints must independently choose the SAME TCP connection to keep, else each drops the one the other kept (mutual-drop thrash). A "prefer outbound" rule is *not* symmetric.

**Final key (symmetric on both endpoints):**

```
(initiator_nonce, responder_nonce, initiator_ephemeral_port, responder_listen_port)
```

- outbound entry: initiator=local nonce, ephemeral=local source port (captured via `getsockname` at connect), responder listen port = remote `listen_port`.
- inbound entry: initiator=peer nonce, ephemeral=peer source port (from `accept`), responder listen port = local `bound_port_`.
- Both endpoints keep the lexicographically smallest key. Same-direction duplicates differ by the initiator's ephemeral port, so they are no longer treated as the same connection.

**Implementation defects found and fixed during exec6 smoke (transparent record):**

1. First attempt keyed only on `(initiator_nonce, responder_nonce)`. Same-direction duplicates share that pair, so the code skipped them and duplicates survived. Smoke showed `peers=5/6`.
2. Moving the decision outside the lock and using `remove_peer()` fixed a `terminate called without an active exception` crash: the original drop-self path erased the peer and returned while its own joinable `std::thread` handle was destroyed. `remove_peer()` already detaches the calling thread safely.

**Post-fix result:** clean smoke with `peers=3 ready=3` on all four nodes, all processes alive, duplicate events logged and resolved. Partition/heal/reconnect results are in `PHANTOMX_BFT_PARTITION_ISOLATION_AUDIT.md`.

## RPC exception safety (read-only review)

- `RpcServer::call` (`src/rpc/rpc.cpp:103-110`) wraps handler invocation in `try/catch (const std::exception&)` and returns JSON-RPC `-32603` internal error. Unguarded `std::stoull/std::stoul` sites therefore produce a clean error, not a process crash (usability gap: `-32603` instead of `-32602`).
- `http_server` rejects non-object `params` before dispatch and caps request bodies.

## P2P message-path controls (read-only review)

- 14-byte header; 32-bit length checked against `max_message_size` (default 2 MiB) **before** payload allocation (`node.cpp:329`).
- Network magic and payload checksum verified per message.
- Handshake timeout 5 s (`kHandshakeTimeoutMs`), idle timeout 30 s (`kIdleTimeoutMs`), per-peer rate limit 500 msg/s, `max_connections` 64.
- Ban score on invalid messages/timeouts/spam.
- Remaining gap: no raw-socket adversarial/mutation harness was executed; controls reviewed, not fuzzed.

## Exec6 runtime verification (2026-11-26)

### PoI attestation relay probe (black-box transport)

- Harness: `/tmp/phx_testnet_exec6_20261126/rpc_p2p/run_poi_probe.sh` (4-node mesh + observer peer + injector peer).
- Probe: `/tmp/phx_testnet_exec6_20261126/rpc_p2p/poi_relay_probe.py`, result `/tmp/phx_testnet_exec6_20261126/poi_relay/probe.json`, PASS=true.
- Results:
  - `relayed_first=true`, `relayed_third=true`: the relay path executes and forwards unique payloads to a ready peer.
  - `duplicate_relay_count=1`: a replayed identical payload is NOT re-relayed (seen_or_mark works).
  - `rpc_all_ok=true`, `rpc_before.ok=true`: node RPC stays responsive before and after relay (no deadlock from the former `mu_` double-lock).
  - All 4 nodes alive post-probe, peers=3/ready=3, count=1, no crash/terminate markers.
- Consensus impact: none. The probe is a transport-level verification; PoI semantics and consensus behavior were not changed.

### TB-021 concurrent RPC under reconnect

- Harness: `/tmp/phx_testnet_exec6_20261126/rpc_p2p/tb021_reconnect_load.sh` (4-node mesh, 3 RPC workers, 2 forced node-3 restarts, 50s).
- Workers: worker0 4180 ok/0 err, worker1 4140 ok/0 err, worker2 4145 ok/0 err (ports 33041-33043).
- No crash/terminate markers in any node log.
- Post-run: node3 recovered to peers=3/ready=3; node0-2 alive. A final-query transient left some net/count lines as ERR, but the sustained 0-error worker results and the clean partition reconnect matrix (3/3 converged) are the authoritative evidence.
- Evidence: `/tmp/phx_testnet_exec6_20261126/rpc_p2p/tb021/{workers.log,events.log,final.log,n*.log}`.

### Configuration-isolation finding (new)

`src/daemon/main.cpp` default-constructs `NetworkConfig` and sets only `listen_host/listen_port/seed_peers`. It never assigns `network_magic` or `genesis_hash`, so they stay at `NetworkConfig` defaults (`0x52505801` regtest magic, zero genesis) even under `--testnet` with `config/testnet*.json`. Consequence: P2P cross-network isolation (magic + genesis) is not wired to the selected network. Reported here and in the master blocker register; no code change made in this pass (owner approval required for behavior change).

## Not run / NOT PASS

Adversarial/mutated P2P fuzzing, authZ matrix, timeout/resource-exhaustion matrix, full P2P disconnect/reconnect matrix beyond partition/reconnect probes, and the 900k-param acceptance policy (still open). The 900k-param acceptance under the 1 MiB limit is a **policy decision still open** (recommend an explicit per-field/request size policy before public exposure). **Do not claim full RPC/P2P completion.**
