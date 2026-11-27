# PoI+Verifier View-Change / Pacemaker Design (Draft)

> **DEPRECATED (PoI+Verifier removed):** PhantomX has moved to **PoI + Verifier attestation** consensus/finality. This document is retained for history only and must not be used to reintroduce PoI+Verifier/verifier consensus.


## Status
Draft, consensus-visible. Required before protocol/consensus behavior changes. Regtest-first activation; no mainnet activation implied.

This addendum supersedes `docs/design/view-change.md` where that draft is stale.
The original file is root-owned in this environment and could not be rewritten
by the development user.

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
- Byzantine fault injection framework beyond deterministic tests.

## State
Per verifier, persisted in WAL v2:
- `epoch, height, view`
- `locked_qc` / `locked_hash`, `locked_height`, `locked_view`
- `valid_qc`, `valid_view`
- `last_finalized`
- `pending_votes`, `pending_proposals`, `pending_qcs`
- `view_timeout_deadline`

WAL v1 files remain readable. When `locked_height` is absent, it is recovered from
the matching precommit QC in `last_finalized`, never from a mismatched hash.

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
Normal view:
`proposer = select_proposer(validatorset, epoch, view)` where input includes `chain_id`, `epoch`, `height`, `view`, and ordered verifier pubkeys.

NewView after a quorum timeout:
`proposer = select_new_view_proposer(validatorset, view_change_qc)`, which runs the
same deterministic selection over the verifiers that signed the ViewChangeQC.
This preserves liveness when the nominal next-view proposer crashed: the signing
quorum elects a live issuer, and every node reconstructs it from the QC. The
elected proposer is also used for the next block proposal and for verifying that
proposal; it is retransmitted while the node remains in that view.

## Timeouts
- Base timeout: `T0 = 4000ms` regtest.
- Backoff: `T(view) = min(Tmax, T0 * 1.5^view_delta)`, `Tmax=30000ms`.
- Timer scheduled on entering view; cancelled/reset on QC/finality/progress.
- On timeout: verifier broadcasts `VIEW_CHANGE` for `view+1`, enters view-change waiting state, does not vote in old view.
- A local timeout never advances the view by itself; `enter_view` requires a signed `NewView` backed by a `ViewChangeQC`.

## Lock / Valid-Round Rule
Safety:
- Verifier locks on highest precommit QC it observes (`locked_qc`), including its height.
- A direct child of the locked block is safe without a new QC:
  `proposal.height == locked_height + 1 && proposal.parent_hash == locked_hash`.
- For any other different hash, the verifier may prevote only with a valid
  prevote `justify_qc` for that same height/block where
  `justify_qc.view > locked_view`.
- Proposer includes `justify_qc` = highest QC it knows; honest verifiers
  reject proposals that rewrite the locked prefix without a newer QC.

Votes, QCs, and finality certificates that arrive before the matching proposal
are deferred. They are replayed only after the block is locally validated, so a
node cannot finalize (and then reject as `bad_prev_hash`) a block it never
prevoted.

## Transitions
1. **EnterView(v):** set timer `T(v)`, accept messages for `v`.
2. **On Proposal(v):** verify proposer (normal or NewView-elected), justify QC, lock rule; if valid, prevote.
3. **On Prevote QC(v):** mark progress, precommit.
4. **On Precommit QC(v):** finalize/apply, advance height.
5. **On Timeout(v):** emit `VIEW_CHANGE(v+1)`, stop voting in `v`.
6. **On NewView QC(v+1):** advance to `v+1`, reset timer, replay deferred messages.

## Network Round Sync
- Resend own latest vote, own view-change, and own NewView periodically while round not final.
- On peer ready, resend current-round state.
- Buffer early/future-view messages with bounds (per peer, per height/view window).
- Deduplicate by `(verifier, type, epoch, height, view, payload_hash)`; QCs are
  deduplicated by sorted voter keys, not raw serialized bytes.
- Ban by authenticated `listen_port`, never by host alone.

## Activation
- Config key: `poi+verifier.view_change_enabled` (default `false`; only `config/regtest.json`
  sets `true`). No mainnet/testnet activation is implied.
- Wire messages gated by activation and protocol version/capability.
- Old nodes reject unknown messages as before; activation requires coordinated regtest config.

## Tests / Evidence
Unit/engine:
- timeout advances view and emits view-change.
- lock rule rejects unsafe prevote and accepts a direct child.
- valid-round allows unlock with higher justify QC.
- duplicate/equivocating view-change detected.
- NewView roundtrip, and fallback proposer when the nominal proposer is down.

Network/integration:
- 1 node down: still finalizes.
- Nominal next-view proposer down: quorum elects a live fallback proposer.
- delayed/lost view-change or NewView: retransmission recovers.
- 20x normal and 20x view-change loops clean in regtest.

Required logs: `bft_view_timer_scheduled/fired/cancelled`, `bft_view_changed`,
`bft_vote_deferred`, `bft_qc_deferred`, `bft_finality_deferred`, `bft_deferred_replay`,
`bft_vote_retransmit`, `bft_new_view_retransmit`, `bft_qc_evaluated`,
`bft_quorum_qc_formed`, `commit_finality_applied`.

## Risks
- Consensus-visible: wrong lock rule can break safety.
- Liveness regressions if timeout/backoff too aggressive.
- Message amplification/DoS from view-change floods; requires bounds and signature checks before buffering.
- Fallback proposer is selected from a signed quorum but is not a formal
  Byzantine-tolerant leader election; no mainnet safety claim is made.
