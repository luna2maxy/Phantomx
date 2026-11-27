# PHANTOMX — Private Testnet Build Acceptance

**Date:** 2026-11-26  
**Binary:** `build-ubuntu/bin/phantomxd`  
**SHA256:** `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`  
**Size:** 34509168 bytes  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (baseline) / `4d24d21` (current, post-impl).

## Build
- Command: `cmake --build build-ubuntu -j6`
- Exit: 0
- Warnings: 1 (pre-existing unused `get_port` in main.cpp)
- ASAN build: `build-ubuntu-asan`, 0 findings

## Tests
| Suite | Result |
|---|---|
| Unit tests | 137/137 PASS |
| CTest | 11/11 PASS |
| Network tests | 4/4 PASS (incl. 3 new identity tests) |
| E2E 4-node | PASS |
| ASAN unit | 137/137, 0 findings |
| ASAN CTest | 11/11 PASS |

## Sanitizers
- ASAN/UBSAN: 137/137 PASS, 0 findings (executed on build-ubuntu-asan).
- TSan: not rerun in exec7 (prior exec2-5 evidence: 135/135 PASS).

## Source identity
- HEAD: `d094596549a53eb0695527df06729eca41e737e9` (baseline).
- Modified files (approved): `src/database/sqlite_db.cpp` (Alternative F), `src/network/node.cpp` (dedup), `src/daemon/main.cpp` (network identity), `include/phantomx/network/peer.hpp` (local_port), `tests/network/test_p2p.cpp` (3 identity tests), `src/rpc/http_server.cpp`, `src/rpc/node_handlers.cpp`, `tests/unit/test_poi.cpp` (prior execs).

## Consensus/genesis audit
- No changes to consensus, genesis content, block format, monetary/reward, BFT finality/QC/quorum/proposer/fork-choice, or PoI semantics.
- Only wiring of existing canonical values and non-consensus P2P/RPC management.

## Verdict
Build acceptance: **PASS** (with documented limitations: TSan not rerun in exec7, 1 pre-existing warning).