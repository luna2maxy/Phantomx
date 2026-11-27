# PHANTOMX — Network Identity Remediation Proposal (TB-023)

**Date:** 2026-11-26  
**Status:** PROPOSAL — no implementation. Owner approval required.  
**Related:** `docs/reports/PHANTOMX_NETWORK_IDENTITY_AUDIT.md`, TB-005, TB-015.

## Problem

`src/daemon/main.cpp` leaves `NetworkConfig::network_magic` and `NetworkConfig::genesis_hash` at defaults (`0x52505801`, zero) for every network. The handshake (`src/network/node.cpp:591-592`) validates both fields, but because all nodes share the same defaults, the gate is vacuous. Testnet, regtest, and (if wired) mainnet nodes can all peer with each other.

## Proposed remediation (owner decision required)

1. In `src/daemon/main.cpp`, after `resolve_network(...)`:
   - Set `p2p_network.network_magic = phantomx::network_magic(network)` (or the config's `protocol.network_magic` if present and validated).
   - Set `p2p_network.genesis_hash` to the selected network's genesis block hash (from `genesis_block(network).header...` or config `genesis.block_hash`).
2. Validate at startup that the configured `network_magic`/`genesis_hash` match the resolved network; fail fast on mismatch (currently silently accepted).
3. Optionally add a compatibility/version gate so older binaries cannot join a network after the change (rolling-upgrade consideration).
4. Add regression tests: two nodes with different magic/genesis must not handshake; same-network nodes must.

## Impact / protected-behavior assessment

- **Consensus rules:** no change.
- **Block format / genesis / monetary / reward / PoI semantics:** no change. The genesis block itself is unchanged; only the P2P handshake identity value is wired to the already-defined canonical value.
- **Network protocol:** the handshake fields already exist and are already validated; this wires the correct values. This is a behavior change in which peers can connect, but not a change to handshake serialization or admission rules.
- **Risk:** any node built before the change (using regtest defaults) would be unable to join a post-change testnet. For a fresh testnet this is acceptable; for an existing mesh it requires coordinated restart. No live testnet exists yet (VPS frozen), so risk is low.
- **Rollout:** all four canonical validators must be restarted together with the new binary.

## Approval required

This is a network-identity/behavior change. Per auto-approval boundary, implementation requires explicit owner approval. This document is a proposal only; no code was changed.

## Validation plan (post-approval)

- Unit: handshake rejects mismatched magic/genesis; accepts matching.
- Integration: 4 testnet validators handshake and reach 3/3 mesh with testnet magic.
- Negative: regtest-magic node cannot join testnet mesh.
- Evidence captured in a new evidence dir; matrix re-run on the new binary.