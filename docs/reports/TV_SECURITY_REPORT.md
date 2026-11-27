# PhantomX Two-VPS Testnet Security Report (DRAFT)

## Status
**NOT READY / DOCUMENTED LIMITATIONS**. Draft based on repository audit and deployment templates; no live VPS hardening verification.

## Public attack surface (intended)
- 80/443: nginx for Pool API (only when explicitly approved).
- 18742/tcp: P2P, preferably restricted to peer VPS IP.

## Must remain private
- 18740 RPC admin: localhost/private only, token + allowip + firewall deny.
- All databases (SQLite files), indexer DB, pool DB.
- SSH: key-based auth only, no password, restricted source IPs where possible.

## Controls provided as templates
- Nginx TLS/security headers/request size/rate-limit zones (`deploy/testnet/nginx/*`).
- UFW baseline (`deploy/testnet/firewall/ufw.sh`).
- systemd hardening: NoNewPrivileges, PrivateTmp, ProtectSystem=strict, ProtectHome, ReadWritePaths.
- Separate service users/datadirs/logs.

## Known gaps / risks
1. **POI Pool app is stub** (`apps/phantomx-pool/main.cpp`) - no auth, replay protection, evidence validation, accounting, audit log. Do not expose publicly.
2. **BFT topology limitation** - 2 VPS cannot provide 4 independent validator failure domains; do not claim BFT resilience.
3. **SQLITE_BUSY_RECOVERY 5/261** reproduced; cause unproven - risk to reliability under concurrency.
4. **Public web backend** (if added later) requires review for auth on any admin/debug endpoints, CORS tightening, WS auth, rate limits, and indexer reorg consistency.
5. **Secrets management** - templates only; real deployment needs secret manager/vault/host-protected env files.
6. **No live security scan/pentest** performed.

## Required before public exposure
- Close RPC/DB/SSH password auth externally.
- TLS certs + HSTS + tested renewal.
- Pool implementation/security review or keep pool disabled.
- External port scan confirms only intended ports open.
- Log/audit/alert pipeline active.
