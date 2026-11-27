# Progress

Date: 2026-11-24

## Summary

| # | Item | Status | Evidence |
|---|---|---|---|
| 1 | Build user-owned | Pass | `cmake -S . -B build-ubuntu -DPHANTOMX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-ubuntu -j6`; no warnings in changed files |
| 2 | CTest | Pass | `ctest --test-dir build-ubuntu --output-on-failure` → `11/11 passed` |
| 3 | Unit test | Pass | `build-ubuntu/tests/phantomx_unit_tests` → `81 passed, 0 failed` |
| 4 | ASan/UBSan | Pass | build `build-ubuntu-asan` with `-fsanitize=address,undefined`; CTest and E2E did not report sanitizer errors |
| 5 | E2E normal single-run | Pass | `E2E_RUN_LABEL=finalnormal2`, exit 0, finality height 1 identical across 4 nodes |
| 6 | E2E view-change single-run | Pass | `E2E_RUN_LABEL=finalvc3`, `E2E_VIEW_CHANGE=1`, quorum NewView + finality height 2 identical across 3 live nodes |
| 6a | E2E view-change nominal proposer down | Pass | `E2E_RUN_LABEL=finalvchk1`, `E2E_VIEW_CHANGE_KILL_PROPOSER=1`, nominal proposer down and signer QC selected a live replacement |
| 7 | Loop 20x normal | Pass | `20/20` rc=0; `/tmp/phantomx-work/final20-normal-summary.txt` |
| 8 | Loop 20x view-change | Pass | `20/20` rc=0 after clean rerun; `/tmp/phantomx-work/vc20-clean-summary.txt` |
| 8a | Loop 10x nominal proposer down | Pass | `10/10` rc=0; `/tmp/phantomx-work/hardvc10-summary.txt` |
| 9 | View-change regtest gate | Done | `poi+verifier.view_change_enabled`, default `false`, only `config/regtest.json` enabled; log `bft_view_change_gate` |
| 10 | Quorum pacemaker | Done | `ViewChange` + `ViewChangeQC` + `NewView`; issuer selected deterministically from QC signers (including when nominal proposer is down); RPC and verification use the same selector; `poi+verifier.view_changed.reason=new_view_quorum` |
| 11 | Lock/valid-round rule | Done | `locked_height` in WAL v2 (v1 still readable); child accepted directly via `parent_hash`; other hashes require prevote QC with a higher view |
| 12 | Root cause loop 20x | Proven | SIGPIPE (`rc=141`) from `::send` to a peer that already closed; port collision (`rc=23`); vote/QC/finality arriving before proposal caused `bad_prev_hash` (`FAIL_CONSENSUS`) |
| 13 | P2P hardening | Done | `MSG_NOSIGNAL`, `SIGPIPE=SIG_IGN`, EINTR retry, ban per `host:listen_port`, rate limit 500 for regtest burst, seed full-mesh, retransmit VC/NewView |
| 14 | RPC scalar params | Done | JSON parser accepts string and integer so `{"view":1}` does not fall back to default; bool/real/null intentionally rejected so `std::stoull` does not throw |
| 15 | Regression test | Done | `p2p_send_to_closed_socket_does_not_kill_process`; VC height mismatch, dedup/equivocation, NewView roundtrip, fallback proposer when nominal proposer is down, lock child/higher-QC |
| 16 | Deferral early messages | Done | vote/QC/finality deferred until proposal is validated, then replayed; state cleared when view changes |

## Root cause and fixes

- **Initial 20x loop failure:** 11 of 20 runs failed in `FAIL_STARTUP` and all node1 processes exited. The wrapper binary recorded `rc=141`. `src/network/node.cpp` used `::send(..., 0)`, so writing to a socket closed by a peer sent SIGPIPE and killed the process. Fix: helper `src/network/socket_io.cpp` uses `MSG_NOSIGNAL` + EINTR retry, and `phantomxd` ignores SIGPIPE. Regression proven with `PHX_TEST(p2p_send_to_closed_socket_does_not_kill_process)`.
- **Secondary failure `rc=23`:** caused by random RPC/P2P port base colliding with a listener/ephemeral port. The harness now tries 50 port pairs before failing.
- **View-change scenario issues:** three additional problems were found from `fail_vcscenario*` artifacts: (1) host-based P2P bans isolated all `127.0.0.1` validators; (2) star seed topology disconnected surviving quorum when a leaf died; (3) `bft_selected_proposer '{"view":1}'` silently used the current view because the RPC parser only accepted strings. Fixes: ban per `host:listen_port`, comma-separated seeds for mesh, and RPC parser accepts integer values (bool/real/null intentionally not coerced).
- **Height 2 proposal rejected after quorum NewView:** rejected with `justify qc epoch/height mismatch` because `bft_propose` carried `valid_qc` from height 1. The lock rule now stores `locked_height` and accepts a direct child (`height = locked_height + 1`, `parent_hash = locked_hash`) without a new QC. WAL bumped to v2 and still reads v1.
- **Remaining `FAIL_CONSENSUS` (e.g. `final20-normal-5.log`):** vote/QC/finality arrived over a different TCP connection before the proposal. The node finalized and advanced tip, then rejected the late proposal with `bad_prev_hash`, so it never sent a local prevote. Fix: daemon defers vote/QC/finality until the proposal is validated (`bft_vote_deferred`, `bft_qc_deferred`, `bft_finality_deferred`, `bft_deferred_replay`) and clears queues when view changes.
- **NewView issuer selection:** artifact inspection showed `bft_selected_proposer '{"view":1}'` was not always the NewView issuer: issuer is selected from `ViewChangeQC` signers. The harness now queries `bft_selected_proposer` after view changes, and the daemon uses the same selector for `bft_propose` and proposal verification. Scenario `E2E_VIEW_CHANGE_KILL_PROPOSER=1` proves fallback when the nominal proposer is down.

## 20x loop notes

- Normal and view-change loops were run serially with `PHANTOMX_BUILD_DIR=$PWD/build-ubuntu` and `E2E_CATCH_DIR=/tmp/phantomx-work/e2e-catch`. Summary: `20/20` normal, `20/20` view-change, and `10/10` view-change with nominal proposer down.
- One early view-change iteration (`final20-viewchange-6`, `rc=2`) failed because the harness was being edited while the iteration ran, so bash read a shifted script (`syntax error near 'fi'`), not because of consensus. The full loop was rerun from scratch with the final script: `vc20-clean-1..20` all `rc=0`.
- Old failures already fixed: `rc=141` (SIGPIPE), `rc=23` (port collision), `FAIL_CONSENSUS` caused by vote/QC/finality arriving before proposal.

## Operational evidence

- Normal E2E command: `env PHANTOMX_BUILD_DIR=$PWD/build-ubuntu E2E_RUN_LABEL=finalnormal2 E2E_CATCH_DIR=/tmp/phantomx-work/e2e-catch scripts/e2e-poi+verifier-4node.sh`
- View-change E2E command: `env PHANTOMX_BUILD_DIR=$PWD/build-ubuntu E2E_RUN_LABEL=finalvc3 E2E_CATCH_DIR=/tmp/phantomx-work/e2e-catch E2E_VIEW_CHANGE=1 scripts/e2e-poi+verifier-4node.sh`
- Nominal proposer down fallback command: `env PHANTOMX_BUILD_DIR=$PWD/build-ubuntu E2E_RUN_LABEL=finalvchk1 E2E_CATCH_DIR=/tmp/phantomx-work/e2e-catch E2E_VIEW_CHANGE=1 E2E_VIEW_CHANGE_KILL_PROPOSER=1 scripts/e2e-poi+verifier-4node.sh`
- Success artifacts: `/tmp/phantomx-work/e2e-catch/pass_finalnormal2_*`, `pass_finalvc3_*`, `pass_finalvchk1_*`, and `pass_vc20-clean-*`/`pass_hardvc10-*`.
- Key logs: `bft_view_change_gate`, `bft_view_timer_fired`, `bft_view_change_broadcast`, `bft_view_change_qc_check`, `bft_new_view_decision`, `bft_new_view_broadcast`, `bft_new_view_retransmit`, `bft_view_changed(reason=new_view_quorum)`, `bft_*_deferred`, `bft_deferred_replay`, `bft_lock_extended`, `commit_finality_applied`.

## Security notes

- These changes are regtest-first and do not prove mainnet/testnet security. Gate default is `false`; no production claim.
- Tokenomics remain integer-only (`phx_atom`); no storage subsystem, VM/contract, or bridge was introduced.
- 20x loop is an empirical regtest gate, not a formal Byzantine fault tolerance proof.
- NewView selector from QC signers is a regtest liveness mechanism, not a general Byzantine recovery proof; `kMaxViewJump=16` cap prevents wild view jumps.
- WAL v2 is not compatible for downgrade to older binaries; v1 can still be read and `locked_height` is reconstructed from a matching QC.
- P2P rate limit 500/sec is a floor for regtest burst; no production tuning yet.

## Ongoing notes still applicable

- PSC20 audit complete: chain UTXO, no VM/contract engine. Selected native protocol-level ledger.
- PoI pool audit: `docs/design/pool-audit.md`; PoI evidence uses challenge/evidence, verifier signature, score, and aggregator certificate.
- PoI pool design: `docs/design/pool.md`; Architecture A selected (external pool, off-chain payout, no consensus change).
- PoI proof commitment design: `docs/design/poi-proof-commitment.md`; envelope format and regtest activation written, no consensus code change yet.

## 2026-11-25 — Qt wallet UI revamp

- **Time (UTC):** 2026-11-25
- **Actions:**
  - Revamped `apps/phantomx-qt` layout: sidebar `Overview/Send/Receive/Transactions/PoI/Settings`; removed create/load/import from Overview.
  - Added auto-create default local wallet when no keystore exists; shows backup warning banner. Seed/private key remains local only.
  - Added sync status on Overview: `blocks/headers`, progress bar, ETA minutes estimated from sampled block rate; honest `Not Available` when target/rate unknown.
  - Added Connected Peers (Node) display from node RPC.
  - Renamed Mining page to **PoI**; added **Start PoI / Stop PoI** buttons, bandwidth limit input MB/GB/TB, backend status notes.
  - Updated node RPC handlers: `getblockchaininfo` now includes `headers/verificationprogress/initialblockdownload/connections`; `getconnectioncount` uses P2P peer count; added `startpoi/stoppoi` and improved `getpoiinfo`.
  - Added docs: updated `docs/DESKTOP_WALLET.md`, added `docs/QT_WALLET_UI.md`.
  - Added skill: `skills/phantomx-qt-wallet/SKILL.md`.
- **Verification:**
  - `cmake -B build-ubuntu -DPHANTOMX_BUILD_QT=ON` **PASS**.
  - `cmake --build build-ubuntu --target phantomx-qt phantomxd -j4` **PASS**.
  - `QT_QPA_PLATFORM=offscreen timeout 3 ./build-ubuntu/bin/phantomx-qt` **PASS** (exit 124 timeout, no crash; GNOME DBus theme warning non-fatal).
  - `ctest --test-dir build-ubuntu --output-on-failure` **11/11 PASS**.
- **Limitations:**
  - PoI bandwidth enforcement is not enforced by daemon/PoI backend unless backend support exists; UI stores config and reports honestly.
  - `headers` currently defaults to `blocks` in RPC when no header target is exposed; ETA becomes available only when sync sampling/target data allows.
  - `docs/CHANGELOG_ACTIONS.md` is root-owned and was not updated.

## 2026-11-25 — Qt wallet: sync statusbar, send presets, PoI domain/score, receive address gen, treasury

- **Time (UTC):** 2026-11-25
- **Actions:**
  - Sync status moved to bottom-left status bar; shows `Synced` when synced.
  - Topbar simplified to Network + Node Online/Offline.
  - Send: preset Fast(2 conf)/Normal(6 conf, default)/Slow(12 conf) with different fees.
  - PoI: added domain field, score/epoch display, status info; domain persisted in qt-wallet.ini.
  - Receive: Generate New Address / Generate 5 Addresses (1 keystore, many addresses) with popup copy; Treasury address highlighted.
  - Treasury: wallet metadata + auto-sweep opt-in to treasury in same keystore; getbalance already aggregates all addresses.
  - RPC: `getnewaddress` now persists to default regtest datadir keystore; added best-effort `sendtoaddress` handler using local datadir wallet.
- **Verification:** `cmake --build build-ubuntu --target phantomx-qt phantomxd -j4` **PASS**; `ctest --test-dir build-ubuntu --output-on-failure` **11/11 PASS**.
- **Limitations:** `sendtoaddress`/`getnewaddress` are best-effort and currently assume default regtest datadir/RPC; production hardening needed. Auto-sweep is wallet-side best-effort; fee/dust/confirmation policy simple. PoI backend enforcement/control depends on daemon/PoI service support.

## 2026-11-25 — BFT/consensus hardening pass (equivocation slashing + WAL v3)

- **Time (UTC):** 2026-11-25
- **Actions:**
  - Restored baseline `src/consensus/bft.cpp`, `include/phantomx/consensus/bft.hpp`, and `tests/unit/test_consensus.cpp` from HEAD because the dirty worktree had gutted BFT into stubs/placeholders.
  - Re-added `src/consensus/bft.cpp` to `CMakeLists.txt` and restored BFT wire encode/decode declarations/implementations in `include/phantomx/network/protocol.hpp` and `src/network/protocol.cpp` (kept existing PoI attestation message types; added BFT message types after them).
  - Hardened BFT: equivocation in votes/view-changes now records evidence and auto-slashes the validator pubkey; `BftEngine::is_slashed()` rejects slashed validators from `submit_vote`/`submit_view_change`.
  - WAL v3: persists slashed validators and equivocation evidence; load accepts v1/v2/v3 and fixes locked_height read for ver>=2. Save uses atomic temp+fsync+rename as before.
  - Added regression test `consensus_slashed_validator_rejected_and_persisted`.
  - Created skill `skills/phantomx-bft-hardening/SKILL.md`.
- **Verification:**
  - `cmake --build build-ubuntu --target phantomx_unit_tests -j6` **PASS**.
  - `./build-ubuntu/tests/phantomx_unit_tests` **85 passed, 0 failed**.
  - `ctest --test-dir build-ubuntu --output-on-failure` **11/11 PASS**.
- **Remaining gaps (honest):** full pacemaker, unbiased production randomness (VRF/commit-reveal), complete governance/slashing beyond equivocation (penalties/appeals/evidence propagation), formal model checking, adversarial multi-node/partition/Byzantine test evidence. **Mainnet NO-GO**.

## 2026-11-25 — BFT gap closure pass (pacemaker, commit-reveal, governance, adversarial tests)

- **Time (UTC):** 2026-11-25
- **Actions:**
  - Pacemaker: added `PacemakerConfig/State`, `armed` progress, `note_progress`, `should_timeout`, exponential backoff with cap, and timeout-driven view advance that preserves backoff across timeout-induced view changes.
  - Commit-reveal randomness: added `RandomnessCommit/Reveal`, `submit_randomness_commit/reveal`, and deterministic `randomness_beacon(epoch)` (sha256d over sorted reveals). Foundation for proposer/selection mixing; not a production-unbiased VRF.
  - Governance/slashing: added `SlashingRecord` (reason, epoch/view/height, penalty_atoms, evidence, appeal/resolve) and APIs `record_slashing`, `appeal_slashing`, `resolve_appeal`.
  - Adversarial/safety/liveness simulation tests: pacemaker backoff/progress, commit-reveal determinism and bad-reveal rejection, slashing appeal flow, conflicting-finality safety, partition no-quorum, view-change recovery.
  - Created skill `skills/phantomx-bft-hardening/SKILL.md` (earlier in session) and used `skill-creator` guidance.
- **Verification:**
  - `cmake --build build-ubuntu --target phantomx_unit_tests -j6` **PASS**.
  - `./build-ubuntu/tests/phantomx_unit_tests` **91 passed, 0 failed**.
  - `ctest --test-dir build-ubuntu --output-on-failure` **11/11 PASS**.
- **Honest remaining production gaps:** formal TLA+/model-check proof artifact, independent security audit, long-running multi-node adversarial/partition/Byzantine cluster evidence, reveal-or-slash/VRF production randomness, and on-chain slashing/governance economic enforcement. **Mainnet NO-GO**.

## 2026-11-25 — BFT gap closure pass 2 (formal artifact, randomness deadline, slashing economics, adversarial script)

- **Time (UTC):** 2026-11-25
- **Actions:**
  - Formal/model artifact: added `docs/formal/bft_safety.tla` and executable bounded checker `tools/model_check_bft.py`.
  - Randomness: added `set_randomness_reveal_deadline_blocks` and `enforce_randomness_deadline` (reveal-or-slash withholding path); finalize enforces deadline at finalized height.
  - Slashing economics: added settlement accounting (`settled`, `settlement_ref`, `settle_slashing`, `total_slashed_penalty_atoms`, `unsettled_slashed_penalty_atoms`).
  - Adversarial cluster: added `scripts/adversarial-bft-cluster.sh` (wraps E2E BFT regtest with view-change and proposer-kill iterations).
  - Added unit tests: `consensus_randomness_reveal_deadline_slashes_withholder`, `consensus_slashing_settlement_economics`.
- **Verification:**
  - `cmake --build build-ubuntu --target phantomx_unit_tests -j6` **PASS**.
  - `./build-ubuntu/tests/phantomx_unit_tests` **93 passed, 0 failed**.
  - `python3 tools/model_check_bft.py` **OK**.
  - `cmake --build build-ubuntu --target phantomxd -j6` **PASS**.
  - `ADVERSARIAL_ITERATIONS=1 ... bash scripts/adversarial-bft-cluster.sh` **FAIL in current dirty tree** (E2E script RPC mismatch: `bft_local_pubkey` method not found / startup failure). Not claimed as passing adversarial evidence.
- **Honest remaining production gaps:** mechanically checked TLC/Apalache proof, independent audit, successful long-running multi-node adversarial/partition/Byzantine cluster evidence, production VRF/unbiased randomness, full on-chain slashing settlement economics. **Mainnet NO-GO**.

## 2026-11-25 — BFT RPC/P2P wiring restoration (4-node regtest E2E green)

- **Time (UTC):** 2026-11-25
- **Actions:**
  - Restored the BFT RPC handlers in `src/rpc/node_handlers.cpp` (they had been reduced to placeholders). `bft_propose` builds and validates a coinbase-only block, stores it locally, signs a `BlockProposal` with `justify_qc = bft.state().valid_qc`, broadcasts the block and proposal over P2P, and invokes `on_local_proposal`.
  - Extended `register_node_rpc_handlers` with a P2P pointer, BFT engine pointer, local BFT key, `on_local_proposal` and `proposer_selector`; the daemon wires these after P2P startup.
  - Fixed daemon config lookups to use the flattened dot keys `bft.enabled` / `bft.view_change_enabled` from `config/regtest.json`; enabled BFT for regtest.
  - Restored BFT gossip in `P2PNode`: decode/dispatch for `kBftBlock`, `kBftProposal`, `kBftVote`, `kBftQc`, `kBftFinality`, `kBftViewChange`, `kBftNewView` plus matching `broadcast_bft_*` methods with daemon-installed callbacks. BFT blocks are persisted but applied only on finality; the ordinary `kBlock` path is unchanged.
  - Replaced the `// bft removed` broadcast stubs in `src/daemon/main.cpp` with real vote/QC/finality/view-change/new-view gossip and retransmission. Ingress callbacks validate before mutating local state (future-view vote buffering, deferred replay, quorum-gated view change).
  - `getfinalityinfo` now reports the further-ahead of PoI in-memory finality and BFT finality persisted to chain metadata (`finalized_height` / `finalized_hash`), so RPC clients observe BFT finality immediately.
  - `scripts/run-bft-regtest.sh` passes `--config config/regtest.json`; `--connect` accepts comma-separated seeds so the E2E harness builds a full mesh.
- **Verification (fresh `build-ubuntu` binary):**
  - `cmake --build build-ubuntu --target phantomxd -j6` **PASS**.
  - `scripts/run-bft-regtest.sh`: 4 nodes RPC-ready, full-mesh peers, `bft_propose` from the selected proposer; all nodes report identical finalized height/hash **PASS**.
  - `scripts/e2e-bft-4node.sh`: `proposal_validated`, `prevote`, `precommit_broadcast`, `commit_finality_applied` on all nodes with identical finality **PASS**.
  - `E2E_VIEW_CHANGE=1 scripts/e2e-bft-4node.sh`: one validator killed, quorum NewView advanced the view, height-2 finality matched on live nodes, no unquorumed local timeout **PASS**.
  - `./build-ubuntu/tests/phantomx_unit_tests`: **93 passed, 0 failed**.
  - `ctest --test-dir build-ubuntu --output-on-failure`: **11/11 PASS**.
- **Repeatable process notes (English):** always rebuild before running the scripts. The default `build-verified/bin/phantomxd` is stale/root-owned, so pass `PHANTOMX_BUILD_DIR=$PWD/build-ubuntu` to the E2E script. Use a writable `E2E_CATCH_DIR` for pass/fail artifacts. `bft_status` requires `bft.enabled=true` in the flattened JSON config (not `bft->enabled`). Start the manual cluster with `scripts/run-bft-regtest.sh`, find the proposer via `bft_selected_proposer`, then call `bft_propose` on that node only. Stop with `pkill -x phantomxd`.
- **Honest remaining production gaps:** mechanically checked formal proof, independent security audit, successful long-running multi-node adversarial/partition/Byzantine evidence, production VRF/unbiased randomness, and full on-chain slashing settlement economics. **Testnet/mainnet remain NO-GO.**
