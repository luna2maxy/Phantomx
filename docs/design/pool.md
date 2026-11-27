# Proof-of-Internet Pool Design

Status: design only; no pool RPC or consensus code implemented.

## 1. Model and audit conclusion

The chain is UTXO-based and has no VM. PoI uses challenge/evidence, verifier signatures, fixed-point metrics/scores, verifier assignments, and aggregator certificates. The audit is recorded in `docs/design/pool-audit.md`.

The exact production proof encoding and consensus commitment are incomplete in the current chain. Pool APIs must therefore wrap node-accepted PoI operations, not invent a second proof format.

## 2. Selected architecture

Choose Architecture A initially: external pool service through node RPCs, no consensus change.

- Members submit PoI evidence or supported work records to the pool service.
- The service validates format, identity, assignment/challenge, signature, duplicate status, and verifier acceptance.
- The service submits accepted proof material through existing node APIs.
- Coinbase rewards go to the pool/miner UTXO address under the current chain policy.
- The service pays members off-chain only after the reward block is covered by `finalized_height` and the coinbase output is mature.
- Proofs and shares are bound to the member identity and assignment epoch; they cannot be reassigned across members or epochs.
- The operator does not hold member keys; operator signing keys remain outside the repository and logs.
- Reconciliation is tested: paid plus pending plus retained fee equals finalized rewards received.

Reasons: lowest consensus risk, matches current UTXO/coinbase code, avoids inventing worker coinbase semantics, and remains possible while block PoI commitments are incomplete.

Architecture B (multi-output coinbase to members) is a later activation-gated option only if custodial/off-chain payout is unacceptable. Architecture C (protocol pool registry/delegation) is rejected for now because current code does not show that A/B are insufficient.

## 3. Terms

Member: an identity/address contributing PoI evidence through an assigned challenge or verifier workflow.

Share: an accepted, signed, non-duplicated unit of verified member contribution associated with the current assignment/epoch when those fields exist. It is not a Bitcoin hash share and has no difficulty target unless the verifier protocol defines one.

Cheap share validation: parse canonical fields; verify member identity and signature; check challenge/assignment status; check epoch/height scope; reject duplicate idempotency key; check rate limits; optionally verify a cached verifier attestation. Full verifier work remains authoritative.

On-chain submission: a proof/certificate submission accepted by the node's existing validation path. A pool share alone is not on-chain and earns no reward until the accepted chain reward is observed.

## 4. Trust and custody

Default: custodial operator reward address with transparent accounting. The operator controls the pool address and pays members off-chain. This is simpler but requires operational custody controls and reconciliation.

The pool must never log private keys. Signing keys stay in an external secret manager or operator-controlled wallet process. Pool records identify members by public address/key, not secrets.

## 5. Payout

Default scheme: proportional per finalized round.

- Member weight equals accepted verified share units in the round.
- Gross member amount = distributable pool reward × member units / total accepted units.
- Integer division uses deterministic largest-remainder allocation; remainder atoms are assigned by canonical member-address order.
- Operator fee default: 1% of the pool reward before member distribution.
- Minimum payout default: 1,000 native atoms, subject to the operational constraint `minimum_payout > dust_limit + payout_transaction_fee`; amounts below threshold remain pending.
- Coinbase rewards also require coinbase maturity before spend. Payout occurs only after `finalized_height >= reward_block_height` and maturity is satisfied.
- Invalid, duplicate, late, or unattested shares earn zero.
- Shares from a non-final block remain pending and may be rolled back; finalized accounting is immutable.

Open owner decisions, with defaults used here:

1. Fee level? Default 1%.
2. Minimum payout? Default 1,000 native atoms.
3. Proportional, PPS, or PPLNS? Default proportional because the current code exposes participant work units and does not establish a safe PPS reserve.
4. Custodial or direct worker payout? Default custodial Architecture A because current coinbase is not worker-split.

## 6. Abuse model

- Sybil members: bind shares to verifier assignments and public identities; enforce existing anti-Sybil policy and service rate limits.
- Replay across members/epochs: bind to challenge/assignment/epoch/height fields available in the actual proof; require member signature and unique idempotency key.
- Share withholding: report submitted, accepted, rejected, and stale counts; payout only accepted shares.
- Operator theft: immutable round ledger, signed payout records, reconciliation, minimum audit export, and finality gate.
- Double submission: unique proof hash plus member/assignment scope; database uniqueness constraint.
- Invalid or late shares: explicit rejection code; no reward.
- Verifier/aggregator collusion: rely on the existing independent verifier quorum and certificate validation; do not replace it with pool claims.

## 7. RPC surface

All methods require authenticated node/service credentials, rate limiting, bounded page size, opaque pagination cursors, and explicit `final` status where results refer to chain state.

Read-only:

- `getpoiwork`: current assignment/challenge, required evidence fields, member scope, epoch/height, expiry, verifier requirements. Error codes: `UNAUTHORIZED`, `INVALID_PARAMS`, `NO_ACTIVE_ASSIGNMENT`, `NOT_FOUND`.
- `getpoistatus`: proof/assignment state, verifier attestations, chain inclusion, reward block, `final`. Error codes: `UNAUTHORIZED`, `INVALID_PARAMS`, `NOT_FOUND`.
- `pool_stats`: paginated round/member/share/reward totals, finalized and pending amounts, `final`. Error codes: `UNAUTHORIZED`, `INVALID_PARAMS`, `RATE_LIMITED`.

Write:

- `submitpoiproof`: submit a canonical proof/certificate through the existing node validation path. Error codes: `UNAUTHORIZED`, `INVALID_PARAMS`, `INVALID_SIGNATURE`, `INVALID_ASSIGNMENT`, `EXPIRED`, `DUPLICATE`, `RATE_LIMITED`, `VERIFIER_REJECTED`, `NODE_REJECTED`.
- `submitshare`: submit member-scoped share evidence for cheap validation and later verifier processing. Error codes: `UNAUTHORIZED`, `INVALID_PARAMS`, `INVALID_SIGNATURE`, `INVALID_SCOPE`, `EXPIRED`, `DUPLICATE`, `RATE_LIMITED`, `INVALID_SHARE`.

RPCs must not claim finality from mempool acceptance or RPC success. `final` is true only when the corresponding block height is covered by finalized state.

## 8. Accounting service

Tables/entities:

- `members(member_id, address, public_key, status, created_at)`
- `shares(share_id, member_id, assignment_id, epoch, proof_hash, status, received_at)`
- `rounds(round_id, start_height, end_height, status, total_units)`
- `rewards(round_id, txid, block_height, gross_atoms, finalized)`
- `payouts(payout_id, round_id, member_id, amount_atoms, idempotency_key, status)`

Require unique proof/idempotency keys, transactional state transitions, restart-safe replay, and deterministic rounding. Rollback is allowed only for non-final blocks. Reconciliation invariant:

`paid_atoms + pending_atoms + retained_fee_atoms = finalized_rewards_received_atoms`.

Chain reconciliation must verify pool-address UTXOs/transactions, not trust service-local reward claims.

## 9. Tests and monitoring

Required before production use:

- share signature/scope/duplicate/rate-limit validation;
- payout proportional math, zero units, overflow, and largest-remainder rounding;
- invalid/late share rejection;
- restart-safe idempotent accounting;
- four-node e2e with a pool service and multiple members;
- finality-gated reward and payout;
- chain restart/catch-up reconciliation.

UI additions must use live API data, never mocks:

- Pool page: members, rounds, accepted/rejected shares, rewards, payouts, finalized/pending state.
- Address/detail panel: member identity, share history, round attribution, payout history.
- All lists paginated and labeled finalized/pending.

## 10. Consensus and activation boundary

Architecture A changes no consensus and needs no activation. Any future Architecture B/C change requires a separate design revision, exact transaction/block byte formats, regtest activation parameter, migration plan, tests, and `docs/PROGRESS.md` entry before implementation.

No pool RPC implementation starts until P2P stabilization reaches Test A/B/C, full CTest, ASan/UBSan, and e2e 100/100, followed by liveness/multi-height gates.
