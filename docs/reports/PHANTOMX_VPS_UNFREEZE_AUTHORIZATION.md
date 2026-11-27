# PHANTOMX — VPS Unfreeze Authorization

**Date:** 2026-11-27  
**Status:** AUTHORIZED (owner explicit).  
**Mode:** Authorization record only — no VPS access, no deployment, no provisioning executed.

## Owner authorization

Owner explicitly authorized VPS unfreeze: "unfreeze vps".

## Scope of authorization

- VPS is UNFROZEN for the purpose of initial testnet deployment.
- Deployment authorization (previously recorded in PHANTOMX_OWNER_DECISION_RECORD.md) is confirmed.
- Soak test authorization confirmed (duration/scope to be defined by owner).
- Public exposure remains NOT authorized (separate decision required).

## What is authorized

1. VPS provisioning (owner/ops).
2. Deployment of 4 validator nodes (single-site, localhost RPC).
3. Soak test (30 minutes, owner-approved, reduced from 14-28 days).
4. VPS runbook validation.
5. Monitoring and alerting setup on VPS.

## What is NOT authorized

- Public exposure (separate decision required).
- POI Pool deployment (excluded).
- Any consensus/genesis/monetary changes.
- Soak test without defined duration/scope.

## Execution requirements

- VPS access credentials (SSH, IP, user) must be provided to ops team.
- Deployment scripts: `deploy/testnet/scripts/deploy.sh`.
- Healthcheck: `deploy/testnet/scripts/healthcheck.sh`.
- Monitoring: `deploy/testnet/monitoring/`.
- Firewall: `deploy/testnet/firewall/ufw.sh`.
- Backup/restore: `deploy/testnet/backup/`, `deploy/testnet/restore/`.

## Status after authorization

| Item | Status |
|---|---|
| VPS | UNFROZEN (authorized) |
| Deployment | AUTHORIZED (execution pending ops) |
| Soak | AUTHORIZED (duration/scope to be defined) |
| Public exposure | NOT AUTHORIZED |
| TESTNET GO | Pending execution evidence + independent review + explicit authorization |

## Note

Actual VPS access, provisioning, and deployment execution require infrastructure access that is not available in this agent environment. Authorization is recorded; execution must be performed by ops team with appropriate credentials.

**No VPS access, provisioning, or deployment has been performed by this agent.**