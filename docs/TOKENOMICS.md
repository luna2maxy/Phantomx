# PhantomX Tokenomics

## Token Parameters

| Parameter | Value |
|---|---|
| Symbol | PHX |
| Decimals | 8 |
| Atom unit | `phx_atom` (`1 PHX = 100,000,000 phx_atom`) |
| Initial block reward | 25 PHX (`2,500,000,000` atoms) |
| Halving interval | 2,100,000 blocks |
| Target block time | 60 seconds |
| Maximum theoretical supply | 105,000,000 PHX (`10,500,000,000,000,000` atoms) |
| Premine | 0 |

## Emission Schedule

- Block subsidy at height `h`: `initial_reward >> halvings`, where `halvings = floor(h / 2,100,000)`.
- When right shift yields 0, subsidy is 0.
- Total minted is capped at the theoretical maximum supply.
- Consensus/economic calculations use integer `phx_atom` arithmetic only; no floating point in consensus decisions.

## Block Subsidy Distribution

Block subsidy is distributed as:

| Recipient | Share |
|---|---:|
| PoI contributors | 70% |
| Verifier operators | 20% |
| Pool operators | 10% |

Storage providers are paid from **user storage fees**, not block subsidy. There is no additional storage emission without a new tokenomics decision.

## Settlement Model

Rewards are settled by epoch:

1. **Epoch snapshot** - capture epoch boundaries, participants, and work units.
2. **Score aggregation** - aggregate PoI contribution scores/units.
3. **Verifier work aggregation** - aggregate verified work units.
4. **Pool work aggregation** - aggregate pool operator work units.
5. **Reward calculation** - deterministic integer split 70/20/10, proportional by work units.
6. **Reward commitment** - build canonical Merkle commitment/root.
7. **Claim validation** - verify Merkle proof, epoch, recipient, amount, category.
8. **Double claim prevention** - claim registry rejects repeated claim key.
9. **Unclaimed reward handling** - unclaimed entries remain claimable by policy window; expired/unclaimed handling must be defined by governance/consensus policy (not auto-minted elsewhere).

## Determinism Rules

- All nodes must compute identical results from identical epoch inputs.
- Participants are canonicalized: zero-id/zero-work removed, sorted by ID, duplicates merged.
- Category split uses integer arithmetic: verifier/pool floor, PoI receives remainder to preserve total.
- Proportional distribution uses integer floor; remainder assigned deterministically to first canonical participant in category.
- Reward entries are canonically sorted before Merkle commitment.

## Security / Accounting Invariants

- No negative or overflowed reward amounts.
- Total distributed + deterministic remainder handling must equal epoch subsidy.
- No double claim for same `(epoch, recipient, category)`.
- Invalid/tampered claims must be rejected.
- Storage fee payments are separate from block subsidy accounting.

## Implementation

- `include/phantomx/rewards/rewards.hpp`
- `src/rewards/rewards.cpp`
- `tests/unit/test_rewards.cpp`
- `docs/REWARD_DISTRIBUTION.md`
