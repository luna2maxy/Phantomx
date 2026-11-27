# PHASE B1-R2 — Current-Source Sanitizer Closure

**Status: COMPLETE** (for ASAN/UBSAN/TSan instrumented unit-test execution on this host; environment limitations documented)  
**Date:** 2026-11-26  
**Branch:** `encryption`  
**HEAD:** `d094596549a53eb0695527df06729eca41e737e9` (unchanged)

## 1. Baseline preservation

| Item | Value |
|---|---|
| `git rev-parse HEAD` | `d094596549a53eb0695527df06729eca41e737e9` |
| `git branch --show-current` | `encryption` |
| `git status --short | wc -l` (start of B1-R2) | 114 |
| Dirty worktree | Preserved; no destructive git commands |
| Existing root-owned `build-asan` | **Untouched** (`drwxr-xr-x root root`, `CMakeCache.txt` root-owned) |

## 2. Toolchain / environment

| Item | Value |
|---|---|
| CMake | 4.2.3 |
| C++ compiler | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 |
| clang++ | Not found (not required; gcc sanitizers used) |
| ASAN runtime | `libasan.so.8` present |
| UBSAN runtime | `libubsan.so.1` present |
| TSan runtime | `libtsan.so.2` present |

## 3. Build directories (writable, current source)

| Dir | Flags | Ownership |
|---|---|---|
| `build-asan-pb001r2` | `-fsanitize=address -fno-omit-frame-pointer -g` | user-writable (ubuntu) |
| `build-ubsan-pb001r2` | `-fsanitize=undefined -fno-omit-frame-pointer -g` | user-writable (ubuntu) |
| `build-tsan-pb001r2` | `-fsanitize=thread -fno-omit-frame-pointer -g` | user-writable (ubuntu) |

CMake config common: `-DCMAKE_BUILD_TYPE=Debug -DPHANTOMX_BUILD_TESTS=ON -DPHANTOMX_BUILD_INTEGRATION_TESTS=ON -DPHANTOMX_BUILD_QT=OFF -DPHANTOMX_WARNINGS_AS_ERRORS=OFF`.

## 4. ASAN (current source)

- **Configure:** `cmake -S . -B build-asan-pb001r2 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DPHANTOMX_BUILD_TESTS=ON -DPHANTOMX_BUILD_INTEGRATION_TESTS=ON -DPHANTOMX_BUILD_QT=OFF -DPHANTOMX_WARNINGS_AS_ERRORS=OFF` — exit 0.
- **Build:** `cmake --build build-asan-pb001r2 --target phantomx_unit_tests phantomxd -j6` — exit 0.
- **Instrumentation verified:** `libasan.so.8` linked; `__asan_*` symbols present.
- **Run:** `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 ./build-asan-pb001r2/tests/phantomx_unit_tests` — exit 0.
- **Result:** **135 passed, 0 failed, 135 total**.
- **Sanitizer diagnostics:** no `ERROR: AddressSanitizer` / `ERROR: LeakSanitizer` / `SUMMARY: AddressSanitizer` in `/tmp/pb001r2-asan.log`.
- **Status:** **PASS** (current source).

## 5. UBSAN (current source)

- **Configure:** `cmake -S . -B build-ubsan-pb001r2 ... -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer -g" ...` — exit 0.
- **Build:** `cmake --build build-ubsan-pb001r2 --target phantomx_unit_tests phantomxd -j6` — exit 0.
- **Instrumentation verified:** `libubsan.so.1` linked; `__ubsan_*` symbols present.
- **Run:** `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1 ./build-ubsan-pb001r2/tests/phantomx_unit_tests` — exit 0.
- **Result:** **135 passed, 0 failed, 135 total**.
- **Sanitizer diagnostics:** no `runtime error:` / `SUMMARY: UndefinedBehaviorSanitizer` in `/tmp/pb001r2-ubsan.log`.
- **Status:** **PASS** (current source).

## 6. TSan (current source)

- **Configure:** `cmake -S . -B build-tsan-pb001r2 ... -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" ...` — exit 0.
- **Build:** `cmake --build build-tsan-pb001r2 --target phantomx_unit_tests phantomxd -j6` — exit 0.
- **Instrumentation verified:** `libtsan.so.2` linked; `__tsan_*` symbols present.
- **Run:** `TSAN_OPTIONS="halt_on_error=1:abort_on_error=1:history_size=7" timeout 600 ./build-tsan-pb001r2/tests/phantomx_unit_tests` — exit 0.
- **Result:** **135 passed, 0 failed, 135 total**.
- **Sanitizer diagnostics:** no `WARNING: ThreadSanitizer` / `SUMMARY: ThreadSanitizer` / `data race` in `/tmp/pb001r2-tsan.log`.
- **Status:** **PASS** (current source, unit-test scope; not a proof of absence of races in unexercised paths).

## 7. Post-sanitizer regression (build-ubuntu, no source changes)

| Command | Exit | Result |
|---|---:|---|
| `./build-ubuntu/tests/phantomx_unit_tests` | 0 | **135 passed, 0 failed, 135 total** |
| `ctest --test-dir build-ubuntu --output-on-failure` | 0 | **11/11 PASS** |

## 8. Files modified

- `docs/reports/PHASE_B1R2_SANITIZER_REPORT.md` (this report).  
No source code, consensus, config, or protocol files modified. Root-owned `build-asan` untouched.

## 9. Limitations / remaining risks

- ASAN/UBSAN/TSan PASS is limited to executed unit-test scope on this host/toolchain; not a formal proof of absence of memory/UB/race bugs.
- TSan was not run with full integration/multi-process stress beyond unit tests.
- Old root-owned `build-asan` remains stale and unwritable; it should not be used as evidence.
- B1-R recovery/partition evidence remains local multi-process, not physical multi-VPS.
- PB-001 still has open items outside this phase (config path ambiguity decision, template key replacement procedure for real deployment). Phase B2/SQLite 5/261/POI Pool/VPS remain out of scope and not started.

## 10. Final status

| Sanitizer | Status |
|---|---|
| ASAN (current source) | **PASS** |
| UBSAN (current source) | **PASS** |
| TSan (current source) | **PASS** |

**Phase B1-R2: COMPLETE** (within declared scope). VPS deployment **FROZEN**. TESTNET GO **NO-GO**. Phase B2 **NOT STARTED**; awaiting owner review.
