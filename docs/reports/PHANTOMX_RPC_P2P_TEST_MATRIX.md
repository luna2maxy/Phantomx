# PHANTOMX — RPC/P2P Reliability Test Matrix (TB-021)

**Date:** 2026-11-26  
**Scope:** Read-only audit + executed tests through exec6. No production behavior changes.  
**Evidence base:** `/tmp/phx_testnet_exec2_20261126/rpc_p2p/`, `exec3`, `exec4`, `exec5`, `exec6`, `/tmp/phx_testnet_exec6_20261126/`.  
**Companion:** `docs/reports/PHANTOMX_RPC_P2P_RELIABILITY_REPORT.md`.

## Legend
- **PASS** — executed and met expected.
- **PARTIAL** — executed with caveats.
- **NOT RUN** — not executed; explicitly not PASS.
- **POLICY** — requires owner policy decision.

## Matrix

| # | Test | Expected | Actual | Evidence | Result | Remaining gap |
|---:|---|---|---|---|---|---|
| 1 | 20 concurrent `getblockcount` (temp node) | 20/20 HTTP 200 | 20/20 HTTP 200 | exec2 `smoke.log` | PASS | None for this scope |
| 2 | Malformed JSON body | HTTP 400 | HTTP 400 | exec2 `smoke.log` | PASS | None |
| 3 | Missing `method` field | HTTP 400 | HTTP 400 | exec2 `smoke.log` | PASS | None |
| 4 | Unknown method | JSON-RPC -32601 | -32601 | exec2 `smoke.log` | PASS | None |
| 5 | Bad auth token | JSON-RPC -32001 | -32001 | exec2 `smoke.log` | PASS | None |
| 6 | `params=[]` | HTTP 400 "params must be an object" | HTTP 400 (after fix) | exec4 `params_smoke.log` | PASS | None |
| 7 | `params=null` | HTTP 400 | HTTP 400 | exec4 `params_smoke.log` | PASS | None |
| 8 | Oversized param value (900 KB, under 1 MiB) | Policy decision | Accepted (ignored by handler) | exec5 `rpc_tests.log` | POLICY | Decide per-field/request size policy before public exposure |
| 9 | Request body >1 MiB | Rejected/ERR, no crash | Rejected/ERR | exec5 `rpc_tests.log` | PASS | None |
| 10 | Pool invalid numeric params | Clean invalid-param, no throw | Clean invalid-param | exec3/4 unit logs | PASS | None |
| 11 | `getblockhash`/`generatetoaddress` numeric validation | Clean invalid-param | Clean invalid-param | exec5 `unit2.log` | PASS | None |
| 12 | Unguarded `stoull` sites | No crash (dispatch try/catch → -32603) | No crash; generic -32603 | `src/rpc/rpc.cpp:103-110` | PARTIAL | Convert to -32602 for usability (optional) |
| 13 | Build/unit/CTest after dedup | 0 warnings, 137/137, 11/11 | PASS | exec6 `build_v4.log`, `unit_v4.log`, `ctest_v4.log` | PASS | None |
| 14 | 4-node dedup smoke | peers=3/ready=3 all nodes | peers=3/ready=3 all nodes | exec6 `dedup_smoke2/` | PASS | None |
| 15 | Partition isolation matrix (3 trials) | 0 cross-group, no false finality | 0 cross-group, no false finality | exec6 `bft_partition_v3/` | PASS | Process-level only |
| 16 | PoI attestation relay probe | Relay executes, no deadlock, RPC responsive | PASS=true | exec6 `poi_relay/probe.json` | PASS | None |
| 17 | Concurrent RPC under reconnect | 0 errors, no crash | 0 errors (3×~4140 calls), no crash | exec6 `tb021/{workers.log,final.log}` | PASS | Final-query transient ERR noted |
| 18 | Authorization matrix (`allowed_methods`) | Per-user allow/deny enforced | Code path present (`rpc.cpp:47-49`) | Read-only review | NOT RUN | Execute authZ matrix |
| 19 | Malformed/adversarial P2P frames | Reject + score/ban, no crash | Controls present (magic, checksum, length cap, rate limit) | Read-only review `node.cpp:318-338` | NOT RUN | Raw fuzz/mutation harness |
| 20 | Handshake timeout (5s) | Remove peer on timeout | Code path present | Read-only review | NOT RUN | Controlled test |
| 21 | Idle timeout (30s) | Remove peer on idle | Code path present | Read-only review | NOT RUN | Controlled test |
| 22 | Rate limit (500 msg/s) | Score/ban on exceed | Code path present | Read-only review | NOT RUN | Controlled test |
| 23 | `max_connections` (64) enforcement | Reject new connection at limit | Code path present (`node.cpp:250`) | Read-only review | NOT RUN | Controlled test |
| 24 | Connection churn beyond 30/30 | No crash, recovery | 30/30 PASS (exec5) | exec5 `rpc_tests.log` | PARTIAL | Sustained churn soak |
| 25 | Node restart under RPC load | RPC stays available | TB021 0 errors during restarts | exec6 `tb021/workers.log` | PARTIAL | Repeated restarts under higher load |
| 26 | Peer-state consistency during reconnect | 3/3 after reconnect | 3/3 in all partition trials | exec6 `bft_partition_v3/` | PASS | None for this scope |
| 27 | P2P message size cap (2 MiB) | Reject >2MiB before alloc | `node.cpp:329` check before alloc | Read-only review | PASS (code) | Runtime fuzz |
| 28 | RPC 1 MiB body cap | Reject >1MiB | Rejected/ERR | exec5 `rpc_tests.log` | PASS | None |

## P2P message-path controls (read-only review)

- 14-byte header; 32-bit length checked against `max_message_size` (default 2 MiB) **before** payload allocation (`node.cpp:329`).
- Network magic and payload checksum verified per message (`node.cpp:328`, `:332`).
- Handshake timeout 5 s (`kHandshakeTimeoutMs`), idle timeout 30 s (`kIdleTimeoutMs`), per-peer rate limit 500 msg/s, `max_connections` 64.
- Ban score on invalid messages/timeouts/spam; `is_banned` gate on hello.
- Dedup collapses duplicate connections (exec6).

## Overall TB-021 status

**PARTIAL.** Core RPC validation and P2P transport controls are sound and exercised for the common paths. Remaining gaps are explicitly listed above (authZ matrix, adversarial P2P fuzz, timeout/resource-exhaustion controlled tests, 900k-param policy). Do not mark TB-021 complete.