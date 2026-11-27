# PHASE B1-R — BFT Recovery, Partition Safety & Sanitizer Closure

**Status: PARTIAL**  
**Date:** 2026-11-26  
**Branch:** `encryption`  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged)  
**Scope:** Only items 1-8 requested. No SQLite 5/261, POI Pool, or VPS deployment work performed.

## 1. Baseline preservation
- Dirty worktree preserved; no destructive git commands.
- No VPS/provisioning/public testnet actions.

## 2. Recovery of 2 validators after restart

### Investigation
Previous PB-001 run showed restart recovery not finalizing in the observation window. Reproduction with longer post-restart settle time showed recovery succeeds. Root cause: **observation/timing window too short in prior run**; P2P mesh and BFT view-change needed additional settle time after restart. No code fix required in this pass; evidence captured.

### Evidence
Initial reproduction (`/tmp/phx-b1r`, 15s settle, 10s post-propose): after 2-validator stop and restart, all 4 nodes finalized to height 4. Proposals observed at height 3 and 4.

3-trial reliability run (`/tmp/pb001_recovery_trials.sh`):
| Trial | before 2-node stop | after 2-node propose | final all-4 counts | 2-node no-finalize | Result |
|---:|---:|---:|---|---:|---|
| 1 | 2 | 2 | 4,4,4,4 | PASS | TRIAL_PASS |
| 2 | 2 | 2 | 3,3,3,3 | PASS | TRIAL_PASS |
| 3 | 2 | 2 | 4,4,4,4 | PASS | TRIAL_PASS |

Conclusion: **2-validator restart recovery PASS** under tested local conditions (3/3 trials). This is local multi-process evidence, not physical failure-domain proof.

## 3. 2+2 network partition

Test (`/tmp/pb001_partition.sh`, ports 30641-44/30651-54): all 4 started fully connected, finalized height 1. Restarted as two isolated partitions {0,1} and {2,3} with only intra-partition --connect peers. Peer counts observed as 2,2,2,2 (intra-partition connectivity). 

## 4. No false finality

During 2+2 partition, before propose counts 1,1,1,1; after propose attempts on all nodes and 10s wait, counts remained 1,1,1,1. **NO_FALSE_FINALITY=PASS**. One proposer returned "status: proposed" but no block was finalized in either partition (counts unchanged).

## 5. Recovery after partition heal

Stopped partitioned nodes, restarted all 4 fully connected. After 15s settle, counts 1,1,1,1 (no false-advanced state). After propose, all 4 reached 2,2,2,2; `bft_commit_finality_applied` observed at height 2 on all nodes (n0-n3). **Partition heal recovery PASS**.

## 6. ASAN rebuild from current source

**NOT RUN / BLOCKED by environment permissions.** `build-asan` is root-owned (`drwxr-xr-x root root`), CMakeCache/root files not writable by ubuntu user. Attempted rebuild failed:

- Command: `cmake --build build-asan --target phantomx_unit_tests phantomxd -j4`
- Exit: 1
- Error: `CMake Error: : System Error: Permission denied` writing `build-asan/CMakeCache.txt.tmp*` / configure incomplete.

Stale ASAN binary previously run (pre-PB-001): `./build-asan/tests/phantomx_unit_tests` → 65 passed, 0 failed (stale, not evidence for current source).

## 7. UBSAN/TSan

**NOT RUN / UNAVAILABLE in workspace.** `build-ubsan` and `build-tsan` directories not present. No existing configured UBSAN/TSan build dirs found.

## 8. Regtest and regression rerun

| Command | Exit | Result |
|---|---:|---|
| `./build-ubuntu/tests/phantomx_unit_tests` | 0 | **135 passed, 0 failed, 135 total** |
| `ctest --test-dir build-ubuntu --output-on-failure` | 0 | **11/11 PASS** |
| `BFT_BASE=/tmp/phx-pb001-regtest timeout 120 bash scripts/run-bft-regtest.sh` + manual propose/check | 0 | 4 RPC ready, all block_count=1 |
| `PHANTOMX_BUILD_DIR=build-ubuntu timeout 300 bash scripts/e2e-bft-4node.sh` | 0 | PASS, finalized height 1, consistent hash `786dc5407295d8fa8243639998c52d49bf4aa553a007fd2f5d2e655386e2c023` |

## 9. Evidence integrity / limitations

- All multi-node evidence is local multi-process on one host, not physical multi-VPS/failure-domain proof.
- Recovery PASS observed in 3/3 trials with 15s settle window; this is not a long soak or adversarial network proof.
- Partition simulation used process isolation via separate --connect groups and restarts; not a kernel/network-namespace packet-partition proof. No cross-partition connection was observed (peer counts 2,2,2,2) and no false finality occurred.
- ASAN current-source rebuild blocked by root-owned build-asan directory. UBSAN/TSan not configured.

## 10. Verdict

**PHASE B1-R: PARTIAL**

- Recovery after 2-validator restart: **PASS (3/3 local trials)**.
- 2+2 partition no false finality: **PASS**.
- Partition heal recovery: **PASS**.
- ASAN current-source rebuild: **BLOCKED (permissions)**.
- UBSAN/TSan: **NOT AVAILABLE**.
- Regressions: **PASS** (135/135 unit, 11/11 ctest, regtest/e2e PASS).

Phase B2 remains **NOT STARTED**. VPS deployment remains **FROZEN**. TESTNET GO remains **NO-GO**.
