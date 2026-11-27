# PhantomX Network Protocol

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## 1. Network Identity

| Item | Value |
|---|---|
| Name | PhantomX |
| Symbol | PHX |
| Chain type | Independent Layer 1 |
| Ledger model | Transparent UTXO |
| Consensus | PoI + Verifier attestation Finality |
| Target block time | 60 seconds |
| Decimals | 8 (`1 PHX = 100,000,000 phx_atom`) |
| Address brand prefix | `PhantomX` (user-facing brand; canonical bech32m HRP is lowercase per network) |

## 2. Networks

| Network | Purpose | Magic | P2P port | RPC port | Address HRP | Genesis hash |
|---|---|---:|---:|---:|---|---|
| mainnet | Production network | `0x50485801` | 8742 | 8740 | `phantomx` | `7b647ef05c1f23959d5560fa4eb7380d8fa2141bc9aee4ba6b2785a76122a59e` |
| testnet | Public testing | `0x54505801` | 18742 | 18740 | `tphx` | `98a75772b9ab585f158a1b98adace23c19bb980838c026560cc1e8f8ae165569` |
| regtest | Local deterministic testing | `0x52505801` | 28742 | 28740 | `rphx` | `849ec5b41b3b7597c5e7e429c17826524498fb91b63c65f705a07f13477e8180` |

Network magic is sent in the P2P handshake and must match exactly. Nodes must reject cross-network connections.

## 3. Protocol Versioning

| Field | Scope | Current | Rule |
|---|---|---:|---|
| `protocol_version` | P2P handshake/capabilities | 1 | Bump on wire-incompatible changes; support negotiation window |
| `block_header.version` | Block header serialization/semantics | 1 | Bump on header layout/semantic changes |
| `tx.version` | Transaction serialization/semantics | 1 | Bump on tx layout/semantic changes |
| `address version` | Address payload program version | 0/1+ (payload-defined) | Bech32m version byte; unknown versions rejected unless enabled |

Compatibility rules:
- Minor/additive P2P messages: negotiate by capability bit/feature string.
- Breaking wire changes: increment `protocol_version`, provide upgrade window.
- Consensus serialization changes: version-gated and activation-height or network-flag gated.
- Unknown mandatory message/field: reject.

## 4. Block Header

### Block Header v1

| Field | Type | Description |
|---|---|---|
| `version` | u32 LE | Block header version (`1`) |
| `prev_hash` | hash256 | Previous block hash (32 bytes) |
| `merkle_root` | hash256 | Merkle root of transaction txids |
| `timestamp` | u64 LE | Unix epoch seconds |
| `height` | u32 LE | Block height |
| `nonce` | u32 LE | Version/consensus-defined nonce field |
| `bits` | u32 LE | Compact target/consensus parameter placeholder; meaning defined by consensus version |

Serialized header v1 is the concatenation of fields in order using foundation encoding. Hash: `block_hash = SHA-256d(serialize_block_header(header))`.

### Block Header Versioning and Backward Compatibility

**Rule:** Do not add block header fields without versioning and backward-compatibility analysis.

Required process before adding/changing a header field:

1. **Version bump or gated extension:** new field requires `block_header.version` increment, or a clearly specified extension mechanism with explicit activation.
2. **Compatibility analysis:** document how old nodes parse/validate, how new nodes handle old blocks, and replay impact.
3. **Activation rule:** define height/epoch/network flag for activation; avoid ambiguous dual-meaning bytes.
4. **Deterministic serialization:** publish exact byte layout, endianness, and test vectors.
5. **Consensus review:** ensure hash commitment, signature/QC coverage, and fork-choice/finality implications are reviewed.
6. **Migration/rollback policy:** define node behavior pre/post activation and reorg boundaries.

Backward-compatibility expectations:
- v1 nodes may reject unknown header versions.
- New nodes must continue to validate historical v1 blocks exactly as originally serialized.
- No silent reinterpretation of `bits`, `nonce`, or reserved fields without version/activation gate.

## 5. Block Serialization

Block v1 serialization:

| Field | Type | Description |
|---|---|---|
| `header_bytes` | bytes | Serialized BlockHeader v1 |
| `tx_count` | u32 | Number of transactions |
| `txs[]` | bytes[] | Serialized transactions in block order; tx[0] coinbase |

Block hash is computed over the header, not over the full block body.

## 6. Transaction Format

See [`TRANSACTION_FORMAT.md`](./TRANSACTION_FORMAT.md).

Summary:
- Transparent UTXO tx v1: version, kind, inputs, outputs, locktime.
- `txid = SHA-256d(serialized_tx)`.
- Amounts are integer `phx_atom`.
- Coinbase kind = 1.

## 7. Merkle Root

`merkle_root` is computed over txids in block order using SHA-256d binary Merkle tree; odd node count duplicates the last node at that level. See test vectors in `TRANSACTION_FORMAT.md`.

## 8. State Transition

A valid state transition applies a valid block to previous finalized/accepted chain state:

1. **Header validity:** version supported, prev_hash known, timestamp/height rules, hash meets consensus rules.
2. **Body validity:** block deserializes, tx count matches, coinbase present/valid.
3. **Merkle validity:** computed merkle root equals header merkle root.
4. **Tx validity:** signatures/scripts, UTXO existence, no double-spend, integer amount conservation.
5. **Consensus commitments:** PoI commitments/attestations and PoI+Verifier QC/finality metadata valid for active version.
6. **Economic validity:** subsidy/fees split rules, no premine/inflation violation, storage fees separate from subsidy.
7. **Atomic commit:** persist block, UTXO updates, consensus state, receipts/events atomically.

## 9. Consensus Commitment

PhantomX consensus is **PoI + Verifier attestation Finality**.

Block/chain commitments must include or reference (by versioned fields/extensions):
- PoI evidence commitment/root for block/epoch window.
- Verifier set/epoch identifier.
- PoI+Verifier proposal/round metadata.
- Quorum certificate (QC) or finality certificate reference.
- UTXO/state commitment (future header extension/version).

PoI contributes eligibility/scoring; PoI+Verifier provides finality. PoI alone is not Sybil resistance.

## 10. Address Encoding

- User-facing brand prefix: `PhantomX`.
- Canonical encoding: bech32m.
- HRP by network: `phantomx` (mainnet), `tphx` (testnet), `rphx` (regtest).
- Payload: version byte + program/payload.
- Validation: HRP, charset, checksum, version, payload conversion, network context.

## 11. Signature Scheme

**Foundation/proposed decision (ADR required before mainnet lock):**

| Use | Candidate | Status |
|---|---|---|
| Wallet/transaction signatures | Ed25519 or secp256k1 Schnorr (TBD) | Proposed |
| Verifier PoI+Verifier votes/QCs | BLS12-381 aggregate or Ed25519 (TBD) | Proposed |
| PoI/verifier/storage attestations | Ed25519 (TBD) | Proposed |

Requirements:
- Domain separation tags for every signed message type.
- Canonical serialization before signing/hashing.
- No key reuse across roles without explicit design.
- Test vectors required before enabling on consensus-critical paths.

## 12. P2P Handshake (Foundation)

Minimum handshake fields:
- `network_magic` (u32)
- `protocol_version` (u32)
- `node_id` (public key/hash)
- `genesis_hash` (hash256)
- `capabilities` (bitset/string list)
- `timestamp` and nonce

A peer is rejected if magic, genesis hash, or incompatible protocol version mismatches.

## 13. Security and Validation Rules

- Integer-only consensus arithmetic (`phx_atom`).
- Reject malformed serialization, trailing bytes, overlong counts, invalid checksums.
- Verify all signatures and evidence freshness before acceptance.
- Separate role keys: node, wallet, verifier, and verifier.
- No private keys or plaintext user files in logs/RPC/UI.