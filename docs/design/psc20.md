# PSC20 Native Token Ledger Design

Status: design only; implementation blocked by project preconditions.
Date: 2026-11-24

## 1. Audit and model decision

The current chain is UTXO-based:

- `Transaction` contains `TxIn`/`TxOut`.
- `UtxoSet` stores `OutPoint -> UtxoEntry`.
- Transaction validation consumes inputs and creates outputs.
- Blocks commit transactions through the merkle root.

No contract VM or smart-contract execution engine was found under `include/` or `src/`. Wallet accounts are key/keystore abstractions over UTXOs, not account-state consensus.

Decision: native protocol-level PSC20 ledger. PSC20 state is consensus state updated by dedicated transaction kinds. No VM is introduced.

## 2. Scope

In scope:

- Token creation.
- Token transfer.
- Balance and total-supply queries.
- Optional mint and burn, only with explicit authority.
- Native PHX fees.

Out of scope initially:

- `approve`, `allowance`, and `transferFrom`; delegated spending is deferred until a concrete use case exists.
- Contract execution.
- Cross-chain bridges.
- Hash-mining or pool semantics.

## 3. Chain parameters and activation

PSC20 activation is a regtest-only network parameter first. Testnet/mainnet remain disabled until separate activation values are defined.

Required parameters:

- `chain_id`: non-empty canonical byte string, unique per network.
- `psc20_activation_height`: height at which PSC20 transaction kinds become valid.
- Native coin fee policy.

Before activation, PSC20 transaction kinds are invalid. After activation, all verifiers use the same parameter set.

## 4. Token identity and limits

`token_id = H("PSC20_TOKEN_ID" || chain_id || creator_pubkey || creator_nonce)`.

- `H` is the chain hash function.
- `creator_pubkey` is the canonical creator key bytes.
- `creator_nonce` is an unsigned 64-bit integer, canonical big-endian.
- The tuple must be unique in the consensus state; duplicate creation is invalid.
- Name and symbol use canonical UTF-8 with length limits defined by ChainParams.
- Decimals are an unsigned integer within the ChainParams maximum.
- Supply values are unsigned fixed-width integers with checked arithmetic.
- `max_supply` is immutable. `initial_supply <= max_supply`.

The creator receives initial supply. A mint authority is optional and identified by canonical public-key bytes. If absent, mint is permanently disabled.

## 5. Transaction kinds and canonical byte layout

New transaction kinds are consensus-visible and require design review before implementation:

- `kTxKindPsc20Create = 2`
- `kTxKindPsc20Transfer = 3`
- `kTxKindPsc20Mint = 4`
- `kTxKindPsc20Burn = 5`

All PSC20 transactions use the existing transaction envelope plus a canonical PSC20 payload. The exact codec must be implemented and round-trip tested before activation.

Canonical payload fields, in order:

Create:

1. domain tag `PSC20_CREATE` (length-prefixed bytes)
2. `chain_id` (length-prefixed bytes)
3. creator public key (fixed canonical key bytes)
4. creator nonce (`u64`, big-endian)
5. name (length-prefixed UTF-8)
6. symbol (length-prefixed UTF-8)
7. decimals (`u8`)
8. initial supply (`u128`)
9. max supply (`u128`)
10. mint-authority-present (`u8` 0/1)
11. optional mint authority public key
12. PHX fee (`u64` atoms)
13. creator signature

Transfer:

1. domain tag `PSC20_TRANSFER`
2. `chain_id`
3. token id (fixed hash bytes)
4. sender public key
5. recipient public key
6. amount (`u128`)
7. PHX fee (`u64` atoms)
8. sender signature

Mint:

1. domain tag `PSC20_MINT`
2. `chain_id`
3. token id
4. mint-authority public key
5. recipient public key
6. amount (`u128`)
7. PHX fee
8. authority signature

Burn:

1. domain tag `PSC20_BURN`
2. `chain_id`
3. token id
4. holder public key
5. amount (`u128`)
6. PHX fee
7. holder signature

The final implementation must specify integer encoding, maximum lengths, signature encoding, and rejection of trailing bytes in the codec tests.

## 6. Signature digest and replay protection

Every PSC20 signature digest includes:

`H("PHANTOMX_TX_SIG" || chain_id || tx_version || tx_kind || canonical_payload || fee_context)`.

The chain ID is mandatory. A signature created for one network must fail validation on another network even when all other fields match.

Transaction IDs hash the canonical complete transaction bytes, including kind and payload.

## 7. Validity rules

Reject transactions when any of these hold:

- PSC20 kind appears before activation.
- Chain ID mismatches the active ChainParams.
- Encoding is malformed, non-canonical, oversized, or has trailing bytes.
- Signature is invalid or signer does not control the referenced balance/authority.
- Amount is zero where zero is disallowed, negative, or overflows.
- Sender balance is insufficient.
- Token does not exist.
- Token creation identity already exists.
- `initial_supply > max_supply`.
- Mint would exceed `max_supply`.
- Burn exceeds holder balance.
- Any balance or total-supply arithmetic overflows.
- Fee is unpaid or native UTXO validation fails.

## 8. State transition order

Within each block, transactions execute in block order:

1. Validate native UTXO inputs and signatures.
2. Validate PSC20 payload and chain ID.
3. Deduct/validate native PHX fee according to existing fee rules.
4. Apply PSC20 transition to a block-local state view.
5. Apply native UTXO changes.
6. Commit the resulting state only after every transaction and block rule validates.

A later transaction in the same block may spend a balance created by an earlier valid PSC20 transaction. If any transaction fails, the block is invalid; partial state changes are discarded.

## 9. State commitment

PSC20 state must be committed in consensus. Preferred design: deterministic key-value serialization of token metadata, total supply, and `(token_id, owner)` balances, hashed into a `psc20_state_root`.

The block header must commit this root before activation is enabled. Adding a header field is consensus-visible and requires a separate activation/migration decision. Until that decision is approved, implementation must not silently keep an uncommitted token ledger.

## 10. Genesis

No PSC20 tokens exist in genesis by default. Regtest fixtures may define explicit genesis tokens only through ChainParams and a deterministic genesis commitment. Mainnet/testnet genesis handling requires a separate activation design.

## 11. RPC contract

Authenticated, read-only RPCs:

- `token_info(token_id)`
- `token_balance(token_id, owner)`
- `token_holders(token_id, cursor, limit)`
- `token_transfers(token_id, address, cursor, limit)`

Creation and mutation use signed raw transaction submission; no RPC bypasses consensus validation.

`getrawtransaction` must decode PSC20 kinds without changing transaction bytes.

UI contract additions:

- `tokens(token_id, chain_id, name, symbol, decimals, creator, max_supply, total_supply, mint_authority)`
- `token_balances(token_id, owner, balance, height)`
- `token_transfers(txid, token_id, sender, recipient, amount, kind, height)`

All list endpoints are paginated and indexed by token/address/height.

## 12. Test plan

Before implementation:

- Unit tests for create, transfer, mint, burn, overflow/underflow, duplicate token ID, unauthorized mint, insufficient balance, malformed/trailing bytes, and cross-chain replay.
- Block tests for multiple ordered PSC20 transactions and invalid block rejection.
- 4-node e2e for create, transfer, identical finalized token state, restart/catch-up, and identical state roots.
- Zero-warning build, CTest, ASan/UBSan.

Tests must be written failing first and must fail against the old implementation before production code is added.

## 13. Risks and migration

Risks:

- Header/state-root changes affect block validity.
- Incorrect canonical encoding can create divergent state.
- Missing chain-ID binding enables cross-network replay.
- Unbounded integer arithmetic can inflate supply.

Migration:

- Regtest activation height and parameter flag first.
- No testnet/mainnet activation until state commitment, replay protection, and restart/catch-up are proven.
- Existing pre-activation blocks remain valid under the old rules.
- Any header or serialization change requires explicit activation and migration documentation.

## 14. Preconditions

Implementation is blocked until the current project gates pass:

- zero-warning build;
- full CTest;
- ASan/UBSan;
- 4-node e2e 100/100 normal;
- subsequent loaded, multi-height, restart/catch-up validation.
