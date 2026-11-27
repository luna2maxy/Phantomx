# PhantomX Mobile Wallet

## Overview

PhantomX mobile wallet targets Android and iOS using a **light-client architecture** by default. Full node on mobile is not the default due to resource, battery, bandwidth, and storage constraints; a full-node mode may be evaluated only with strong technical justification.

## Outputs

- `mobile/android/` - Android foundation skeleton (Gradle/Kotlin).
- `mobile/ios/` - iOS foundation skeleton (SwiftUI).

## Light-Client Architecture

The mobile app does not download/validate the full chain like a desktop full node by default. Instead it:

1. Connects to configured/trusted PhantomX RPC endpoints (and storage APIs for storage features).
2. Fetches headers, proofs, balances, txs, PoI/storage/reward data as needed.
3. Validates signatures/proofs/transaction data locally where possible.
4. Keeps private keys/seeds on-device in secure OS storage.

## Trust Assumptions (Light Client)

| Assumption | Risk | Mitigation |
|---|---|---|
| RPC may be malicious or compromised | False balances, tx history, fee estimates, or status | Use multiple RPC endpoints, verify signed data/proofs, pin trusted endpoints, compare responses |
| Network partitions/eclipse | Stale or manipulated view | Multi-endpoint checks, finality certificate verification when available, user warnings |
| Remote indexer/RPC may omit data | Missing tx/history | Query multiple sources, allow user-configured trusted node |
| Push notifications are third-party | Metadata leakage/spoofing | Optional; no secrets in payload; verify against RPC on open |
| Storage API may be malicious | Fake upload/download/contract status | Verify manifest/chunk hashes, signatures, and contract/lease proofs |

Light client **does not** provide the same trust model as a fully validating node. Users needing maximum trust should run/point to their own trusted PhantomX node.

## Features

| Feature | Status | Notes |
|---|---|---|
| Create wallet | Foundation | Local key generation; secure OS keystore |
| Import wallet | Foundation | Local import only; seed never sent to server |
| Secure seed | Required | Android Keystore / iOS Keychain/Secure Enclave; no plaintext |
| Balance | Requires RPC | From RPC/light-client proofs; no dummy production data |
| Send | Requires RPC | Local sign, broadcast via RPC, validate response |
| Receive | Foundation | Local address/QR generation |
| QR scanner | Planned | Scan addresses/payment requests; validate format/network |
| Transaction history | Requires RPC/indexer | Verified where possible; no dummy production data |
| Address book | Planned | Local-only storage |
| Push notification | Optional | No secrets; verify on app open |
| Storage contract management | Requires storage API | Contracts/leases/proofs/settlement status |
| Upload | Requires storage API | Client-side encrypt/chunk/manifest before upload |
| Download | Requires storage API | Verify chunk/manifest/hash, decrypt locally |
| Storage payment | Requires chain/storage settlement | User-funded storage fees; clear signing UI |
| Reward history | Requires RPC/indexer | Verified claim/history data |

## Security Requirements

- **Android Keystore** for key/seed protection; hardware-backed when available.
- **iOS Keychain / Secure Enclave** for key/seed protection where available.
- **No plaintext seed storage.** Seed/mnemonic encrypted at rest or stored only in secure OS keystore; avoid logs/screenshots/backups unless explicitly encrypted.
- **No private key transmission.** Signing happens on-device; RPC/storage APIs never receive private keys or seeds.
- **Transaction validation.** Validate address/network, amounts, fees, outputs, and signed tx before broadcast; display clear confirmation.
- **Malicious RPC response protection.** Validate schema, signatures/proofs, network/chain identifiers, and cross-check critical data when possible.
- **Secure backup.** Encrypted backup only; user-controlled passphrase; never upload plaintext seed.

## Platform Notes

### Android
- Use Android Keystore / StrongBox where available.
- Disable insecure backups for sensitive wallet data (`allowBackup=false` in foundation manifest).
- Use biometric/PIN gate for unlock/signing where appropriate.

### iOS
- Use Keychain with appropriate accessibility class; Secure Enclave for supported key operations.
- Protect backups; exclude secrets from iCloud backup unless explicitly encrypted by user flow.

## Files

- `mobile/android/README.md`
- `mobile/android/app/build.gradle.kts`
- `mobile/android/app/src/main/AndroidManifest.xml`
- `mobile/android/app/src/main/java/org/phantomx/wallet/MainActivity.kt`
- `mobile/ios/README.md`
- `mobile/ios/PhantomXWallet/PhantomXWalletApp.swift`
- `docs/MOBILE_WALLET.md`
