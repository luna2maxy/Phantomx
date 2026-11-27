# DEPENDENCY AUDIT

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Audit Scope

This audit covers the four inspected PhantomX reference repositories and proposes an initial dependency policy for PhantomX. It is a **planning audit**, not a legal clearance or security certification. All PhantomX dependencies must be re-audited before inclusion in build manifests.

## PhantomX Reference Repository Licenses

| Repository | License | Source | Notes |
|---|---|---|---|
| `PhantomX-blockchain` | Apache-2.0 | https://raw.githubusercontent.com/PhantomX-Network/PhantomX-blockchain/main/LICENSE | Primary reference repo; Python + bindings; do not copy code |
| `PhantomXpos` | Apache-2.0 | https://raw.githubusercontent.com/PhantomX-Network/PhantomXpos/main/LICENSE | C++ Proof of Space; not for PhantomX consensus |
| `PhantomXvdf` | Apache-2.0 | https://raw.githubusercontent.com/PhantomX-Network/PhantomXvdf/main/LICENSE | C++ VDF/timelord; not for PhantomX consensus |
| `PhantomX-blockchain-gui` | Apache-2.0 | https://raw.githubusercontent.com/PhantomX-Network/PhantomX-blockchain-gui/main/LICENSE | Electron/React GUI; PhantomX will use Qt instead |

**License conclusion:** inspected PhantomX repos are Apache-2.0. Apache-2.0 allows reuse under license terms, but PhantomX policy is **no code copying/forking** from PhantomX for consensus or product identity. Any future exception would require legal review, NOTICE preservation, attribution, and clean-room verification.

## PhantomX Dependency Observations

### PhantomX-blockchain (`pyproject.toml`)

Observed dependencies include:

| Dependency | Observed purpose | PhantomX relevance | Risk / action |
|---|---|---|---|
| `aiohttp` | Async HTTP/RPC/websocket client/server | Low for core C++; maybe for external backend services | Do not adopt into C++ core; evaluate mature C++ HTTP stack or separate service stack |
| `aiosqlite` | Async SQLite access | Medium conceptually; not C++ runtime | Use C++ DB abstraction; SQLite acceptable for MVP if carefully wrapped |
| `anyio` | Async abstraction | Low | Not needed in C++ core |
| `boto3` | S3/cloud integration | Low; potential ops tooling only | Avoid in consensus/core; isolate if ever used |
| `PhantomXbip158` | BIP158 compact filters | Medium for wallet/light-client ideas | Audit separately; not MVP-critical |
| `PhantomXpos`, `PhantomXvdf` | PoSpace/VDF | **None (forbidden for consensus)** | Do not include in PhantomX |
| `PhantomX_rs` | Rust/C++ binding core primitives | Medium as reference for performance boundary | Do not depend on PhantomX-specific bindings; select independent crypto libs |
| `click` | CLI | Low | PhantomX CLI is native C++ |
| `clvm*` | PhantomXlisp/CLVM execution | Low for MVP transparent UTXO | Do not include unless a deliberate, audited VM decision is made |
| `cryptography` | Crypto primitives | Medium category reference | Use vetted C++/Rust crypto libs with clear licensing |
| `dnspython` | DNS seeding/discovery | Medium for peer discovery | Reimplement in C++ or use audited DNS library |
| `filelock`, `packaging`, `psutil`, `pyyaml`, `setproctitle` | Ops/runtime utilities | Low | Replace with C++ equivalents where needed |
| `hsms`, `bip-utils` | Key/signing utilities | Medium | Evaluate for reference only; implement independent key management |
| `zstd`, `lz4` | Compression | Medium for P2P/storage transport | Acceptable category; audit library and bomb protection |

### PhantomX-blockchain-gui (`packages/gui/package.json`)

Observed dependencies include Electron, React 18, MUI, Redux, axios, WalletConnect, charting libs, webpack/babel toolchain, Playwright/Jest, etc.

| Area | Risk | PhantomX action |
|---|---|---|
| Electron/React/MUI stack | Large attack surface, heavy dependency graph | Do not reuse for PhantomX desktop; Qt desktop instead |
| WalletConnect/axios/websocket libs | Supply-chain and API security risk | For mobile/web clients, pin, audit, SBOM, and isolate |
| Build tooling (webpack/babel) | Transitive dependency churn | Prefer simpler modern toolchains for mobile/web clients |

## PhantomX Dependency Policy

### Allowed by default (subject to audit)

- OS/libc/runtime provided by supported platforms.
- Well-maintained permissive-licensed libraries with active security response.
- C++ libraries with reproducible builds, SBOM metadata, and no copyleft contamination for distributed binaries unless explicitly approved.

### Restricted / requires architecture review

- Cryptography libraries (must be audited, deterministic build, side-channel considerations).
- P2P/network stacks with embedded protocol assumptions.
- Database engines affecting consensus reproducibility/performance.
- Serialization frameworks (must be canonical and stable).

### Forbidden

- PhantomX consensus dependencies for PhantomX consensus: `PhantomXpos`, `PhantomXvdf`, PoSpace plot/proof/timelord codepaths.
- Copy-pasted PhantomX source or derived code without legal/provenance review.
- Libraries with unclear license, abandoned maintenance, or unverifiable binaries.
- Floating-point monetary/consensus calculations.

## Proposed PhantomX Dependency Candidates (Initial)

These are **candidates**, not approved dependencies. Each requires ADR, license check, CVE review, and build reproducibility check.

| Subsystem | Candidate | License (verify) | Purpose | Risk | Status |
|---|---|---|---|---|---|
| Build/test | CMake, Ninja, CTest | BSD-like/Apache/MIT (verify per tool) | Build orchestration | Low | Candidate |
| Crypto/hash | libsodium / BLAKE3 / OpenSSL / libsecp256k1 / BLS lib (TBD) | Verify individually | Hashing, signatures, KDF | High - security critical | Needs ADR |
| Serialization | Protobuf / FlatBuffers / custom canonical codec | Verify | P2P/storage/wire formats | Medium - canonicalization bugs | Needs ADR |
| DB | RocksDB / SQLite / LMDB | Verify | UTXO/block/index storage | High - consensus persistence | Needs ADR |
| Networking | libuv/Asio/Boost.Asio/custom | Verify | P2P/event loop | Medium/High | Needs ADR |
| Logging/metrics | spdlog, Prometheus client (optional) | Verify | Ops observability | Low/Medium | Candidate |
| Qt Desktop | Qt 6 | LGPL/Commercial (verify obligations) | Desktop UI | Medium - licensing obligations | Needs legal/product ADR |
| Testing | GoogleTest, libFuzzer/AFL++, sanitizers | Verify | Unit/fuzz/sanitizer tests | Low | Candidate |

## Security and Supply-Chain Controls

1. **Pin versions and hashes:** lockfiles/manifests for every repo.
2. **SBOM generation:** produce SBOM per release artifact.
3. **CVE monitoring:** automated dependency scanning in CI.
4. **Reproducible builds:** deterministic build flags where possible.
5. **Vendoring policy:** only vendor audited dependencies; record upstream commit/tag and license.
6. **Code provenance:** no PhantomX code copy; clean-room implementation for consensus-critical components.
7. **Secrets hygiene:** no private keys in configs/logs/tests; secret scanning in CI.

## Audit Gaps / Next Actions

- Run automated license/SBOM scan once PhantomX code repos/manifests exist.
- Create ADRs for crypto, DB, serialization, P2P, and Qt licensing.
- Perform legal review for Apache-2.0 reference usage policy and Qt distribution model.
- Define threat model for PoI evidence, storage proofs, and PoI+Verifier key management before selecting crypto libs.