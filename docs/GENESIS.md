# PhantomX Genesis

## Overview

PhantomX genesis is **independently generated** for PhantomX. It does not reuse or derive from PhantomX or Dogecoin genesis blocks, messages, hashes, or networks.

- Premine: **0**
- Consensus: **PoI + Verifier attestation** (foundation phase; genesis anchors chain identity)
- Ledger: **Transparent UTXO**
- Target block time: **60 seconds** (regtest may use shorter interval for local testing)

## Network Genesis Parameters

| Network | Magic | HRP | Timestamp (UTC) | Genesis message | Merkle root | Genesis block hash |
|---|---|---|---:|---|---|---|
| mainnet | `0x50485801` | `phantomx` | 1767225600 (`2026-01-01T00:00:00Z`) | `PhantomX Genesis mainnet` | `3751e9c10bd8a5297104311ac0c9338cf59e8d9cac4d2becf77cb9382ecca155` | `7b647ef05c1f23959d5560fa4eb7380d8fa2141bc9aee4ba6b2785a76122a59e` |
| testnet | `0x54505801` | `tphx` | 1767225601 (`2026-01-01T00:00:01Z`) | `PhantomX Genesis testnet` | `581d7e47fe4a726405e1eb3f28c8fb51828e9adbc96570e76365ba7a0bcd4711` | `98a75772b9ab585f158a1b98adace23c19bb980838c026560cc1e8f8ae165569` |
| regtest | `0x52505801` | `rphx` | 1767225602 (`2026-01-01T00:00:02Z`) | `PhantomX Genesis regtest` | `61a0774e1ad61becf01e38ed7b70600d446e9728c9da01a8ae06503d55e232bf` | `849ec5b41b3b7597c5e7e429c17826524498fb91b63c65f705a07f13477e8180` |

## Genesis Block Structure

Genesis block (all networks) uses **Block Header v1** and one coinbase transaction.

### Block Header v1

| Field | Type | Value (mainnet) | Description |
|---|---|---|---|
| `version` | u32 LE | 1 | Header format version |
| `prev_hash` | 32 bytes | all-zero | No previous block |
| `merkle_root` | 32 bytes | see table | Merkle root of txids |
| `timestamp` | u64 LE | 1767225600 | Unix epoch seconds |
| `height` | u32 LE | 0 | Genesis height |
| `nonce` | u32 LE | 0 | Reserved/consensus-defined |
| `bits` | u32 LE | `0x1d00ffff` | Compact target placeholder; consensus meaning versioned |

### Coinbase Transaction v1

| Field | Value | Description |
|---|---|---|
| `version` | 1 | Transaction format version |
| `kind` | 1 (`coinbase`) | Coinbase transaction |
| `inputs[0].prevout.txid` | 32-byte zero | Null prevout |
| `inputs[0].prevout.vout` | `0xFFFFFFFF` | Coinbase marker |
| `inputs[0].script_sig` | ASCII `PhantomX Genesis <network>` | Genesis message; network-specific |
| `inputs[0].sequence` | `0xFFFFFFFF` | Final |
| `outputs[0].amount_atoms` | 0 | Premine 0 |
| `outputs[0].script_pubkey` | ASCII `OP_RETURN PHANTOMX_GENESIS` | Non-spendable marker output |
| `locktime` | 0 | No locktime |

## Genesis Test Vectors

### Mainnet

- Coinbase txid: `3751e9c10bd8a5297104311ac0c9338cf59e8d9cac4d2becf77cb9382ecca155`
- Merkle root: `3751e9c10bd8a5297104311ac0c9338cf59e8d9cac4d2becf77cb9382ecca155`
- Block header hex: `010000002000000000000000000000000000000000000000000000000000000000000000000000000000000020000000000000003751e9c10bd8a5297104311ac0c9338cf59e8d9cac4d2becf77cb9382ecca15500b95569000000000000000000000000ffff001d`
- Block hash (SHA-256d of header): `7b647ef05c1f23959d5560fa4eb7380d8fa2141bc9aee4ba6b2785a76122a59e`
- Coinbase tx hex: `01000000010100000020000000000000000000000000000000000000000000000000000000000000000000000000000000ffffffff18000000000000005068616e746f6d582047656e65736973206d61696e6e6574ffffffff0100000000000000000000001a000000000000004f505f52455455524e205048414e544f4d585f47454e4553495300000000`

### Testnet

- Coinbase txid / Merkle root: `581d7e47fe4a726405e1eb3f28c8fb51828e9adbc96570e76365ba7a0bcd4711`
- Block hash: `98a75772b9ab585f158a1b98adace23c19bb980838c026560cc1e8f8ae165569`

### Regtest

- Coinbase txid / Merkle root: `61a0774e1ad61becf01e38ed7b70600d446e9728c9da01a8ae06503d55e232bf`
- Block hash: `849ec5b41b3b7597c5e7e429c17826524498fb91b63c65f705a07f13477e8180`

## Genesis Validation Rules

A node must reject a chain whose genesis block does not match the expected network genesis hash and serialized header/body constraints.

Required checks:
- Network magic matches selected network.
- Genesis height is 0 and prev_hash is all-zero.
- Genesis header hash equals configured network genesis hash.
- Genesis merkle root matches coinbase txid(s).
- Coinbase amount does not create premine; mainnet premine is 0.
- Serialization is canonical for the declared versions.

## Implementation References

- `src/blockchain/genesis.cpp`
- `include/phantomx/blockchain/genesis.hpp`
- `config/mainnet.json`
- `config/testnet.json`
- `config/regtest.json`
- `tests/unit/test_protocol_vectors.cpp`
