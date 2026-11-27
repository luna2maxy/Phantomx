# PhantomX P2P Network

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Overview

PhantomX P2P is an independent TCP framed protocol for block/transaction propagation, peer discovery, and block synchronization. It is a foundation implementation (not production-hardened).

## Protocol

- **Transport:** TCP, length-prefixed framed binary messages.
- **Frame:** `network_magic(u32 LE) | type(u8) | reserved(u8) | payload_len(u32 LE) | checksum(u32 LE) | payload`.
- **Checksum:** first 4 bytes of `SHA-256d(payload)`.
- **Network magic:** must match configured network (`mainnet/testnet/regtest`); mismatches rejected.
- **Protocol version:** `1`; incompatible versions rejected.
- **Max message size:** 2 MiB (configurable limit in code, hard cap enforced).

## Message Types

| Type | Purpose |
|---|---|
| `HELLO` / `HELLO_ACK` | Handshake, protocol negotiation, identity/genesis validation |
| `PING` / `PONG` | Liveness |
| `GET_ADDR` / `ADDR` | Peer discovery |
| `INV` / `GET_DATA` | Inventory announcement and data request |
| `BLOCK` / `TX` | Block/transaction propagation |
| `GET_HEADERS` / `HEADERS` | Block synchronization (foundation header/hash list sync) |
| `REJECT` | Rejection reason (foundation) |

## Handshake

HELLO contains: network magic, protocol version, nonce, timestamp, genesis hash, agent, listen port. A peer is accepted only if magic, protocol version, and genesis hash match and nonce is not self-connection.

## Features

- **Peer discovery:** seed connect, `GET_ADDR/ADDR`, remembered addresses.
- **Handshake/protocol negotiation:** HELLO/HELLO_ACK with magic/genesis/protocol checks.
- **Peer management:** peer table, ready state, inbound/outbound tracking.
- **Block propagation:** INV/BLOCK, seen-hash dedup, apply on tip match.
- **Transaction propagation:** INV/TX, mempool validation before admission.
- **Block synchronization:** GET_HEADERS/HEADERS hash list + GET_DATA/BLOCK retrieval.
- **Peer scoring:** invalid message/timeout/spam penalties, good-message reward.
- **Peer banning:** score threshold or severe violation -> temporary host ban.
- **Reconnection:** maintenance loop reconnects seeds when no ready peers.
- **Timeouts:** recv timeout, handshake timeout, maintenance cleanup.
- **Message validation:** size limit, checksum, magic, payload decode, inventory limits.

## Security Controls

| Control | Implementation |
|---|---|
| Message size limits | `kMaxMessageSize` / config `max_message_size`, reject oversized |
| Connection limits | `max_connections`, `max_inbound` |
| Rate limiting | per-peer per-second message window (`rate_limit_per_sec`) |
| Malformed message rejection | decode/checksum/type/payload validation, score penalty |
| Replay protection | seen-hash cache for tx/block inventory/data |
| Eclipse attack mitigation (foundation) | seed reconnect, address table, inbound/outbound limits; needs peer diversity/anchor slots for production |
| DoS protection | rate limits, message size, bans, timeouts, inventory/addr count caps |
| Resource limits | bounded addr/inv counts, seen-hash cache cap, connection caps |

## Simulation

`tests/network/test_p2p.cpp` runs a local multi-node simulation:
- starts two nodes with independent regtest chain DBs and mempools,
- connects node2 to node1,
- applies a spendable coinbase block on node1, broadcasts/syncs to node2,
- builds a signed tx on node2, broadcasts it, verifies node1 mempool receives it.

## Limitations

- Foundation sync is simple hash-list/header sync, not full locator/headers-first production sync.
- Block application is tip-only/simple; no full reorg/fork-choice/PoI+Verifier finality integration.
- Peer discovery lacks DNS seeds, addr persistence, anchor connections, and AS/diversity protections.
- No encryption/authenticated transport; node identity keys and message signing should be integrated.
- Thread-per-peer model is acceptable for foundation/tests, not high-scale production.
- Bans are in-memory and time-limited; persistent misbehavior store needed.