# PhantomX Development Guide

## Documentation Language

All written project artifacts MUST be in **English**. This applies to:

- Documentation files under `docs/`, `README.md`, and release notes
- Changelog entries, test reports, and audit/report documents
- Source-code comments and commit messages

When updating an existing file, write the new or revised text in English; do not
introduce text in another language, and translate any affected non-English text
you touch. User requests may arrive in any language, but the documentation and
other written artifacts produced for the project must be English.

## Project Layout

```text
.
├── .github/workflows/        # CI workflows
├── cmake/                    # CMake helpers/modules
├── docs/                     # Architecture and development docs
├── include/phantomx/common/  # Public foundation headers
├── src/
│   ├── common/               # Common library implementation
│   ├── daemon/               # phantomxd
│   ├── cli/                  # phantomx-cli
│   ├── wallet/               # phantomx-wallet
│   ├── poi/                  # phantomx-poi
│   └── pool/                 # phantomx-pool
└── tests/
    ├── unit/                 # Unit tests
    └── integration/          # Integration/smoke tests
```

## Foundation Components

| Component | Location | Purpose |
|---|---|---|
| Common types | `include/phantomx/common/types.hpp` | PHX units, constants, address prefix, version/type helpers |
| Error handling | `include/phantomx/common/error.hpp` | `ErrorCode`, `Error`, `Result<T>` |
| Logging | `include/phantomx/common/logging.hpp` | Structured JSON-line logger with levels and fields |
| Serialization | `include/phantomx/common/serialization.hpp` | Binary writer/reader and `Serializable` interface |
| Config loader | `include/phantomx/common/config.hpp` | Simple key=value config file loader |
| CLI parser | `include/phantomx/common/cli.hpp` | Command/options/flags parser with help output |

## C++ Standard

- **C++20** is the current project standard.
- Rationale: broad support on current MSVC/GCC/Clang, no external dependency requiring a newer standard.
- Do not introduce compiler extensions; `CMAKE_CXX_EXTENSIONS` is OFF.

## Build and Test

Configure/build/test using CMake presets where possible. See [`BUILDING.md`](./BUILDING.md).

Typical debug flow:

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug --parallel
ctest --preset linux-gcc-debug
```

## Testing

### Unit tests

Unit tests live in `tests/unit/` and use a small built-in test framework (`tests/test_framework.hpp`).

Run:

```bash
ctest --preset linux-gcc-debug -R phantomx.unit --output-on-failure
```

### Integration tests

Integration tests live in `tests/integration/` and CTest smoke tests execute built binaries with `--version`.

Run:

```bash
ctest --preset linux-gcc-debug -R phantomx.integration --output-on-failure
```

### Adding tests

1. Add a `PHX_TEST(name)` in an existing or new test file.
2. Register the file in `tests/CMakeLists.txt`.
3. Use `PHX_EXPECT(...)` for boolean assertions.
4. Keep tests deterministic; avoid network and timing dependencies.

## Logging

Logging uses structured JSON lines to `std::clog`.

Fields:
- `ts` - UTC timestamp
- `level` - TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL
- `component` - executable/module name
- `msg` - message
- additional key/value fields

Example:

```json
{"ts":"2026-11-23T00:00:00.000Z","level":"INFO","component":"phantomxd","msg":"startup","app":"phantomxd","version":"0.1.0"}
```

## Configuration

The foundation config loader accepts simple `key=value` files:

```ini
# comment
network=mainnet
rpc_port=8740
log_level=info
```

Rules:
- `#` starts a comment.
- Empty lines ignored.
- `[section]` headers are accepted and ignored by the current simple loader.
- Invalid syntax returns `ErrorCode::kConfigError`.

## CLI Conventions

All executables should support:

- `--help`
- `--version`
- `--config <path>`
- `--log-level <level>`

Commands are positional before options; options use `--name value` or `--name=value`; flags are boolean.

## Serialization

The foundation serialization interface is intentionally small and deterministic:

- `BinaryWriter` writes little-endian primitives and length-prefixed bytes/strings.
- `BinaryReader` returns `Result<T>` on parse/read failures.
- `Serializable` provides a virtual interface for future domain objects.

Consensus-critical serialization in future phases must add canonical encoding rules, versioning, domain separation, and golden vectors.

## Error Handling

- Use `ErrorCode` + `Error` for failures.
- Use `Result<T>` for operations that can fail.
- Avoid throwing across module boundaries for expected errors; exceptions may be caught at top-level/test harness boundaries.

## Dependency Management

Current foundation has **no external runtime dependencies**.

Policy for future dependencies:
- Add only audited, pinned dependencies.
- Prefer permissive licenses compatible with project distribution.
- Do not add PhantomX PoSpace/VDF/PoT dependencies for consensus.
- Document new dependencies in CMake and docs, and update CI.

## PhantomX Product Constraints

When extending the foundation, preserve product constraints:

- Independent L1; no PhantomX/Dogecoin fork/copy.
- Consensus target: PoI + Verifier attestation finality.
- No storage mining, storage marketplace, HDD mining, or storage reward in the initial scope.
- Integer `phx_atom` accounting; no floating point in consensus decisions.
- User-facing addresses use prefix `PhantomX` (encoding details per ADR).
- No private key or plaintext user file exposure.

## CI

GitHub Actions workflow: `.github/workflows/ci.yml`.

Matrix covers:
- Windows x64 Debug/Release
- Linux x86_64 Debug/Release
- Linux ARM64 Debug/Release
- macOS Apple Silicon Debug/Release
- macOS Intel Debug/Release (runner-dependent)

CI runs configure, build, and `ctest --output-on-failure`.