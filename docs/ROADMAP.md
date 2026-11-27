# PhantomX Roadmap

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Status
Aligned to `prompt.txt` (Prompt 01-14). This roadmap tracks implementation phases. Items are not complete unless verified by build/tests and documented evidence.

## Phase 1 — Architecture & Audit (Prompt 01)
- [x] Baseline build/test run recorded (12/12 PASS in build/dev at audit time).
- [x] Storage code remnants removed/disabled (VerifierType storage/both, PoolRole storage, KeyRole storage, storage role mapping).
- [x] Major storage-subsystem references removed/patched in code/docs; remaining historical diagram/text cleanup tracked.
- [x] Tokenomics decision finalized: master plan is 90% PoI contributors / 10% verifiers, no pool subsidy category. Code/docs/config/tests reconciled.
- [x] Created/refreshed: VERIFIER_PROTOCOL, AGGREGATOR_PROTOCOL, THREAT_MODEL, ROADMAP, PROMPT01_AUDIT_REPORT; updated ARCHITECTURE/CONSENSUS/PROOF_OF_INTERNET/TOKENOMICS/KNOWN_LIMITATIONS partially.

## Phase 2 — Core Foundation (Prompt 02)
- [x] Canonical serialization/types/amount/fixed-point/hash/crypto interfaces (existing + added `common/amount.hpp`, `common/chain_params.hpp`).
- [x] Block/header/tx/outpoint/address/chain params/genesis/error/logging present.
- [x] Deterministic hash/serialization tests passing (ctest 12/12).

## Phase 3 — UTXO & Transactions (Prompt 03)
- [x] UTXO/mempool/tx validation/tx_builder/p2pk ed25519 signing present; regtest_e2e + unit tests pass.
- [ ] Dust rule and full tx-size/fee policy documentation still need explicit final spec.

## Phase 4 — Chainstate DB (Prompt 04)
- [x] SQLite chain DB with TransactionGuard atomic apply/disconnect, undo, recovery, snapshot/verify present.
- [ ] Full PoI+Verifier finality-aware reorg prevention and deeper crash-consistency/property tests still needed.

## Phase 5 — P2P & Sync (Prompt 05)
- [x] Network/P2P modules and genesis/mainnet/testnet/regtest params exist; network tests pass.
- [ ] Full multi-node regtest sync, hardened peer scoring/ban/rate-limit/malformed-message protection need deeper adversarial tests.

## Phase 6 — PoI Engine (Prompt 06)
- [x] PoI modules exist (identity/measurement/challenge/evidence/scoring/anti_sybil/simulator); unit/anti-sybil tests pass.
- [ ] Live network probing, production verifier quorum policy, and stronger anti-Sybil/collusion defenses remain.

## Phase 7 — Verifier Network (Prompt 07)
- [x] Verifier registry/assignment/service foundation exists; verifier tests pass.
- [ ] Authenticated delivery, signed registration/heartbeat hardening, dispute/slashing, full rotation/failure recovery remain.

## Phase 8 — Aggregator Engine (Prompt 08)
- [x] Aggregator module: attestation collection, duplicate/conflict/quorum checks, certificate build/sign/verify, certificate store/lookup, dispute open/resolve, assignment register/get/rotate.
- [x] Persistence: file state + SQLite DB index (`aggregator.db`, cert_hash PK, assignment/subject indexes, full cert+attestations). Daemon loads/saves.
- [x] RPC wiring: aggregator_register/list/set_status, submitattestation, buildcertificate, getaggregation, getcertificate, verifycertificate (stored cert full verify + signature-only fallback), dispute/assignment RPCs.
- [x] Unit tests including SQLite roundtrip (`tests/unit/test_aggregator.cpp`).
- [ ] Deep integration with PoI/verifier/PoI+Verifier flows into finality/reward path; production arbitration/slashing governance (BLOCKED on governance/design).

## Phase 9 — PoI+Verifier Consensus & Finality (Prompt 09)
- [x] PoI+Verifier foundation: vote/QC verify, equivocation detection, finalize, timeout/view change, verifier rotation.
- [x] Hardening: lock/unlock by view, precommit-vs-lock rejection, finalize lock/locked_view, save/load persistence including equivocations+votes, durable_path auto-persist, pacemaker hooks (backoff timeout/tick).
- [x] Fuzz/chaos unit tests: invalid vote fuzz, timeout/view chaos (executed in ctest).
- [ ] Remaining: full pacemaker network round sync, crash-safe WAL vote/QC store, unbiased production randomness, slashing/governance enforcement, BftEngine<->P2PNode daemon wiring, full Byzantine/partition/long-running cluster suite.

## Phase 10 — Rewards & Epoch Settlement (Prompt 10)
- [x] Reward split aligned to 90/10 (no pool) in code/config/tests; deterministic overflow-safe integer math, halving, claim Merkle/ClaimRegistry present.
- [ ] Full on-chain epoch settlement, persistent claim nullifiers, duplicate payout prevention across restarts, complete accounting audit remain.

## Phase 11 — RPC & CLI (Prompt 11)
- [x] `phantomxd`/`phantomx-cli` exist; RPC server, auth token, basic rate limit, node handlers present; smoke tests pass.
- [ ] Full required RPC method coverage, RBAC, stronger authz, complete RPC docs, safe shutdown hardening pending.

## Phase 12 — Wallet/Pool (Prompt 12)
- [x] Wallet/Qt/pool foundations exist in repo.
- [ ] Full secure key handling, mobile light wallet production flows, pool accounting transparency, and monitoring/finality visibility completeness pending.

## Phase 13 — Regtest Multi-node (Prompt 13)
- [x] Regtest config and E2E integration test exist; basic regtest flow passes.
- [x] Added foundation scripts: `scripts/start-regtest.sh`, `stop-regtest.sh`, `reset-regtest.sh`, `check-regtest.sh`, `create-regtest-wallet.sh`, `send-regtest-transaction.sh` (syntax checked).
- [ ] Full 4-verifier PoI+Verifier multi-node orchestration, contributor/verifier/aggregator/pool services, and documented E2E acceptance evidence pending.

## Phase 14 — Security & Testnet Release (Prompt 14)
- [x] Initial security/threat docs created (`THREAT_MODEL.md`, `SECURITY.md`); test baseline passing.
- [ ] Independent audit, fuzz/property/chaos/partition/Byzantine/collusion tests, operator guides, release packaging, and long-running testnet evidence pending. No mainnet claim.
