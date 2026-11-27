# PhantomX Cryptography Foundation

## Principles

- **No custom crypto algorithms.** PhantomX uses audited, widely deployed libraries.
- **Role-separated keys.** Wallet, node, verifier, and verifier keys are distinct types and must not be reused across roles.
- **No secret logging.** Private keys, seeds, mnemonics, and raw secret material must never be written to logs, telemetry, crash dumps, or RPC responses.
- **Deterministic verification.** Signatures and message authentication must be verifiable with canonical serialization and explicit domain separation.

## Library Choice

| Library | Version/Integration | Reason | Status |
|---|---|---|---|
| libsodium | System package / vcpkg | Audited, simple misuse-resistant API, Ed25519 signing, secure random (`randombytes_buf`), message authentication (`crypto_auth`), active maintenance, cross-platform | Selected for foundation |

**Why libsodium:**
- Provides modern primitives with conservative defaults and easy-to-audit API.
- `crypto_sign_*` implements Ed25519 signatures.
- `randombytes_buf` provides secure random generation from OS CSPRNG.
- `crypto_auth` provides HMAC-SHA512-256 for symmetric message authentication.
- Avoids implementing cryptographic algorithms in-house.

**Build integration:**
- Linux: `libsodium-dev`
- macOS: `brew install libsodium`
- Windows: `vcpkg install libsodium:x64-windows` + CMake toolchain file

## Algorithms

| Purpose | Algorithm | libsodium API | Notes |
|---|---|---|---|
| Secure random | OS CSPRNG via libsodium | `randombytes_buf` | Used for seeds/nonces/key generation |
| Keypair generation | Ed25519 | `crypto_sign_keypair`, `crypto_sign_seed_keypair` | Deterministic from 32-byte seed when using seed form |
| Transaction/message signing | Ed25519 detached signature | `crypto_sign_detached` | Sign canonical serialized message bytes |
| Signature verification | Ed25519 detached verify | `crypto_sign_verify_detached` | Reject invalid/tampered/wrong-key signatures |
| Message authentication | HMAC-SHA512-256 | `crypto_auth`, `crypto_auth_verify` | Symmetric key required; not a replacement for public signatures |
| Hashing | SHA-256 / SHA-256d | Existing PhantomX primitives | Used for block/tx/merkle; can be migrated to audited lib if needed |

## Key Roles

Keys are separated by role and must not be reused:

| Role | Enum | Purpose |
|---|---|---|
| Wallet signing key | `kWalletSigning` | Signs UTXO transactions/wallet operations |
| Node identity key | `kNodeIdentity` | Authenticates P2P node identity and node-signed messages |
| Verifier identity key | `kVerifierIdentity` | Signs PoI challenges/attestations/verifications |
| Storage provider identity key | `kStorageProviderIdentity` | Signs storage registration, proofs, receipts, and provider messages |

## Key Sizes

| Item | Size |
|---|---:|
| Seed | 32 bytes |
| Public key (Ed25519) | 32 bytes |
| Secret key (libsodium crypto_sign) | 64 bytes |
| Signature (Ed25519) | 64 bytes |
| MAC (crypto_auth) | 32 bytes |

## Key Serialization

- Public key: hex string, 32 bytes = 64 hex chars.
- Secret key: hex string, 64 bytes = 128 hex chars. **Sensitive; never log.**
- Signature: hex string, 64 bytes = 128 hex chars.
- Keystore file: simple `role.field=value` text format; contains secret keys and should be protected (`0600` on Unix best-effort). Future format should migrate to encrypted keystore.

## Message Authentication

`crypto_auth`/`crypto_auth_verify` (HMAC-SHA512-256) are provided for symmetric authenticated channels or internal message authentication where both parties share a 32-byte key. Public blockchain/identity signatures must use Ed25519 keypairs.

## Test Vectors

### Ed25519 deterministic vector (seed-based)

Seed (hex):
`000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f`

Public key (hex):
`03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8`

Libsodium secret key (hex, seed||pub, sensitive):
`000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8`

Message (ASCII `PhantomX test message`, hex):
`5068616e746f6d582074657374206d657373616765`

Signature (hex):
`726485298113f9a367f332bb11ce1fea1bd30483dc9318a3bd85a47e1f3a059509e7b29af536bc2b42ac8940b922be1e7e4b76ba5ca9477a4734bc5f3a2ddc00`

Expected:
- Valid signature verifies: **true**
- Tampered message verifies: **false**
- Wrong key verifies: **false**
- Mutated signature byte verifies: **false**

## Randomness Tests

Automated tests should check basic sanity only:
- `random_bytes` returns success for requested length.
- Two 32-byte samples are extremely unlikely to be equal (sanity check, not proof of randomness).
- No deterministic seed reuse in production random API.

Do **not** claim cryptographic randomness quality from unit tests alone; rely on libsodium/OS CSPRNG and external audits.

## Security Rules

- Never log private keys, seeds, secret keystore contents, or decrypted plaintext.
- Zeroize sensitive buffers where practical before freeing (future hardening).
- Use domain separation for all signed protocol messages.
- Keep key roles separate and enforce at API boundaries.
- Rotate/revoke compromised keys with protocol-defined procedures.

## Implementation Files

- `include/phantomx/common/crypto.hpp`
- `src/common/crypto.cpp`
- `include/phantomx/wallet/keystore.hpp`
- `src/wallet/keystore.cpp`
- `tests/unit/test_crypto.cpp`