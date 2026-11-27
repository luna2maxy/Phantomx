# PHASE B1 — PB-001 BUILD REPORT: Canonical Testnet PoI+BFT

**Status: PARTIAL — core PB-001 implementation/tests pass; recovery/partition full-matrix and fresh sanitizer build NOT VERIFIED**  
**Date:** 2026-11-26  
**Branch:** `encryption`  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged)

## 1. Baseline preservation

| Item | Value |
|---|---|
| Dirty worktree before this phase | 108 (`/tmp/b1_build_baseline_status.txt`) |
| Dirty worktree after this phase | 113+ (see `/tmp/b1_build_final_status.txt` snapshot; additional untracked files from this continuation) |
| Destructive git commands | None |
| VPS deployment/provisioning/public testnet | None (FROZEN) |

`config/testnet.json` is root-owned (`root:root`) and was **not modified**. Canonical BFT config created as new file.

## 2. PB-001 decisions and implementation status

| Decision | Status | Evidence |
|---|---|---|
| PB-001A Canonical testnet PoI+BFT | IMPLEMENTED (config+daemon path) | `config/testnet-bft-canonical.json`, daemon config-driven init |
| PB-001B 4 validators, quorum >2/3, 3/4 equal power | IMPLEMENTED / ENFORCED | `validate_bft_startup` requires exact 4 for operator mode and current validated seed mode; quorum formula unchanged in `src/consensus/bft.cpp` |
| PB-001C Operator-managed keys | IMPLEMENTED | `load_operator_identity`, `load_secret_key_hex_file`, no seed fallback in operator mode, no secret in error/log tests |
| PB-001D New canonical BFT testnet config | IMPLEMENTED | `config/testnet-bft-canonical.json` marked `_canonical_status=CANONICAL_BFT_TESTNET`; `testnet-bft-validation.json` remains isolated |
| PB-001E Two-VPS failure-domain limitation | DOCUMENTED | config warning, deploy README, this report |
| PB-001F Config-driven wiring | IMPLEMENTED | `src/consensus/bft_config.cpp`, daemon uses `parse_bft_config`/`build_validator_set` |

## 3. Config decision (Item 1)

- **Canonical:** `config/testnet-bft-canonical.json`
- **Legacy/root-owned:** `config/testnet.json` (no BFT section) - **not canonical PB-001 BFT testnet**; owner/repo decision needed to replace/rename/remove ambiguity. Cannot be modified in-place due permissions.
- **Isolated:** `config/testnet-bft-validation.json` - remains ISOLATED TEST-ONLY.
- Deployment templates updated to reference `testnet-bft-canonical.json` and `PHANTOMX_BFT_SIGNING_KEY_FILE`.

## 4. Validator policy scope (Item 2)

- Operator mode (PB-001 canonical): exact 4 validators enforced.
- Seed mode: exact 4 enforced as currently validated/supported size; message states PB-001B scope/current validated seed mode. This is conservative/fail-closed; it does not silently allow unvalidated sizes. Any future size change requires separate validation and approval.
- Quorum/finality formulas unchanged.

## 5. Operator key procedure (Item 3)

- Generation: `scripts/generate-bft-operator-keys.sh <out-dir> [count]` - uses libsodium ctypes, writes 0600 key files in 0700 dir, prints only pubkeys/paths; tested: 4 keys generated, `secret_hex_in_stdout 0`.
- Installation: place key file with mode 0600, configure `--bft-signing-key-file` or `bft.signing_key_file`; daemon validates match against config pubkey.
- Verification: `load_operator_identity` verifies parse, sign/verify probe, and configured pubkey membership; startup fail-closed.
- Backup: file copy preserves content; tested in `pb001_operator_key_rotation_and_backup` (backup load ok).
- Rotation: replace config pubkey + install new key file + restart; old key rejected against new config. Tested in `pb001_operator_key_rotation_and_backup`.
- No private keys committed. Template pubkeys in canonical config must be replaced before real deployment.

## 6. Full multi-node acceptance (Item 4)

### Executed
| Scenario | Result | Evidence |
|---|---|---|
| 4-node operator-key startup + propose/finalize | PASS | `/tmp/phx-pb001-op3`: all 4 ready, bft_status ok, block proposed `c4d261...a1d7`, all block_count=1 |
| 3-node quorum (1 validator stopped) | PASS | `/tmp/phx-pb001-mn`: after stopping node3, 3 remaining nodes proposed and reached height 2 |
| 2-node quorum loss / cannot finalize | PASS | `/tmp/phx-pb001-mn`: with nodes 2,3 stopped, `before=2 after0=2 after1=2`, `TWO_VALIDATORS_CANNOT_FINALIZE=PASS` |
| Recovery after restart of stopped validators | **PARTIAL / NOT VERIFIED** | `/tmp/phx-pb001-mn`: nodes 2,3 restarted and RPC became ready, but P2P mesh recovery incomplete in window; proposed height 3 not finalized, all nodes remained at block_count 2. Logs show height mismatch/stale view-change and missing peer connections. Requires investigation. |
| Network partition simulation (two isolated 2-node groups) | NOT RUN | Not executed in this pass; required before PB-001 COMPLETE |
| Explicit 2-validator partition cannot falsely finalize | PARTIAL | 2-node quorum-loss test shows no finalization; isolated 2+2 partition test NOT RUN |

### Existing regtest evidence
| Command | Exit | Result |
|---|---:|---|
| `BFT_BASE=/tmp/phx-pb001-regtest timeout 120 bash scripts/run-bft-regtest.sh` + manual propose | 0 | 4 RPC ready, propose accepted, all 4 block_count=1 |
| `PHANTOMX_BUILD_DIR=build-ubuntu timeout 300 bash scripts/e2e-bft-4node.sh` | 0 | PASS, finalized height 1, all 4 nodes consistent `finalized_hash=786dc540...c023`, `success_artifacts=/tmp/e2e-catch/pass_manual_1795689290820817123` |

## 7. Final regression (Item 5)

| Command | Exit | Result |
|---|---:|---|
| `cmake --build build-ubuntu --target phantomxd phantomx_unit_tests -j6` | 0 | PASS |
| `./build-ubuntu/tests/phantomx_unit_tests` | 0 | **135 passed, 0 failed, 135 total** |
| `cmake --build build-ubuntu -j6` | 0 | PASS |
| `ctest --test-dir build-ubuntu --output-on-failure` | 0 | **11/11 PASS** |
| ASAN `./build-asan/tests/phantomx_unit_tests` | 0 | **65 passed, 0 failed, 65 total** but **STALE BUILD** (pre-PB-001 changes; not rebuilt with current source) |
| UBSAN/TSan | NOT RUN | build dirs not present in workspace (`build-ubsan`, `build-tsan` missing) |
| `scripts/e2e-bft-4node.sh` | 0 | PASS |
| `scripts/run-bft-regtest.sh` | 0 | PASS (cluster start + manual propose/finalize smoke) |

## 8. Files changed/created in PB-001

- `include/phantomx/consensus/bft_config.hpp` (new)
- `src/consensus/bft_config.cpp` (new)
- `src/daemon/main.cpp` (modified)
- `CMakeLists.txt` (modified)
- `tests/unit/test_consensus.cpp` (modified)
- `config/testnet-bft-canonical.json` (new)
- `scripts/generate-bft-operator-keys.sh` (new)
- `deploy/testnet/README.md`, `deploy/testnet/configs/node-a.env.example`, `deploy/testnet/configs/node-b.env.example`, `deploy/testnet/systemd/phantomxd@.service` (updated drafts)
- `docs/reports/PHASE_B1_PB001_BUILD_REPORT.md` (this report)

## 9. Consensus/protocol invariants

Unchanged: quorum formula >2/3, finality/QC/view-change/proposer/vote semantics, genesis, network IDs, monetary/reward economics, block format, PoP semantics. No mainnet config change.

## 10. AUTO-FIX SUMMARY

| Error | Root cause | Fix | Files | Attempts | Post-fix |
|---|---|---|---|---:|---|
| Operator-mode nodes 1-3 rejected startup in 4-node smoke | `--node-index` default=1 always present in parsed args; mismatch check triggered for operator identities with derived index != 0 | Detect explicit `--node-index` in argv; enforce consistency only when explicitly provided | `src/daemon/main.cpp` | 1 | 4-node operator smoke PASS, all block_count=1 |

## 11. Remaining blockers / owner review items

1. **PB-001 config path ambiguity:** `config/testnet.json` root-owned/legacy; canonical is `config/testnet-bft-canonical.json`. Owner decision needed.
2. **Recovery/partition matrix incomplete:** restart recovery did not finalize in one run (P2P mesh recovery/liveness issue observed). 2+2 partition and false-finality proof NOT RUN.
3. **ASAN stale:** build-asan binaries predate PB-001 changes; rebuild not run. UBSAN/TSan build dirs absent.
4. **Validator count policy:** currently exact-4 enforced for current validated BFT modes (conservative). Confirm scope.
5. **Template pubkeys:** must be replaced before any real deployment.

## 12. Verdict

- **PB-001:** **PARTIAL**. Items 1-3 substantially complete with evidence; Item 4 partially complete (key safety case 2-of-4 cannot finalize PASS; recovery/partition incomplete); Item 5 partially complete (regressions pass, e2e/regtest pass, ASAN stale, UBSAN/TSan not available).
- **TESTNET GO:** **NO-GO**.
- **Phase B2:** **DO NOT START** until owner reviews PB-001 and remaining gaps are accepted/resolved.
- **VPS deployment:** **FROZEN**.
