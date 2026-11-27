# PhantomX Transaction Format

## Overview

PhantomX uses a transparent UTXO transaction model. Amounts are integer `phx_atom` values (`1 PHX = 100,000,000 phx_atom`, 8 decimals). Floating point is forbidden in consensus decisions.

## Primitive Encoding

The foundation uses deterministic little-endian binary encoding.

| Type | Encoding |
|---|---|
| `u8` | 1 byte |
| `u32` | 4 bytes, little-endian |
| `u64` | 8 bytes, little-endian |
| `bytes` | `u64 length` + raw bytes |
| `string` | `u64 length` + UTF-8 bytes |
| `hash256` | 32 bytes, written as `bytes` in current foundation encoding (length-prefixed) |
| `bool/enum` | explicit integer size; do not encode as platform-dependent bool |

> Note: The foundation serialization currently length-prefixes hash fields via `write_bytes`. This is deterministic but verbose. A future canonical v2 may encode fixed-size hashes inline; any such change requires versioning and backward compatibility handling.

## Transaction v1

| Field | Type | Description |
|---|---|---|
| `version` | u32 | Transaction format version. v1 = `1` |
| `kind` | u8 | `0=standard`, `1=coinbase` (future kinds require version/kind rules) |
| `input_count` | u32 | Number of inputs |
| `inputs[]` | TxIn | Transaction inputs |
| `output_count` | u32 | Number of outputs |
| `outputs[]` | TxOut | Transaction outputs |
| `locktime` | u32 | Locktime/height-time rule (consensus-defined) |

### TxIn v1

| Field | Type | Description |
|---|---|---|
| `prevout.txid` | bytes/hash256 | Referenced transaction ID |
| `prevout.vout` | u32 | Output index |
| `script_sig` | bytes | Unlocking/signature script (foundation placeholder) |
| `sequence` | u32 | Sequence; default `0xFFFFFFFF` |

### TxOut v1

| Field | Type | Description |
|---|---|---|
| `amount_atoms` | u64 | Amount in smallest unit (`phx_atom`) |
| `script_pubkey` | bytes | Locking script/address program (foundation placeholder) |

## Transaction ID

`txid = SHA-256d(serialize_transaction(tx))`

- Hash is over the canonical serialized transaction bytes for the declared tx version.
- Display form is hex; internal consensus uses raw 32 bytes.
- Any future segwit-like/signature-separated format must define `txid` and `wtxid`/equivalent explicitly with versioning.

## Merkle Root

Block merkle root is computed from transaction IDs in block order:

1. List txids as 32-byte hashes.
2. If empty: root = 32-byte zero (genesis/special rules should avoid empty non-genesis blocks).
3. If one tx: root = txid.
4. While more than one hash: if odd, duplicate last; hash pairs as `SHA-256d(left || right)`.
5. Final remaining hash is merkle root.

## Address Encoding

PhantomX user-facing addresses use the **`PhantomX`** brand/prefix requirement. Canonical on-wire/user string encoding uses **bech32m**.

| Network | HRP | Example |
|---|---|---|
| mainnet | `phantomx` | `phantomx1...` |
| testnet | `tphx` | `tphx1...` |
| regtest | `rphx` | `rphx1...` |

### Address Payload

`encode_phantomx_address(hrp, version, payload)`:

1. `version` is 0..31 (5-bit).
2. Convert payload bytes 8-bit -> 5-bit (pad).
3. Encode as bech32m: `hrp + "1" + data + checksum`.
4. Validate checksum, HRP, character set, and payload conversion on decode.

### Address Test Vectors

| Label | HRP | Version | Payload hex | Address |
|---|---|---:|---|---|
| mainnet_p2pkh_v0 | `phantomx` | 0 | `000102030405060708090a0b0c0d0e0f10111213` | `phantomx1qqqqsyqcyq5rqwzqfpg9scrgwpugpzysnazx7fn` |
| mainnet_p2sh_v1 | `phantomx` | 1 | `000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f` | `phantomx1pqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0swgfusy` |
| testnet_v0 | `tphx` | 0 | `000102030405060708090a0b0c0d0e0f10111213` | `tphx1qqqqsyqcyq5rqwzqfpg9scrgwpugpzysnwzt8ta` |
| regtest_v0 | `rphx` | 0 | `000102030405060708090a0b0c0d0e0f10111213` | `rphx1qqqqsyqcyq5rqwzqfpg9scrgwpugpzysntw2amr` |

## Serialization Test Vector

### sample_tx_v1

Input tx:
- version: 1
- kind: 0 (standard)
- input[0]: prevout.txid=`11..11` (32 bytes), vout=1, script_sig=`76a914`, sequence=`0xFFFFFFFF`
- output[0]: amount_atoms=`2500000000` (25 PHX), script_pubkey=`88ac`
- locktime: 0

Expected serialized hex:

`010000000001000000200000000000000011111111111111111111111111111111111111111111111111111111111111111101000000030000000000000076a914ffffffff0100000000f9029500000000020000000000000088ac00000000`

Expected txid (SHA-256d):

`d93e9b453dd36421d6507b0e41c7929a617da383535dc85492d749497a93b83b`

## State Transition (Foundation)

For a block to be valid (foundation scope):

1. Parse/deserialize block and transactions by declared versions.
2. Verify header fields and block hash rules for network.
3. Verify merkle root matches transaction txids in order.
4. Verify coinbase rules (height/reward/uniqueness) per consensus version.
5. For each non-coinbase tx: inputs exist/unspent, scripts/signatures valid, no double spend in block/chain context, amounts non-negative, sum(inputs) >= sum(outputs)+fee, all arithmetic in integer `phx_atom`.
6. Apply UTXO updates atomically: remove spent outputs, add new outputs.
7. Apply consensus/finality/storage/PoI commitments per active protocol version.

## Versioning / Compatibility

- `version` fields are mandatory in block header and transaction.
- Unknown required fields/kinds must be rejected unless explicitly allowed by compatibility rules.
- Adding fields requires a new version or an explicitly version-gated extension mechanism.
- Nodes must not reinterpret old serialized bytes under new rules without a version bump and activation rule.
- Consensus-critical serialization changes require golden test vectors and activation height/network rule.