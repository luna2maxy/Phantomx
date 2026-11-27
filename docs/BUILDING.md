# Building PhantomX

## Prerequisites

PhantomX foundation is a C++20 project built with CMake.

### Required tools

| Tool | Minimum | Notes |
|---|---:|---|
| CMake | 3.20 | Configure/build/test driver |
| C++ compiler | C++20-capable | MSVC 19.29+, GCC 10+, Clang 12+ recommended |
| Ninja | 1.10+ | Recommended generator; used by CMake presets |
| Git | 2.x | Source control |

### Platform targets

| Platform | Architecture | Status |
|---|---|---|
| Windows | x64 | CI preset: `windows-msvc-debug/release` |
| Linux | x86_64 | CI preset: `linux-gcc-debug/release` |
| Linux | ARM64 | CI preset: `linux-gcc-debug/release` on ARM runner |
| macOS | Apple Silicon | CI preset: `macos-clang-debug/release` |
| macOS | Intel | CI preset: `macos-clang-debug/release` on Intel runner (if dependency/toolchain support remains available) |

## C++ Standard

The project currently uses **C++20** (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_EXTENSIONS OFF`).

Rationale:
- Supported by modern MSVC/GCC/Clang toolchains used in CI.
- Provides useful standard-library and language features without external dependencies.
- No current dependency requires C++23; standard can be revisited when dependencies/platform constraints change.

## Quick Start

### Configure

```bash
cmake --preset linux-gcc-debug
```

### Build

```bash
cmake --build --preset linux-gcc-debug --parallel
```

### Test

```bash
ctest --preset linux-gcc-debug
```

## Windows (MSVC x64)

Use a Developer Command Prompt / PowerShell with MSVC environment loaded, or let CI use `ilammy/msvc-dev-cmd`.

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
ctest --preset windows-msvc-debug
```

Release:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
ctest --preset windows-msvc-release
```

## Linux

Install build tools (example Ubuntu/Debian):

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
```

Debug:

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug --parallel
ctest --preset linux-gcc-debug
```

Release:

```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release --parallel
ctest --preset linux-gcc-release
```

## macOS

Install Xcode Command Line Tools and Ninja:

```bash
xcode-select --install
brew install cmake ninja
```

Debug:

```bash
cmake --preset macos-clang-debug
cmake --build --preset macos-clang-debug --parallel
ctest --preset macos-clang-debug
```

Release:

```bash
cmake --preset macos-clang-release
cmake --build --preset macos-clang-release --parallel
ctest --preset macos-clang-release
```

## Build Outputs

Executables are placed under:

```text
build/<preset>/bin/
```

Foundation executables:

- `phantomxd`
- `phantomx-cli`
- `phantomx-wallet`
- `phantomx-poi`
- `phantomx-pool`

## CMake Options

| Option | Default | Description |
|---|---:|---|
| `PHANTOMX_BUILD_TESTS` | ON | Build unit/integration tests |
| `PHANTOMX_BUILD_INTEGRATION_TESTS` | ON | Build integration and smoke tests |
| `PHANTOMX_ENABLE_WARNINGS` | ON | Enable compiler warnings |
| `PHANTOMX_WARNINGS_AS_ERRORS` | OFF | Treat warnings as errors |
| `PHANTOMX_ENABLE_INSTALL` | ON | Enable install targets |

Example:

```bash
cmake -S . -B build/custom -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPHANTOMX_WARNINGS_AS_ERRORS=ON
cmake --build build/custom --parallel
ctest --test-dir build/custom --output-on-failure
```

## Troubleshooting

- **CMake not found:** install CMake 3.20+ and ensure it is on PATH.
- **Ninja not found:** install Ninja or change generator.
- **MSVC not found:** run from Visual Studio Developer prompt or use `vcvarsall.bat`.
- **No compiler found:** install platform toolchain (Visual Studio Build Tools, build-essential, or Xcode CLT).
- **Tests not discovered:** configure with `PHANTOMX_BUILD_TESTS=ON` and run `ctest --output-on-failure`.