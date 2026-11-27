# PhantomX Autonomous Hardening Ledger

> **Status:** Active  
> **As of:** 2026-11-26  
> **Branch:** `testnet/local-node-runnable` (dirty worktree, many pre-existing uncommitted changes)  
> **Baseline (verified this session):** `build-ubuntu` present; `phantomx_unit_tests` **99/99 PASS**; `ctest` **11/11 PASS**.  
> **Production decision:** **NO-GO**.

## Baseline Evidence

| Check | Command | Result |
|---|---|---|
| Unit tests | `./build-ubuntu/tests/phantomx_unit_tests` | 102 passed, 0 failed |
| CTest | `ctest --test-dir build-ubuntu --output-on-failure` | 11/11 passed |
| BFT regtest | `PHANTOMX_BUILD_DIR=$PWD/build-ubuntu E2E_CATCH_DIR=/tmp/e2e-catch-user E2E_RUN_LABEL=px-final-check bash scripts/e2e-bft-4node.sh` | PASS |
| BFT view-change | same + `E2E_VIEW_CHANGE=1` | PASS (`pass_px-final-check_1795631894913974574`) |
| Git state | `git status --short` | Dirty; many pre-existing modified/untracked files. Do not revert/discard. |

## Findings

| ID | Category | Finding | Evidence | Severity | Status | Remediation | Tests | Verification | Residual risk |
|---|---|---|---|---|---|---|---|---|---|
| PX-SEC-0001 | Identity/Anti-Sybil | PoI identity was self-reported RPC claim; remote claims accepted without cryptographic proof. IP binding is transport heuristic only. | `src/rpc/node_handlers.cpp` `startpoi/poi_heartbeat` pre-fix; session smoke | Critical | Fixed-Foundation | Added Ed25519 PoP identity claim verification (`identity_pubkey/sig/ts`, domain `PHX/poi/identity/v1`), owner<->pubkey binding, persisted identity bindings in `poi_state_json`; remote RPC requires signature unless `PHANTOMX_POI_ALLOW_UNSIGNED_IDENTITY=1`. | Added 3 unit tests: missing-sig/valid/owner-conflict; bad-sig/skew/owner-pubkey/pubkey-owner conflicts; metadata persistence. | 102/102 unit, 11/11 ctest, regtest+VC PASS. | **Not production Sybil-resistant**: no stake/bond/cost/attestation, no chain-visible registry, IP still weak signal, key can be mass-generated. |
| PX-SEC-0002 | PoI/Uptime Proof | Uptime is deterministic self-reported heartbeat accounting, not cryptographic measurement proof. | `poi_uptime_ratio`, heartbeat counters in `src/rpc/node_handlers.cpp`; `docs/KNOWN_LIMITATIONS.md` | Critical | Open | Target: signed verifier challenge/response/attestation quorum, freshness/replay protection, evidence persisted/finalized. | None yet | N/A | Reward/consensus can be influenced by fake liveness until fixed. |
| PX-ECON-0001 | Settlement | `mine_one` scales contributor share by max active PoI score; not full on-chain verified/finalized settlement. Remainder unminted in output path. | `mine_one` in `src/rpc/node_handlers.cpp` | Critical | Open | Target: PoI record commitment/finality, deterministic settlement tx/state transition, reward ledger, replay/audit tests. | None yet | N/A | No auditable on-chain PoI settlement economics. |
| PX-ECON-0002 | Multi-machine accounting | Multi-machine accumulation is RPC-side owner aggregate sum capped 100; not on-chain audited payout weight. | `poi_owner_aggregate_score`, RPC responses | High | Open | Target: per-machine finalized records, owner cap/normalization by protocol rule, anti-double-count keys, on-chain payout ledger. | None yet | N/A | Double-count/gaming risk remains; not auditable for payouts. |
| PX-CONS-0001 | BFT/Test Harness | Manual testnet BFT ad-hoc repro entered view-change storm with no proposal/finality (`stale_or_distant_view` after view changes). Official regrest still PASS. | `/tmp/phx-testnet-bft-proper2` logs; `scripts/e2e-bft-4node.sh` PASS | High | Investigating | Built `/tmp/phx_bft_repro_v3.sh` mirroring regtest assumptions (regtest config, rpc.token, getblockcount readiness, ready_peer_count>=1 3-check barrier, proposer agreement/routing). Script PASS on regtest: 3 heights finalized. Earlier failures traced to harness/setup mismatches (missing token, wrong config, parsing). | Official regrest PASS; repro v3 PASS | `e2e-bft-4node.sh` PASS, `E2E_VIEW_CHANGE=1` PASS | Ad-hoc testnet-config repro still needs equivalent script run with `--testnet` and evidence capture; no consensus code changed. |
| PX-SEC-0003 | Identity Binding Persistence | Identity bindings were in-memory only before this session. | `node_handlers.cpp` pre-fix | High | Fixed-Foundation | Persist `identity_bindings` in `poi_state_json`, load on startup. | Build+unit+ctest PASS | Verified compile/tests | Bindings still local node state, not chain-visible/audited registry. |
| PX-DOC-0001 | Protocol Spec | No single source of truth protocol/security spec existed. | docs tree | Medium | Fixed-Foundation | Added `docs/PHANTOMX_PROTOCOL_SECURITY_SPEC.md` (draft, NO-GO, covers required 15 sections). | N/A | File created | Spec is draft; requires review, test vectors, and implementation alignment. |

## External/Blocked Gates

| ID | Gate | Why blocked | Evidence required |
|---|---|---|---|
| PX-GATE-0001 | Formal verification | No mechanically checked TLC/Apalache/CI proof artifact for full protocol | Checked model/proof artifacts, assumptions, bounds, CI job |
| PX-GATE-0002 | Independent security audit | External auditor required | Audit report, finding disposition |
| PX-GATE-0003 | Long-running adversarial evidence | Needs sustained multi-node partition/Byzantine/soak runs and ops artifacts | Archived logs/metrics/runbooks |
| PX-GATE-0004 | Production VRF/unbiased randomness | Current commit-reveal is foundation; last-revealer bias not eliminated | VRF/beacon design, proofs, tests |
| PX-GATE-0005 | Full on-chain slashing settlement economics | Engine hooks exist; full on-chain economic enforcement missing | Deterministic state transition, tests, audit |

## Next Actions (priority)

1. PX-CONS-0001: run repro v3 against `--testnet` config/ports; capture artifacts and compare to ad-hoc failure; confirm no consensus regression.
2. PX-SEC-0001: add HTTP integration tests for identity claim; test persistence reload across actual restart path; consider removing local unsigned fallback or making it explicit config.
3. PX-SEC-0002: prototype challenge/response/attestation structs, canonical signing, replay cache, quorum aggregation behind feature flag; no settlement authority.
4. PX-ECON-0001/0002: add deterministic fixed-point settlement accounting test vectors (no inflation, no double payout, owner cap, anti-double-count keys).
5. Keep verification gate: build, unit, ctest, e2e-bft-4node, E2E_VIEW_CHANGE=1 after every code change.
