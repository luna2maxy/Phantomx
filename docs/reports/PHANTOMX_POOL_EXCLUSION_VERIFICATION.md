# PHANTOMX — POI Pool Exclusion Verification (TB-022)

**Date:** 2026-11-26  
**Status:** Verification of owner-approved exclusion for initial isolated testnet.  
**Related:** `docs/reports/PHANTOMX_POOL_REMEDIATION_STATUS.md`, `docs/reports/PHANTOMX_POOL_INITIAL_TESTNET_EXCLUSION_PROPOSAL.md`.

## Verification findings

- `deploy/testnet/systemd/phantomx-pool.service` exists but is a DRAFT unit; `deploy.sh` line 24 has pool enable commented out (`# systemctl enable --now phantomx-pool`).
- `deploy/testnet/nginx/pool.conf.example` exists as an example only.
- `deploy/testnet/scripts/smoke_two_vps.sh` already states "pool disabled until secured".
- `deploy/testnet/scripts/healthcheck.sh` references `POOL_URL` (would fail if pool not running); recommend removing pool from initial-testnet healthcheck or gating behind a flag.
- `deploy/testnet/README.md` mentions pool as a service; initial testnet runbook must explicitly state "POOL OUT OF SCOPE / NOT EXPOSED".
- No pool binary is required for blockchain node startup; `phantomx-pool` is separate.

## Exclusion boundary (initial testnet)

- No Pool daemon.
- No public Pool RPC.
- No nginx Pool route.
- No Pool systemd service enabled.
- No public Pool endpoint.
- No Pool dependency in initial blockchain testnet startup.
- PA-001..PA-011 remain OPEN for a future separately approved Pool phase.

## Recommendation

Document the exclusion explicitly in runbooks and deployment docs; optionally remove/gate the pool healthcheck entry for the initial testnet profile. Do not implement or deploy POI Pool. TB-022 remains OPEN for hardening; exclusion is verified for initial testnet.