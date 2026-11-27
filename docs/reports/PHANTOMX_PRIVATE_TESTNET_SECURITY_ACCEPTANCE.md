# PHANTOMX — Private Testnet Security Acceptance

**Date:** 2026-11-26  
**Scope:** Isolated private testnet, 4 validators, single-site, localhost RPC.

## TB-020 — Isolation
- Process/socket-level isolation: PROVEN (exec6, 3 trials, 0 cross-group sockets).
- OS-level (kernel firewall): NOT PROVEN (requires root; unavailable). Method documented.
- Private testnet scope: 4 validators, P2P authorized peers only, RPC localhost, no public endpoint, no internet exposure.
- TB-023 network identity: CLOSED (cross-network rejection verified).

## TB-021 — RPC/P2P
- RPC smoke 13/13 PASS. Dedup smoke PASS. PoI relay probe PASS. TB021 load 0 errors. Timeout/resource probe PASS. Adversarial probe PASS (6 frame types, no crash).
- AuthZ unit-tested (`rpc_authz_rate_limit_audit`).
- Common-path evidence accepted for private testnet (Option 2 approved).
- Deferred: full fuzzing, authZ-on-daemon matrix, controlled timeout/resource tests (mandatory before public exposure).

## TB-022 — POI Pool
- Excluded from initial testnet. No daemon, no public RPC, no nginx route, no systemd service, no public endpoint.
- `POOL_ENABLED` defaults to 0; healthcheck gates pool check.
- PA-001..PA-011 remain OPEN for future phase.

## Security boundaries
- RPC localhost only (no public RPC).
- P2P restricted to authorized peers in controlled private network.
- No public endpoints, no internet exposure.
- No POI Pool.
- Monitoring and stop conditions documented (TB-019 Option A).

## Verdict
Security acceptance: **PASS for isolated private testnet** (with documented limitations: OS-level isolation unproven, deferred fuzzing/authZ tests). Not sufficient for public exposure.