# PHANTOMX — POI Pool Initial Testnet Exclusion Proposal (TB-022)

**Date:** 2026-11-26  
**Status:** PROPOSAL — not owner acceptance.  
**Related:** `docs/reports/PHANTOMX_POOL_SAFETY_AUDIT.md`, `docs/reports/PHANTOMX_POOL_REMEDIATION_STATUS.md`.

## Recommendation (recommended)

Exclude the POI Pool from the initial isolated blockchain testnet. The pool is not required to validate core BFT/chain/RPC/P2P behavior for a private isolated testnet.

## If excluded (required boundary)

- No public/listening pool daemon.
- No pool RPC exposure beyond disabled/internal.
- No nginx/systemd pool deployment.
- No public pool endpoint.
- Runbook/docs explicitly state "POOL OUT OF SCOPE / NOT EXPOSED".
- PA-001..PA-011 remain OPEN for a separate future hardening phase.

## If included (alternative)

Must resolve PA-001..PA-011 to acceptance criteria: real daemon/API, authZ/authN, persistence/recovery, bounds/rate limits, evidence verification, audit logging, regression tests, security review. Requires owner scope/design decision and approved implementation phase.

## Pool status

NOT SAFE FOR PUBLIC EXPOSURE. No deployment performed. PA-004 partial (pool stoull guarded; other sites rely on dispatch try/catch).

## Owner decision required

Select excluded (recommended) or included-with-hardening. Until then pool remains non-deployed and non-exposed; TB-022 stays OPEN. Preparation of this proposal is not acceptance.