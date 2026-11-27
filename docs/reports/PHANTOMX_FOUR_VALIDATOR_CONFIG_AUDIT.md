# PHANTOMX — Four-Validator Config Audit (Phase 4)

**Date:** 2026-11-26  
**Mode:** Read-only audit. No config changes, no key generation.  
**Evidence:** `config/testnet-bft-canonical.json`, `config/testnet.json`, `config/testnet-bft-validation.json`, `/tmp/phx-pb001-op/v0..v3.key`, exec6/exec7b evidence.

## 1. Canonical config (testnet-bft-canonical.json)

| Property | Value | Status |
|---|---|---|
| Network | testnet | OK |
| BFT enabled | true | OK |
| Validator count | 4 | OK (matches approved topology) |
| Quorum rule | gt_2_3 (3/4) | OK (matches approved) |
| View change | enabled | OK |
| Key mode | operator | OK |
| Network magic | 0x54505801 | OK (matches code-level canonical after exec7b) |
| Genesis block hash | 98a75772... | OK (matches code-level canonical) |
| Default P2P port | 18742 | OK |
| Default RPC port | 18740 | OK |

## 2. Validator identities

| Index | Pubkey | Voting power | Status |
|---|---|---|---|
| 0 | 2bd8d899bd3f9662b1e46c4b7ad5c0e61cac98c56d1a6939b8d8e37dfdae6343 | 1 | Canonical (from exec evidence) |
| 1 | d49f722dea26012ef6fa7852418399d4430074f1018e669a07aff1cb63918dca | 1 | Canonical |
| 2 | 19d8de75966c7a644ca4745e937b9bbab51f2149ac741374d5099a4ce79386b1 | 1 | Canonical |
| 3 | 4454470a87f019db7a0e1040441613065e6d1ec7f96d0af9ee424491095f38c0 | 1 | Canonical |

- 4 distinct identities. ✓
- Equal voting power (1 each). ✓
- Quorum 3/4 (>2/3). ✓

## 3. Operator keys

- Keys exist at `/tmp/phx-pb001-op/v0..v3.key` (operator-managed, exec evidence).
- **Not promoted to deployment keys.** Owner approved: preserve as evidence; require custody/integrity/backup/recovery review before reuse. New deployment keys require secure generation.
- Private keys must NOT be stored in config or committed to Git.

## 4. Config variants

| Config | Purpose | BFT | Status |
|---|---|---|---|
| `config/testnet.json` | Legacy/root-owned | No (non-BFT) | Legacy, not canonical |
| `config/testnet-bft-canonical.json` | Canonical testnet PoI+BFT | Yes (4 validators, >2/3) | Template, owner-accepted (exec7b) |
| `config/testnet-bft-validation.json` | ISOLATED TEST-ONLY | Yes (4 validators) | Test-only, not for production |

## 5. Preflight checks (recommended, not yet implemented)

- Duplicate identity detection.
- Duplicate port detection.
- Incorrect genesis hash detection.
- Incorrect network magic detection.
- Invalid quorum configuration detection.
- Missing peer detection.
- Invalid data directory detection.
- Invalid key permissions detection.
- Non-localhost RPC detection.
- Unsafe public binding detection.

## 6. Gaps

1. **Canonical config not yet owner-accepted for deployment** (TB-005) — logical topology approved, deployment config pending.
2. **Operator keys not validated for deployment** — custody/integrity/backup/recovery review required.
3. **No automated preflight validation** — recommended but not implemented (scope: non-consensus config validation, auto-approvable).
4. **Single-site failure limitation** — documented as accepted risk (TB-005 Option 1).
5. **Network identity wiring** — verified (TB-023 closed); config magic matches code-level canonical.

## 7. Verdict

Config audit: **PASS (with gaps)**. Canonical config is well-formed and consistent with code-level identity. Deployment requires owner acceptance of topology/keys and preflight validation implementation. No consensus or genesis changes. No deployment authorized.