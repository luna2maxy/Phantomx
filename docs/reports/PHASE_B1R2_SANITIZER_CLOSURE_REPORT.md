# PHASE B1-R2 — Sanitizer Closure Report

## A. Repository baseline
- **Branch:** `encryption`
- **HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged)
- **Dirty worktree (B1-R2 start):** 114 entries
- **Dirty worktree (current):** 114+ (this report added; existing dirty work preserved)
- **Destructive Git operations:** None. No reset/clean/checkout --/restore. Root-owned `build-asan` untouched (no permission/ownership changes).

## B. Environment
- **OS/kernel:** Linux (workspace host; see `/tmp` logs captured on host)
- **CMake:** 4.2.3
- **Compiler:** g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
- **clang++:** not present (gcc sanitizer runtimes used)
- **Existing root-owned dir:** `build-asan` (`drwxr-xr-x root root`) — untouched, not used for evidence.

| Build dir | Build type | Flags | Purpose |
|---|---|---|---|
| `build-asan-pb001r2` | Debug | `-fsanitize=address -fno-omit-frame-pointer -g` | ASAN current-source |
| `build-ubsan-pb001r2` | Debug | `-fsanitize=undefined -fno-omit-frame-pointer -g` | UBSAN current-source |
| `build-tsan-pb001r2` | Debug | `-fsanitize=thread -fno-omit-frame-pointer -g` | TSan current-source |

CMake common options: `-DPHANTOMX_BUILD_TESTS=ON -DPHANTOMX_BUILD_INTEGRATION_TESTS=ON -DPHANTOMX_BUILD_QT=OFF -DPHANTOMX_WARNINGS_AS_ERRORS=OFF`.

## C. ASAN
- **Configuration result:** PASS (exit 0). `cmake -S . -B build-asan-pb001r2 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DPHANTOMX_BUILD_TESTS=ON -DPHANTOMX_BUILD_INTEGRATION_TESTS=ON -DPHANTOMX_BUILD_QT=OFF -DPHANTOMX_WARNINGS_AS_ERRORS=OFF`
- **Build result:** PASS (exit 0). `cmake --build build-asan-pb001r2 --target phantomx_unit_tests phantomxd -j6`
- **Instrumentation verification:** PASS. `libasan.so.8` linked; `__asan_*` symbols present in `build-asan-pb001r2/tests/phantomx_unit_tests`.
- **Unit test result:** **135 passed, 0 failed, 135 total** (exit 0).
- **Command:** `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 ./build-asan-pb001r2/tests/phantomx_unit_tests` (log: `/tmp/pb001r2-asan.log`)
- **Runtime reports:** No `ERROR: AddressSanitizer`, `ERROR: LeakSanitizer`, or `SUMMARY: AddressSanitizer` found.
- **Exit codes:** configure 0, build 0, test 0.

## D. UBSAN
- **Configuration result:** PASS (exit 0). CMake flags `-fsanitize=undefined -fno-omit-frame-pointer -g`.
- **Build result:** PASS (exit 0). `cmake --build build-ubsan-pb001r2 --target phantomx_unit_tests phantomxd -j6`
- **Instrumentation verification:** PASS. `libubsan.so.1` linked; `__ubsan_*` symbols present.
- **Unit test result:** **135 passed, 0 failed, 135 total** (exit 0).
- **Command:** `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1 ./build-ubsan-pb001r2/tests/phantomx_unit_tests` (log: `/tmp/pb001r2-ubsan.log`)
- **Runtime reports:** No `runtime error:` or `SUMMARY: UndefinedBehaviorSanitizer` found.
- **Exit codes:** configure 0, build 0, test 0.

## E. TSan
- **Configuration result:** PASS (exit 0). CMake flags `-fsanitize=thread -fno-omit-frame-pointer -g`.
- **Build result:** PASS (exit 0). `cmake --build build-tsan-pb001r2 --target phantomx_unit_tests phantomxd -j6`
- **Instrumentation verification:** PASS. `libtsan.so.2` linked; `__tsan_*` symbols present.
- **Unit test result:** **135 passed, 0 failed, 135 total** (exit 0).
- **Command:** `TSAN_OPTIONS="halt_on_error=1:abort_on_error=1:history_size=7" timeout 600 ./build-tsan-pb001r2/tests/phantomx_unit_tests` (log: `/tmp/pb001r2-tsan.log`)
- **Runtime reports:** No `WARNING: ThreadSanitizer`, `SUMMARY: ThreadSanitizer`, or `data race` found.
- **Exit codes:** configure 0, build 0, test 0.

## F. Regression
| Check | Command | Exit | Result |
|---|---|---:|---|
| Unit tests (build-ubuntu) | `./build-ubuntu/tests/phantomx_unit_tests` | 0 | **135 passed, 0 failed, 135 total** |
| CTest (build-ubuntu) | `ctest --test-dir build-ubuntu --output-on-failure` | 0 | **11/11 PASS** |
| BFT regtest + manual propose/check | `BFT_BASE=/tmp/phx-pb001-regtest timeout 120 bash scripts/run-bft-regtest.sh` + propose/check | 0 | 4 RPC ready, all block_count=1 |
| 4-node E2E | `PHANTOMX_BUILD_DIR=build-ubuntu timeout 300 bash scripts/e2e-bft-4node.sh` | 0 | PASS, finalized height 1, consistent hash `786dc5407295d8fa8243639998c52d49bf4aa553a007fd2f5d2e655386e2c023` |

Baseline comparison: matches reported baseline (135/135, 11/11, BFT E2E PASS, consistent finality).

## G. Findings

| Category | Finding |
|---|---|
| PASS | ASAN current-source: configured, built, instrumented, 135/135, no diagnostics. |
| PASS | UBSAN current-source: configured, built, instrumented, 135/135, no diagnostics. |
| PASS | TSan current-source: configured, built, instrumented, 135/135, no diagnostics. |
| PASS | build-ubuntu regressions and BFT regtest/E2E PASS. |
| WARNING | Sanitizer PASS is unit-test-scope evidence on this host/toolchain; not a formal proof of absence of memory/UB/race bugs in unexercised paths. |
| WARNING | TSan not run with full integration/multi-process stress beyond unit tests. |
| Environment limitation | Old `build-asan` root-owned/unwritable; not modified, not used as evidence. |
| NOT RUN | Broader sanitizer integration stress (out of declared B1-R2 scope). |
| BLOCKED | None. |
| FAIL | None. |

## H. Final verdict

**COMPLETE**

All required B1-R2 sanitizer checks were executed from current source with verified instrumentation and runtime evidence. No unresolved sanitizer errors. Regression baseline intact. No protocol/consensus/config/source-code semantic changes made. VPS deployment remains FROZEN. TESTNET GO remains NO-GO. Phase B2 not started.
