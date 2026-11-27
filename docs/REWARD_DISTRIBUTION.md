# PhantomX Reward Distribution

## Purpose

This document defines the deterministic reward distribution mechanism for PHX block subsidy.

## Inputs

Per epoch snapshot:

- `epoch`, `start_height`, `end_height`
- `total_subsidy_atoms` (sum of block subsidies in epoch, integer atoms)
- PoI contributor work units
- Verifier operator work units
- Pool operator work units

All work units must be deterministically derived from validated evidence/consensus records.

## Category Split

Total subsidy is split by integer basis points:

- PoI contributors: `70,000 / 100,000`
- Verifier operators: `20,000 / 100,000`
- Pool operators: `10,000 / 100,000`

Calculation:

- `verifier_atoms = floor(total * 20000 / 100000)`
- `pool_atoms = floor(total * 10000 / 100000)`
- `poi_atoms = total - verifier_atoms - pool_atoms`

This ensures the sum equals total subsidy with no floating point.

## Proportional Distribution

For each category:

1. Canonicalize participants: remove zero id/work, sort by id bytes, merge duplicate IDs by summing work units.
2. Compute `total_work`.
3. For each participant in canonical order: `amount = floor(category_atoms * work_units / total_work)`.
4. Assign any rounding remainder to the first canonical participant in that category to preserve exact total.

If a category has no eligible participants, its atoms are **unclaimed by policy** and must be handled by explicit governance/consensus rule (e.g., carry forward, burn, or treasury) - the foundation does not silently reassign to other categories.

## Reward Commitment

Reward commitment includes:

- epoch
- total reward atoms
- Merkle root
- canonical reward entries (or off-chain storage with proofs)

Each leaf hash is domain-separated: `SHA-256d("PHX/reward/leaf/v1" || recipient_id || amount_atoms || category_bytes)`.

The Merkle root allows efficient claim proofs. An alternative settlement transaction model can carry the same commitment on-chain; Merkle claims are preferred for large participant sets.

## Claim Validation

A claim is valid if:

1. Claim epoch matches commitment epoch.
2. Leaf hash for `(recipient_id, amount_atoms, category)` is included in Merkle proof against commitment root.
3. Claim has not been previously claimed (`double claim prevention`).
4. Amount is non-zero and within commitment rules.

## Double Claim Prevention

Claim registry key: `epoch:recipient_hex:category`.

- First valid claim marks key as claimed.
- Any second claim with same key is rejected.

Production should persist claim registry in chainstate/DB and include claim nullifiers in settlement state transitions.

## Unclaimed Rewards

- Unclaimed rewards remain associated with the commitment until policy expiry/finalization.
- Expiry/reassignment/burn policy must be explicit and versioned; default foundation behavior: no automatic reassignment.
- Unclaimed rewards must never create extra emission.

## Halving and Supply Tests

Required test coverage:

- Block before halving
- Block exactly at halving
- Block after halving
- Repeated halving
- Reward rounding
- Zero reward
- Maximum supply cap
- Invalid reward claim / double claim prevention

## Implementation

- `include/phantomx/rewards/rewards.hpp`
- `src/rewards/rewards.cpp`
- `tests/unit/test_rewards.cpp`
