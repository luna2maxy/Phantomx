# Proof-of-Internet Pool Audit

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


Status: audit only; no node or consensus code changed.

## Chain model

The chain is UTXO-based, not account-based, and no VM was found.

Evidence:
- `include/phantomx/utxo/utxo.hpp`: `UtxoEntry`, `UtxoSet`.
- `src/blockchain/chain.cpp`: SQLite `utxos` table and `ChainDatabase::validate_block`/UTXO application.
- `include/phantomx/primitives/transaction.hpp`: transaction inputs and outputs.
- `src/utxo/validation.cpp`: input/prevout validation.

## PoI proof contents and measurement

The code models PoI as a score and evidence pipeline, not as hash-mining work:

- `include/phantomx/poi/types.hpp`: `PoiMetrics`, `EvidenceRecord`, score-related types and verifier identity fields.
- `include/phantomx/poi/measurement/measurement.hpp`: measurement records include verifier identity and measured metrics.
- `src/poi/score.cpp` and `include/phantomx/poi/score.hpp`: deterministic fixed-point score calculation.
- `include/phantomx/poi/evidence/evidence.hpp`: evidence records and `sign_evidence`.
- `include/phantomx/poi/challenge/challenge.hpp`: verifier-selected challenge construction.

The available source indicates measured network/Internet properties are represented by `PoiMetrics` and converted to a bounded score. The exact production proof payload, canonical wire encoding, and height/epoch binding are not yet demonstrated by the current block format. This is a design blocker for protocol-level pool shares.

Identity is represented by public keys:
- `include/phantomx/poi/identity/identity.hpp`.
- `include/phantomx/poi/evidence/evidence.hpp`.
- `include/phantomx/verifier/service.hpp`.

The audit did not find sufficient evidence that an arbitrary worker proof is currently bound to a block height or epoch. Do not invent such binding in pool RPCs; use the challenge/assignment fields actually accepted by the verifier path.

## Verification and aggregation

Verifier flow:

- `include/phantomx/verifier/assignment.hpp`: assignment and verifier selection.
- `include/phantomx/verifier/registry.hpp`, `src/verifier/registry.cpp`: verifier registry, status, reputation, reward accounting.
- `include/phantomx/verifier/service.hpp`: `verify_and_sign`, signed verifier results.

Aggregator flow:

- `include/phantomx/aggregator/aggregator.hpp`: verifier attestations, required quorum, assignment, certificate, aggregator signature.
- `src/aggregator/aggregator.cpp`: aggregation/certificate implementation.
- `include/phantomx/aggregator/aggregator.hpp`: `required` defaults to independent verifier attestations.

The current source has verifier and aggregator certificates as service/state objects. The block validation path does not yet demonstrate that a PoI evidence certificate is stored as a consensus block commitment. `docs/DATABASE.md` explicitly notes that full PoI/PoI+Verifier consensus commitments remain incomplete. Therefore pool submissions must not be treated as on-chain consensus proofs until that gap is closed.

## Roles and RPCs

Roles:

- Proposer: selected by PoI+Verifier engine; `include/phantomx/consensus/poi+verifier.hpp` and `src/consensus/poi+verifier.cpp`.
- Verifiers: PoI+Verifier voting power and QC logic; `include/phantomx/consensus/consensus.hpp` and PoI+Verifier implementation.
- Verifiers: registry and signed evidence results above.
- Aggregators: certificate creation and verifier quorum above.

Existing RPC registration:

- `src/rpc/node_handlers.cpp`: `getpoiinfo`, `verifier_list`, `getaggregatorinfo`, `getvalidatorinfo`.
- `src/rpc/rpc.cpp`: method authentication/role registration.

These RPCs are node APIs; pool service APIs must not assume they are currently public or unauthenticated.

## Rewards and coinbase

Current reward path is UTXO coinbase based:

- `src/rpc/node_handlers.cpp`: `generatetoaddress`/block generation constructs a coinbase transaction.
- `src/rpc/node_handlers.cpp`: reward split uses `phantomx::rewards::block_subsidy_atoms`, `kPoiShareBps`, and `kVerifierShareBps`; `getblocktemplate` exposes `coinbase_split`.
- `include/phantomx/rewards/rewards.hpp`, `src/rewards/rewards.cpp`: epoch participant/reward calculations and reward commitments.
- `src/blockchain/chain.cpp`: coinbase-first and subsidy/UTXO validation.

Observed current policy: coinbase pays the pool/miner reward address and may pay the rolling verifier address. This is not multi-output worker payout. The code does not yet prove that reward payment is gated by `finalized_height`; that must be enforced by any external accounting service before payout.

## Replay, Sybil, collusion

Present mechanisms include:

- challenge assignment and verifier identity: `include/phantomx/poi/challenge/challenge.hpp`;
- signed evidence/verifier results: `include/phantomx/poi/evidence/evidence.hpp`, `include/phantomx/verifier/service.hpp`;
- verifier registry and eligibility: `include/phantomx/verifier/registry.hpp`;
- endpoint-prefix anti-Sybil policy: `include/phantomx/poi/anti_sybil/anti_sybil.hpp`;
- aggregator deduplication/quorum fields: `include/phantomx/aggregator/aggregator.hpp`.

The audit did not confirm complete replay protection across pool members and epochs, nor a consensus-enforced rate limit for pool shares. Pool design must therefore use challenge/assignment IDs, verifier signatures, epoch/height when present, member address, and an idempotency key; it must reject duplicate submissions at the accounting boundary.

## Audit conclusion

Safe initial architecture: external pool service using existing node APIs, with no consensus change. Pool members submit evidence to the operator; the operator submits only node-accepted proofs/transactions; the chain pays the pool/miner UTXO address; off-chain accounting pays members after finality. Architecture B (worker coinbase outputs) requires a separate consensus design and activation. Architecture C is unjustified by current evidence.
