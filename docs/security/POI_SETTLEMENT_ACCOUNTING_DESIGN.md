# PoI Settlement & Multi-Machine Accounting Design (PX-ECON-0001/0002)

> Status: DRAFT / DESIGN ONLY - NOT IMPLEMENTED AS CONSENSUS
> As of: 2026-11-26

## Goals
- Full on-chain, deterministic, replay-verifiable PoI settlement.
- Audited multi-machine owner accounting with anti-double-count.
- No inflation, no duplicate payout.

## Inputs (must be finalized/verified)
- Finalized PoI records: `PoiRecordV1` (see protocol spec §3).
- Epoch budget: `rewards::block_subsidy_atoms(height)` split 70/20/10.
- Identity registry state: active owner/machine bindings.

## Accounting Keys
- `record_key = (epoch, slot, machine_id)`
- `payout_key = (epoch, owner_address, machine_id)`
- `challenge_key = (epoch, slot, machine_id, challenge_id)`
- Reject duplicates; conflicting finalized records are invalid/slashable evidence.

## Deterministic Weighting
- `w_m = clamp(score_composite_milli/100000,0,1) * eligibility * anti_sybil_factor`.
- `owner_raw = sum(w_m for active machines in epoch)`.
- `owner_capped = min(owner_raw, W_owner_cap)` (parameter, default TBD by governance/spec).
- Normalize owner shares over eligible owners; fixed-point integer math only.

## Settlement Record
```text
PoiSettlementV1 {
  version, epoch, finalized_height, poi_records_root,
  reward_budget_atoms, poi_pool_atoms, verifier_pool_atoms, pool_ops_atoms,
  payouts_root, slashes_root, unissued_atoms, rule_hash
}
```

## Invariants
- `sum(payouts)+sum(slashed_or_redirected)+unissued == distributable_budget`.
- No negative amount.
- No payout without finalized eligible PoI record.
- Same payout_key cannot settle twice.
- Replaying chain yields identical settlement.

## Multi-Machine
- Owner may register multiple machines; each machine has independent finalized records.
- Payout attribution is per machine but paid to owner address by deterministic rule.
- Owner cap and anti-Sybil factor prevent linear farm scaling.

## Current Status
- Current `mine_one` scales contributor share by max active RPC PoI score; foundation only.
- `aggregate_score` is RPC-local capped sum; not audited on-chain payout weight.
- No settlement tx/state transition, no payout ledger, no anti-double-count enforcement at consensus.
