# PHANTOMX — Network Identity Audit (TB-023)

**Date:** 2026-11-26  
**Scope:** Read-only audit of the path from daemon startup to P2P handshake network identity. No code changes.  
**Evidence dir:** `/tmp/phx_testnet_exec7a_20261126/network_identity/`  
**Raw evidence:** `/tmp/phx_testnet_exec6_20261126/poi_relay/probe.json`, `/tmp/phx_testnet_exec6_20261126/poi_relay/n0.log`, `src/daemon/main.cpp`, `include/phantomx/network/node.hpp`, `src/network/node.cpp`.

## Classification

**CONFIRMED defect.** `phantomxd` never propagates the selected network's canonical magic or genesis hash into `NetworkConfig`. P2P layer therefore always uses regtest defaults (magic `0x52505801`, zero genesis) regardless of `--testnet`/config. Cross-network isolation at the P2P layer is not wired, even though the handshake nominally validates both fields.

## Source references

- `src/daemon/main.cpp:66-67` — `--testnet` / `--regtest` flags.
- `src/daemon/main.cpp:83-103` — `cli_testnet`, `cli_regtest`, config `testnet`/`regtest`, `resolve_network(...)` → `network` (Network enum).
- `src/daemon/main.cpp:110` — `datadir = default_datadir(network)`.
- `src/daemon/main.cpp:148-151` — `rpcport`/`p2pport` defaults from `default_rpc_port(network)` / `default_p2p_port(network)` (network IS used for ports).
- `src/daemon/main.cpp:371-387` — `NetworkConfig p2p_network;` sets only `listen_host`, `listen_port`, `seed_peers`. **No assignment to `network_magic` or `genesis_hash`.**
- `src/daemon/main.cpp:387` — `P2PNode p2p(p2p_network, &chain, &mempool);`
- `include/phantomx/network/node.hpp:32-35` — `NetworkConfig` defaults: `network_magic = 0x52505801u` (regtest magic), `genesis_hash{}` (zero).
- `include/phantomx/common/chain_params.hpp:26-36` — canonical per-network magic: mainnet `0x50485801`, testnet `0x54505801`, regtest `0x52505801`.
- `src/blockchain/genesis.cpp:39-47` — `network_magic(Network)`.
- `src/network/node.cpp:591` — `if (hello.network_magic != config_.network_magic)` → `maybe_ban`, return (magic IS validated).
- `src/network/node.cpp:592` — `if (hello.genesis_hash != config_.genesis_hash)` → `maybe_ban`, return (genesis IS validated).
- `src/network/node.cpp:596` — `peer->info.nonce = hello.nonce;` (identity binding).
- `src/network/node.cpp:601` — `is_banned(host, listen_port)` gate.
- `config/testnet-bft-canonical.json:13` — `"network_magic": "0x54505801"` (canonical value present in config but not consumed by P2P).
- `config/testnet.json` — genesis `block_hash` `98a75772...` (canonical genesis, not wired to P2P).
- `tests/network/test_p2p.cpp:88-95` — tests set `cfg1.network_magic` manually (proves the field is meant to be set by callers).

## Question-by-question findings

**A. Does `--testnet` assign the canonical network magic?**  
NO. `main.cpp:371-387` constructs `NetworkConfig` with default `network_magic = 0x52505801u` (regtest magic) and never overrides it for testnet. `--testnet` only affects `network`, datadir, ports, and RPC handlers; not P2P magic.

**B. Does `--testnet` assign the canonical genesis hash?**  
NO. `p2p_network.genesis_hash` stays zero-initialized (`Hash256Bytes{}`). The chain's genesis block hash (`98a75772...`) is not propagated to P2P.

**C. Are default regtest values still used when testnet is enabled?**  
YES. Both magic and genesis default to regtest/zero values for all networks. Confirmed by code inspection and by the exec6 PoI probe, which had to use magic `0x52505801` and 32 zero bytes to complete a handshake with a `--testnet` node.

**D. Does the P2P handshake actually validate network magic?**  
YES (`node.cpp:591`). A peer presenting a different magic is banned (`maybe_ban`) and removed. The validation is real but ineffective because all nodes share the same default.

**E. Does it validate genesis hash before peer admission or data exchange?**  
YES (`node.cpp:592`), before `handle_hello` proceeds and before any relay/data. Same caveat: all nodes share zero genesis, so the gate is vacuous.

**F. Can a regtest node connect to a testnet node?**  
YES in practice. Both use default magic `0x52505801` and zero genesis, so the handshake passes. This defeats network isolation.

**G. Can a node with a different genesis hash connect?**  
Only if it uses the same (zero) genesis hash. A node that sets a non-zero genesis hash would be rejected by `node.cpp:592`. Since no daemon sets it, this is not exercised in production.

**H. What happens when a peer presents an incorrect network identity?**  
`maybe_ban(peer, "magic mismatch"/"genesis mismatch")` sets `kBanned` state, records a 10-minute ban keyed on `host:listen_port`, and `remove_peer` closes the connection. The peer is scored invalid and removed. This is the intended behavior, but it is not triggered by real cross-network connections because of the shared defaults.

**I. Are all four canonical testnet validators guaranteed to use identical network identity settings?**  
YES, but trivially: they all use the same defaults (regtest magic, zero genesis), not the canonical testnet values. They agree with each other and with any regtest node, which is the defect.

**J. Are configuration errors detected at startup, or silently accepted?**  
Silently accepted. No startup check verifies that `network_magic`/`genesis_hash` are non-default or consistent with `--network`/config. `P2PNode` is constructed with whatever `NetworkConfig` contains (defaults).

## Impact

- **Security/isolation:** P2P network is not actually segmented by network. A regtest or misconfigured node can join a testnet mesh and exchange blocks/headers/addresses.
- **Consensus safety:** Not a consensus-rule change, but it removes a network-level isolation boundary that the handshake is supposed to enforce.
- **Testnet readiness:** Blocks confident TESTNET GO; cross-network contamination risk must be resolved or explicitly accepted.

## What is proven vs not proven

- Proven: defaults are regtest/zero; main.cpp does not assign magic/genesis; handshake validates both; cross-network handshake would pass due to shared defaults; probe confirmed magic `0x52505801` + zero genesis needed to talk to `--testnet` node.
- Not proven: no attempt was made to actually connect a regtest-configured daemon to a testnet daemon in this audit (inferred from code + probe). No live multi-network test performed.

## Remediation (requires owner approval)

See `docs/reports/PHANTOMX_NETWORK_IDENTITY_REMEDIATION_PROPOSAL.md`. Any change to network magic/genesis wiring, handshake semantics, or peer admission rules requires explicit owner approval before implementation. This audit does not implement anything.

## EXEC7B — Verification evidence (implementation applied under approved scope)

**Owner approval obtained (exec7b).** Implementation: `src/daemon/main.cpp` now sets `p2p_network.network_magic = network_magic(network)` and `p2p_network.genesis_hash = block_hash(genesis_block(network).header)`, with fail-closed on zero, plus diagnostic `p2p_network_identity`.

- Same-network 2-node smoke: node0 peers=1/ready=1, node1 peers=1/ready=1; identity log shows testnet magic `0x54505801` and genesis `98a75772...`.
- Cross-network smoke (testnet vs regtest): testnet node0 peers=0/ready=0; rejection "network magic mismatch" in node0 log.
- E2E 4-node regtest smoke: PASS (all 4 finalized height 1, same hash).
- PoI relay probe (testnet identity): PASS=true.
- Network tests: 4/4 PASS (3 new identity tests: match connects, mismatch rejected, zero genesis rejected).
- Unit: 137/137 PASS; CTest: 11/11 PASS.
- Build: 0 warnings (except pre-existing unused `get_port`).
- Binary SHA256: `7338730b1240ac9b2d7ba8e0ffb320b95cc5e7ac7234c5b21e1f7e7ff461121d` (pre-fix); post-fix rebuild SHA differs — record in exec7b report.

**Consensus/genesis change audit:** No change to genesis content, block format, monetary/reward rules, BFT finality, QC, quorum, proposer, fork-choice, or PoI semantics. Only wiring of existing canonical values into `NetworkConfig`.