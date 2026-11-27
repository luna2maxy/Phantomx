# TECHNICAL DECISIONS

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.

## Status Legend

- **Proposed:** initial direction selected for planning; requires ADR/implementation validation.
- **Accepted:** approved by project owner/maintainers (none in this document unless explicitly stated).
- **Rejected:** explicitly not allowed.

## Decision Log

| ID | Decision | Status | Rationale | Consequences |
|---|---|---|---|---|
| TD-001 | PhantomX is an independent L1, not a PhantomX/Dogecoin fork | Accepted (user) | Avoids inherited consensus/technical debt and license/provenance ambiguity; matches product requirements | Must implement all core components independently; higher initial effort |
| TD-002 | Consensus is `PoI + Verifier attestation Finality` | Accepted (user) | PoI measures contribution; PoI+Verifier provides deterministic finality | Requires formal safety/liveness spec and verifier protocol |
| TD-003 | Exclude PhantomX PoSpace, VDF, Proof of Time from consensus | Accepted (user) | PhantomX consensus primitives do not match PhantomX model | Do not depend on `PhantomXpos`/`PhantomXvdf`; ignore PhantomX sub-slot/signage/VDF logic |
| TD-004 | Transparent UTXO ledger | Accepted (user) | Simple, auditable value transfer model; aligns with integer accounting | Need UTXO index, coin selection, wallet sync model; no account-balance shortcuts in consensus |
| TD-005 | Integer smallest-unit accounting (`phx_atom`) | Accepted (user) | Prevents floating-point nondeterminism and rounding errors | All money/fee/reward logic uses checked integer arithmetic; test vectors required |
| TD-006 | Tokenomics fixed as specified | Accepted (user) | Clear emission and allocation model | Consensus must encode subsidy, halving, and remainder rules with integer accounting. |
| TD-007 | Storage providers paid from user storage fees, not block subsidy | Superseded | Initial direction separated storage economics from block subsidy | Storage subsystem removed from initial scope; no storage rewards/fees in consensus. See README/ROADMAP. |
| TD-008 | PoI evidence requires challenge-response, nonce, signature, freshness, independent verification | Accepted (user) | Prevents naive spoofing/replay/self-reporting | More protocol complexity; verifier infrastructure required |
| TD-009 | PoI is not Sybil-resistant | Accepted (user constraint) | Honest security posture | PoI+Verifier/verifier admission must not rely on PoI alone; need bonds/rate limits/reputation/caps |
| TD-010 | Separate identity keys by role | Proposed | Limits blast radius and role confusion | Key management UX more complex; requires clear registration/rotation flows |
| TD-011 | C++ core with CMake | Proposed | Performance/control for node, consensus, crypto, storage; matches requirement | Requires strong CI, sanitizers, fuzzing, packaging discipline |
| TD-012 | Qt desktop, native C++ CLI, Android+iOS mobile | Proposed | Matches product requirement and platform-native UX | Shared core/sdk boundaries must be carefully designed |
| TD-013 | Canonical binary serialization for consensus/P2P | Proposed | Deterministic hashing/signing and cross-language compatibility | Requires strict schema versioning and test vectors |
| TD-014 | Storage is real file storage with full lifecycle | Superseded | Originally considered core product requirement | Storage subsystem removed from initial scope; no storage lifecycle in consensus. |
| TD-015 | No reward for empty HDD capacity | Accepted (user) | Prevents fake capacity farming | Superseded with TD-014; no storage reward model in initial scope. |
| TD-016 | Pools cannot self-verify contributions | Accepted (user) | Prevents pool-side fraud | Verifier assignment and cross-verification must be independent and auditable |
| TD-017 | Voting power not based on account count | Accepted (user) | Mitigates Sybil voting | Need deterministic voting-power function; likely bond-based with caps |
| TD-018 | No private key/plaintext file exposure | Accepted (user) | Basic security/privacy requirement | RPC/UI/logging/redaction reviews required; encryption by default |
| TD-019 | User-facing address prefix is `PhantomX` | Accepted (user) | Clear product identity and consistent wallet/address UX | Address parser/verifier, wallet UI/CLI, and SDKs must accept/display `PhantomX` prefix; encoding/checksum/network variant finalized by ADR |

## Detailed Decisions

### TD-010: Identity Separation

**Decision:** Use distinct cryptographic identities for node, wallet, verifier, and PoI verifier roles.

**Alternatives considered:**
- Single keypair per participant: simpler UX, but catastrophic compromise blast radius and role ambiguity.
- Wallet key reused as verifier key: operationally convenient, but unsafe for hot verifier nodes.

**Implications:**
- Registration transactions/messages must bind role key to payout address with signature proof.
- Revocation/rotation procedures required.
- UI must clearly label role identities.

### TD-011: C++ Core

**Decision:** Implement consensus-critical and node-critical components in C++.

**Alternatives considered:**
- Python/Go/Rust: faster prototyping, but user requirement states C++ core; mixed-language core increases FFI risk.
- Rust core: strong safety, but conflicts with stated requirement.

**Implications:**
- Use modern C++20/23 subset with strict compiler warnings.
- Mandatory ASan/UBSan/TSan/fuzz targets for parsers, P2P, PoI evidence, storage proofs.

### TD-013: Serialization

**Decision:** Use a canonical serialization format for all signed/hashed consensus objects.

**Alternatives considered:**
- JSON: human-readable but ambiguous/canonicalization risk.
- Ad hoc binary: fast but risky without schema discipline.

**Implications:**
- Every signed structure includes version, domain separator, network ID.
- Generate golden test vectors across platforms/endianness.

### TD-017: Voting Power

**Decision:** Voting power must be deterministic and not account-count based.

**Recommended direction:** PoI+Verifier voting power primarily bonded stake/deposit, with PoI affecting eligibility/rewards; if PoI influences voting power, cap it tightly and model attacks.

**Rationale:** PoI is explicitly non-Sybil-resistant. Using it directly for PoI+Verifier safety can create cheap influence attacks.

**Implication:** Requires economic simulation and formal review before mainnet.

## Open Decisions (Need ADR)

| ADR | Question | Options | Needed before |
|---|---|---|---|
| ADR-001 | PoI+Verifier protocol family | HotStuff-like, Tendermint-like, PBFT-like | Consensus prototype |
| ADR-002 | Verifier voting power formula | Bond-only; bond+PoI cap; bond+reputation cap | Testnet economics |
| ADR-003 | DB engine | SQLite, RocksDB, LMDB | Core persistence MVP |
| ADR-004 | Signature scheme(s) | Ed25519, secp256k1, BLS12-381, mixed | Identity/protocol spec |
| ADR-005 | P2P transport/framing | libp2p-like custom, QUIC/TCP custom, gRPC internal-only | Network prototype |
| ADR-006 | Storage proof scheme | Merkle challenge, PDP/POR-like, erasure-coded proofs | Storage MVP |
| ADR-007 | Qt licensing/distribution | LGPL dynamic, commercial, hybrid | Desktop release |
| ADR-008 | Monitoring UI stack | Rust/Go/Node backend + React/Vue frontend | Future MVP |
| ADR-009 | Address encoding | bech32m HRP `phantomx`, base58check-like, custom canonical encoding; define mainnet/testnet prefixes and payload versioning | Wallet/core MVP |

## Rejected Decisions

| Decision | Why rejected |
|---|---|
| Use PhantomX PoSpace/VDF/PoT in PhantomX consensus | Violates product requirements and different security/economic model |
| Fork or copy PhantomX codebase as starting point | Violates independence/provenance requirement |
| Reward empty HDD capacity | Violates real-storage requirement; invites fake capacity attacks |
| Use floating point in consensus/accounting | Nondeterministic/rounding risk |
| Treat PoI as Sybil resistance | Explicitly disallowed by requirements |