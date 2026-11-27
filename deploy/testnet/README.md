# PhantomX Two-VPS Testnet Deployment

**Status: DRAFT / NOT VERIFIED FOR PRODUCTION TESTNET**

This directory contains deployment artifacts for a two-VPS PhantomX testnet topology. It does **not** claim consensus safety, validator independence, or soak-test completion.

## Topology constraint
- Exactly 2 VPS: `PHANTOMX-A`, `PHANTOMX-B`.
- Each VPS may run blockchain node instance(s) as separate systemd units.
- Public: no public web services in this directory. POI Pool is OUT OF SCOPE for the initial isolated blockchain testnet (no daemon, no RPC, no nginx route, no systemd service, no public endpoint).
- Private: blockchain RPC admin, databases, internal metrics.
- P2P: only required protocol ports.

## Canonical config decision (PB-001)
- **Canonical BFT testnet config:** `config/testnet-bft-canonical.json` (PB-001A-F approved direction). Contains explicit PoI+BFT, 4 validators, operator key mode, `quorum_rule=gt_2_3`.
- **Legacy/non-BFT:** `config/testnet.json` is root-owned and currently has no BFT section; it is **not** the canonical PB-001 BFT testnet config. Owner/repo decision needed to replace/rename/remove ambiguity.
- **Isolated test-only:** `config/testnet-bft-validation.json` remains ISOLATED TEST-ONLY / NOT CANONICAL TESTNET.
- **Do not treat two configs as canonical.** Deployment templates reference `testnet-bft-canonical.json`.

## Critical blocker
A 4-validator BFT set on 2 physical VPS creates 2 failure domains and cannot be claimed as independent BFT fault domains. Quorum/finality rules MUST NOT be weakened. Any testnet deployment under this topology is **READY WITH DOCUMENTED LIMITATIONS** at best and may be **BLOCKED** for consensus safety.

## Layout
- `configs/` - node/pool/nginx env templates
- `systemd/` - service units
- `nginx/` - reverse proxy/TLS templates
- `firewall/` - ufw/nftables templates
- `scripts/` - bootstrap/deploy/health/backup/restore
- `monitoring/` - healthcheck/metrics notes
- `tests/` - smoke/integration test scripts
- `runbook/` - see `../docs/reports/TV_RUNBOOK.md`

## Safety
- Scripts must be idempotent and must not delete chain/DB data on rerun.
- No secrets in git. Use env files outside repo or secret manager.
- Do not expose RPC admin publicly.
