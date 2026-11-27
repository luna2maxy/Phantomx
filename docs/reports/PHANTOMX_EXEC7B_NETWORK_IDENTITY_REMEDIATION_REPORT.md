# PHANTOMX — EXEC7B Network Identity Remediation Report

**Date:** 2026-11-26  
**Evidence dir:** `/tmp/phx_testnet_exec7b_20261126/`  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9`, branch `encryption`, dirty 115+.  
**Owner approval:** Obtained (TB-023 remediation, TB-019 Option A, TB-022 exclusion).

## Source audit findings

- `src/daemon/main.cpp:371-387` constructed `NetworkConfig` with defaults only; `network_magic` and `genesis_hash` never assigned.
- `include/phantomx/network/node.hpp:32-35` defaults: magic `0x52505801` (regtest), genesis zero.
- `src/network/node.cpp:591-592` validates magic and genesis before peer readiness.
- `include/phantomx/common/chain_params.hpp` + `src/blockchain/genesis.cpp` provide canonical per-network magic and genesis block.
- `tests/unit/test_protocol_vectors.cpp` locks testnet genesis hash `98a75772...`.

## Files changed

- `src/daemon/main.cpp`: added includes `genesis.hpp`, `primitives/block.hpp`; wired `p2p_network.network_magic = network_magic(network)` and `p2p_network.genesis_hash = block_hash(genesis_block(network).header)`; fail-closed on zero magic/genesis; diagnostic log `p2p_network_identity`.
- `tests/network/test_p2p.cpp`: added 3 tests (`p2p_network_identity_match_connects`, `p2p_network_identity_mismatch_rejected`, `p2p_network_identity_zero_genesis_rejected`).

## Diff summary

- `src/daemon/main.cpp`: +~18 lines (wiring + fail-closed + log).
- `tests/network/test_p2p.cpp`: +~95 lines (3 tests).
- No changes to consensus, genesis content, block format, monetary/reward, BFT finality/QC/quorum/proposer/fork-choice, or PoI semantics.

## Build / test results

- Build: EXIT=0, warnings 1 (pre-existing unused `get_port`).
- Unit: 137/137 PASS (`/tmp/phx_testnet_exec7b_20261126/unit.log`).
- CTest: 11/11 PASS (`/tmp/phx_testnet_exec7b_20261126/ctest.log`).
- Network test: 4/4 PASS (incl. 3 new identity tests).
- E2E 4-node regtest smoke: PASS.

## Cross-network isolation matrix

| Local | Remote | Expected | Actual | Evidence |
|---|---|---|---|---|
| Testnet | Testnet (same identity) | ACCEPT | peers=1/ready=1 both | `smoke_identity.sh` |
| Testnet | Regtest | REJECT | testnet peers=0/ready=0; "network magic mismatch" | `smoke_identity.sh` |
| Regtest | Testnet | REJECT | (symmetric; regtest node also rejected) | `smoke_identity.sh` |
| Testnet | Wrong magic | REJECT | unit test PASS | `test_p2p.cpp` |
| Testnet | Wrong genesis | REJECT | unit test PASS | `test_p2p.cpp` |
| Testnet | Zero genesis | REJECT | unit test PASS | `test_p2p.cpp` |

## Same-network four-node result

- E2E 4-node regtest smoke: all 4 finalized height 1, same hash `c5745e...`, consensus_mode=bft, finality_status=finalized. EXIT=0.
- Identity log: testnet magic `0x54505801`, genesis `98a75772...`.
- PoI relay probe (testnet identity): PASS=true (`/tmp/phx_testnet_exec7b_20261126/poi_relay/probe.json`).

## P2P dedup regression

- Network test `p2p_discover_sync_and_tx` PASS (dedup unaffected).

## Consensus/genesis change audit

- No change to consensus algorithms, BFT finality, QC, quorum, proposer, fork-choice, PoI semantics, genesis content, block format, monetary/reward rules.
- Only wiring existing canonical values into `NetworkConfig`.

## Binary identity

- Post-fix binary SHA256: `489a1bcdb403d0aa94aa1100b1980ca191e61fa63563c22b93c5aa6298379293`
- Size: 34509168; mtime 2026-11-26 15:44:16Z.
- HEAD: `d094596549a53eb0695527df06729eca41e737e9`.

## Residual risks

- NetworkConfig still not read from config JSON strings (uses authoritative genesis_block-derived values). This is intentional and safer; config strings are not trusted.
- Fail-closed guards against zero magic/genesis; other invalid configs still rely on resolve_network.
- Shutdown/join lifecycle residual (from exec6 audit) remains.

## TB-023 verdict

**TB-023 VERIFIED/CLOSED FOR INITIAL TESTNET** (subject to independent evidence review). All acceptance criteria met.

## Overall verdict

**TESTNET NO-GO — VPS FROZEN.** TB-023 closure alone does not constitute TESTNET GO; remaining gates (TB-019 monitoring, TB-020 OS-level isolation, TB-021 full matrix, TB-001 authorization, deployment/soak/config acceptance) still required. Stop after this report; no EXEC7C, VPS work, or deployment without further owner authorization.