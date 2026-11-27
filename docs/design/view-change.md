# PoI+Verifier View-Change / Pacemaker Design (Draft)

> **DEPRECATED (PoI+Verifier removed):** PhantomX has moved to **PoI + Verifier attestation** consensus/finality. This document is retained for history only and must not be used to reintroduce PoI+Verifier/verifier consensus.


## Status
Draft, consensus-visible. Required before protocol/consensus behavior changes. Regtest-first activation; no mainnet activation implied.

## Motivation
Current daemon has a local timer baseline that calls `BftEngine::timeout()` and clears votes. It does not provide network round sync, view-change messages, lock/valid-round safety across views, or liveness evidence under lost/delayed messages. This design defines the minimal view-change/pacemaker protocol.

## Scope
In scope:
- View timeout and view change for the existing PoI+Verifier finality path.
- NewView/Timeout message carrying verifier vote for next view.
- Lock rule and valid-round rule for safe proposal selection.
- Deterministic proposer per `(chain_id, epoch, height, view)`.
- Regtest activation parameter and tests.

Out of scope (separate design/implementation):
- Full slashing/governance.
- Crash-safe WAL (separate hardening item).
- Byzantine fault injection framework beyond deterministic tests.

## State
Per verifier, in-memory (WAL later):
- `epoch, height, view`
- `locked_qc` / `locked_hash`, `locked_view`
- `valid_qc`, `valid_view`
- `last_finalized`
- `pending_votes`, `pending_proposals`, `pending_qcs`
- `view_timeout_deadline`

## Message Formats (canonical, domain-separated)
All signatures include `chain_id`, network magic/genesis domain, height, view, epoch, message type, and payload hash.

### Timeout / ViewChangeVote
Fields:
- `type = VIEW_CHANGE`
- `chain_id`
- `epoch, height, new_view` (sender moves to `new_view = current_view + 1`)
- `validator_pubkey, voting_power`
- `high_qc` (highest QC seen by verifier, optional)
- `signature`

Purpose: proves verifier timed out and requests move to `new_view`.

### NewView
Fields:
- `type = NEW_VIEW`
- `chain_id`
- `epoch, height, view`
- `new_view_qc` (QC over VIEW_CHANGE votes for same `new_view`)
- `proposer_pubkey`
- `proposal` (block proposal for view; may carry `justify_qc`)
- `signature`

Purpose: proposer for `view` proves quorum wants this view and proposes block.

## Proposer Rule
Deterministic:
`proposer = select_proposer(validatorset, epoch, view)` where input includes `chain_id`, `epoch`, `height`, `view`, and ordered verifier pubkeys. Existing `select_proposer` may be extended to include `chain_id/height` behind activation.

## Timeouts
- Base timeout: `T0 = 4000ms` regtest.
- Backoff: `T(view) = min(Tmax, T0 * 1.5^view_delta)`, `Tmax=30000ms`.
- Timer scheduled on entering view; cancelled/reset on QC/finality/progress.
- On timeout: verifier broadcasts `VIEW_CHANGE` for `view+1`, enters view-change waiting state, does not vote in old view.

## Lock / Valid-Round Rule
Safety:
- Verifier locks on highest precommit QC it observes (`locked_qc`).
- Verifier may prevote for proposal only if `proposal.block_hash == locked_hash` OR `proposal.justify_qc.view > locked_qc.view`.
- Proposer must include `justify_qc` = highest QC it knows; honest verifiers reject proposal without valid justify when locked.

This preserves safety across views.

## Transitions
1. **EnterView(v):** set timer `T(v)`, accept messages for `v`.
2. **On Proposal(v):** verify proposer, justify QC, lock rule; if valid, prevote.
3. **On Prevote QC(v):** mark progress, precommit.
4. **On Precommit QC(v):** finalize/apply, advance height, enter view 0/next round.
5. **On Timeout(v):** emit `VIEW_CHANGE(v+1)`, stop voting in `v`.
6. **On NewView QC(v+1):** advance to `v+1`, reset timer, process included proposal.

## Network Round Sync
- Resend own latest vote and latest view-change vote periodically while round not final.
- On peer ready, resend current-round state.
- Buffer early/future-view messages with bounds (per peer, per height/view window).
- Deduplicate by `(verifier, type, epoch, height, view, payload_hash)`.

## Activation
- Config/chain param: `bft_view_change_enabled` (default false mainnet/testnet until evidence; regtest may enable).
- Wire messages gated by activation and protocol version/capability.
- Old nodes reject unknown messages as before; activation requires coordinated regtest config.

## Tests / Evidence
Unit/engine:
- timeout advances view and emits view-change.
- lock rule rejects unsafe prevote.
- valid-round allows unlock with higher justify QC.
- duplicate/equivocating view-change detected.

Network/integration:
- 1 node down: still finalizes.
- 2 nodes down: stalls, no divergence.
- delayed/lost view-change message: retransmission recovers.
- partition heals and converges.
- restart/catch-up reaches same finalized hash.

Required logs: `bft_view_timer_scheduled/fired/cancelled`, `bft_view_changed`, `bft_vote_buffered`, `bft_vote_retransmit`, `bft_qc_evaluated`, `bft_quorum_qc_formed`, `commit_finality_applied`.

## Risks
- Consensus-visible: wrong lock rule can break safety.
- Liveness regressions if timeout/backoff too aggressive.
- Message amplification/DoS from view-change floods; requires bounds and signature checks before buffering.
