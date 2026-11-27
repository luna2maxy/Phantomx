# PhantomX Two-VPS Testnet Deployment

**Status: DEPLOYED AND TESTNET GO AUTHORIZED (owner, 2026-11-27) — with documented residual gaps. Public exposure remains NOT AUTHORIZED.**

This directory contains deployment artifacts for a two-VPS PhantomX testnet topology. Deployment was executed by ops and validated in the owner-authorized TESTNET GO record (`docs/reports/PHANTOMX_TESTNET_READINESS_EXECUTION_REPORT.md`, section Y). It is **not** a claim of consensus safety, independent validator failure domains, or a completed 14-28 day soak test; the approved 30-minute soak and accepted residual gaps are documented in that report.

## Execution evidence (2026-11-27)
- 4 validators running; connections=3 and ready=3 on all nodes.
- BFT height=3, locked=true; finalized_height=2 with an identical hash on all nodes.
- No errors, equivocations, or slashed state; DB integrity ok; backups created with identical `chain.db` hashes.
- 30-minute soak: 60 samples, 0 RPC errors, 0 hash divergence, 0 height divergence.
- Monitoring: 56 samples, 0 critical, 0 warning.
- Owner-accepted residual gaps: binary hash mismatch from a different build environment, P2P exposed on `0.0.0.0:18750-18753` with ufw `ALLOW Anywhere`, unit tests skipped on VPS, 30-minute soak only, and automated (not human) independent review.

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
A 4-validator BFT set on 2 physical VPS creates 2 failure domains and cannot be claimed as independent BFT fault domains. Quorum/finality rules MUST NOT be weakened. The owner accepted this topology for the initial isolated testnet with documented limitations; public exposure requires a separate decision and OS-level isolation proof.

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
