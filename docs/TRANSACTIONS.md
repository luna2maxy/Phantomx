# PhantomX Transactions

## Overview

PhantomX uses a transparent UTXO transaction model. Amounts are in integer `phx_atom` (`1 PHX = 100,000,000 phx_atom`). Consensus-critical arithmetic uses checked integer operations; floating point is forbidden.

## Transaction Structure (v1)

Defined in `include/phantomx/primitives/transaction.hpp`.

| Field | Type | Description |
|---|---|---|
| `version` | u32 | Tx format version (`1`) |
| `kind` | u8 | `0=standard`, `1=coinbase` |
| `inputs[]` | TxIn[] | Inputs spending previous outputs |
| `outputs[]` | TxOut[] | New outputs |
| `locktime` | u32 | Locktime (foundation field) |

### TxIn

| Field | Type | Description |
|---|---|---|
| `prevout.txid` | hash256 | Previous transaction hash |
| `prevout.vout` | u32 | Output index |
| `script_sig` | bytes | Unlocking data/signature |
| `sequence` | u32 | Sequence number |

### TxOut

| Field | Type | Description |
|---|---|---|
| `amount_atoms` | u64 | Amount in `phx_atom` |
| `script_pubkey` | bytes | Locking script/program |

## Canonical Serialization

Transaction serialization is deterministic little-endian foundation encoding:
- `u8/u32/u64` fixed little-endian
- `bytes` = `u64 length + raw bytes`
- `txid = SHA-256d(serialized_tx)`

See `docs/TRANSACTION_FORMAT.md` for byte-level vectors.

## UTXO Set

`src/utxo/utxo.*` provides an in-memory `UtxoSet` keyed by `OutPoint(txid,vout)`.

`UtxoEntry` contains:
- `amount_atoms`
- `script_pubkey`
- `coinbase` flag
- `height`

## Validation Rules

Implemented in `src/utxo/validation.*`.

### Basic structural rules
- Supported tx version/kind.
- At least one input and one output.
- No duplicate inputs.
- Non-coinbase inputs cannot reference null prevout.
- Output amount > 0.
- Output amount <= `kMaxMoneyAtoms`.
- Sum of outputs checked for u64 overflow and <= `kMaxMoneyAtoms`.
- `script_pubkey` non-empty.

### Contextual rules (with UTXO set)
- Every input must exist in UTXO set.
- No integer overflow when summing input amounts.
- Sum(inputs) >= Sum(outputs).
- Fee = Sum(inputs) - Sum(outputs).
- Fee >= `kMinFeeAtoms` (foundation relay minimum).
- Signature verification must pass for each input (unless explicitly disabled for special tests).

## Signature Scheme (Foundation v1)

Simple Ed25519 P2PK-hash program for foundation testing:

- **script_pubkey:** `0x01 || 0x20 || sha256(pubkey32)` (34 bytes)
- **script_sig:** `signature64 || pubkey32` (96 bytes)
- **sighash:** `SHA-256d("PHX/sighash/v1" || serialize_transaction(tx_with_script_sigs_cleared_except_current))`

The current input's script_sig is set to the corresponding `script_pubkey` during sighash computation. All other script_sigs are cleared.

> This is a foundation scheme, not a final PhantomX script VM. Future versions must add domain separation, sighash flags, script versioning, and audited address/program types.

## Address Validation

`validate_address(address, expected_hrp)` uses bech32m decoding and HRP check. `script_pubkey_for_address` currently accepts versioned bech32m payloads of 32 bytes and maps them to the foundation P2PK-hash script.

## Fee Calculation

`fee_atoms = total_input_atoms - total_output_atoms`

Validation rejects insufficient funds and fees below `kMinFeeAtoms`.

## Mempool

`src/utxo/mempool.*` provides a simple mempool:
- Validates tx against UTXO set before admission.
- Rejects duplicate txids.
- Rejects mempool conflicts/double-spends (same outpoint already spent by mempool tx).
- Tracks fee and allows removal.

## Transaction Builder

`src/utxo/tx_builder.*` builds and signs standard transactions:
- Add inputs with outpoint, UTXO entry, secret key, public key.
- Add outputs with amount and script_pubkey.
- Checks output/input overflow, sufficient funds, minimum fee.
- Signs each input with Ed25519 sighash v1.

## Tests

`tests/unit/test_utxo.cpp` covers required cases:
- Valid transfer
- Invalid signature
- Double spend (duplicate input)
- Insufficient input
- Invalid output
- Overflow
- Serialization roundtrip
- Mempool conflict

## Security Notes

- No floating point in amount calculations.
- Checked u64 arithmetic for amount sums.
- Duplicate input rejection prevents in-tx double spend.
- Mempool tracks spent outpoints to prevent mempool conflicts.
- Signatures are verified against UTXO script_pubkey.
- Canonical serialization used for txid/sighash.