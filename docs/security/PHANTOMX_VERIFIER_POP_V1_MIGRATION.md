# PHANTOMX Verifier PoP V1 Migration Policy

**Status:** DRAFT / BLOCKED for protocol activation decision  
**Phase:** 15C.4

## Scope

Migration from legacy string-based verifier registration PoP payload to canonical binary PoP v1 (`PHXVERPOP\x01`).

## Current Behavior (implemented)

- `verifier_register_challenge` returns `payload_hex` (binary v1), `format_version=1`, `protocol_version=1`, `operation=register`, `nonce_hex`, timestamps, and context fields.
- `verifier_register` requires `challenge_id` and `pop_sig` over exact `payload_hex` bytes.
- Server decodes binary v1 strictly: magic/version/network/protocol/operation/field lengths/endianness/timestamp order; rejects trailing bytes and malformed encodings.
- Legacy string payload format `PHX/verifier/register/v1|...` is **not accepted** by v1 decoder.

## Compatibility

| Case | Behavior | Status |
|---|---|---|
| Old client -> new server | BREAKS: old client signs string payload / expects `payload` field | Expected; migration required |
| New client -> old server | BREAKS: old server does not issue/verify binary v1 | Expected |
| Mixed-version nodes | Unsafe for registry consistency; registry is local-only today | BLOCKED |
| Old challenge consumed by new server | Only if stored as binary v1 record; legacy string records not issued by new code | N/A for new challenges |
| Rollback new->old | Old code cannot verify v1 challenges; may cause registration outage; do not rollback silently after v1 issuance | Risk |

## Security Rules

- **No silent dual-format acceptance.** Accepting legacy string payload alongside v1 creates downgrade/ambiguity risk unless explicitly versioned, domain-separated, time-bounded, and governance-approved.
- **No unsigned fallback.** Registration without valid PoP is rejected.
- **No replay across formats/versions.** Challenge IDs are random 32-byte values; payload domain magic includes version. Any future v2 must use distinct magic/version/domain.

## Required Decisions (BLOCKED)

1. Activation point (testnet/regtest/mainnet) and announcement window.
2. Whether a time-bounded dual-accept transition is allowed; if yes, explicit cutoff height/time and mandatory telemetry/audit.
3. Client/SDK release and minimum supported version.
4. Handling of pre-existing local verifier records created before PoP v1 - migration or re-registration policy.
5. Rollback policy and database compatibility guarantees.

## Recommendation

- Keep **v1-only strict** acceptance in code.
- Provide migration documentation and client updates before any public testnet activation requiring PoP v1.
- Do not enable dual-format without explicit governance/protocol approval and downgrade protections.
