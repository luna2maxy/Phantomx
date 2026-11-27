# PhantomX Two-VPS Testnet Runbook (DRAFT)

## Status
**NOT VERIFIED ON LIVE VPS.** This runbook is an operational draft. Do not claim soak test completion without actual 14-28 day execution logs.

## 1. Preconditions
- Two VPS with static IPs, SSH key auth, Ubuntu/Debian compatible.
- DNS for `pool.example.com` (pool only after app security verified).
- Secrets generated outside git: RPC token, pool API token, WS auth token, TLS certs.
- Review `config/testnet.json` and consensus limitations in `TV_AUDIT_2026-11-26.md`.

## 2. Per-VPS bootstrap
```bash
sudo REPO_DIR=/opt/phantomx bash deploy/testnet/scripts/bootstrap_vps.sh
sudo install -d -o phantomx -g phantomx /opt/phantomx
# clone/copy repo to /opt/phantomx; ensure ownership
```

## 3. Configuration
- Copy `deploy/testnet/configs/node-a.env.example` or `node-b.env.example` to `/etc/phantomx/node-a.env` / `node-b.env`.
- Set `PHANTOMX_CONNECT` to peer VPS IP:18742.
- Set strong `PHANTOMX_RPC_TOKEN`; keep `PHANTOMX_RPC_HOST=127.0.0.1`, `PHANTOMX_RPC_ALLOWIP=127.0.0.1`.
- Copy `config/testnet.json` to `/etc/phantomx/testnet.json`.
- Pool env only if pool implementation is secured.

## 4. Build
```bash
cd /opt/phantomx
cmake -S . -B build-ubuntu -DCMAKE_BUILD_TYPE=Release
cmake --build build-ubuntu -j$(nproc)
```

## 5. Services
- Install reviewed systemd units from `deploy/testnet/systemd/`.
- `systemctl daemon-reload`
- Enable/start node. Do not enable public pool until security gates pass.

## 6. Firewall/TLS
- Apply `deploy/testnet/firewall/ufw.sh` after setting `PEER_VPS_IP`.
- Install certbot/certs and nginx configs from `deploy/testnet/nginx/*.example`.
- Confirm RPC 18740 is not publicly reachable.

## 7. Health checks
- `deploy/testnet/scripts/healthcheck.sh`
- Verify P2P peer count, chain height.
- Verify no public RPC/DB ports: external `nmap` or cloud firewall audit.

## 8. Backup/restore
- Use `deploy/testnet/backup/backup.sh` during low activity; prefer stopping write-heavy services for consistent SQLite snapshot.
- Restore is manual by design; validate checksums and test on non-production clone first.

## 9. Incident handling
- Node crash: check `journalctl -u phantomxd@*`, disk, DB integrity, restart; capture logs.
- Chain split: do not force manual DB edits; collect logs/hashes/heights from both VPS.
- SQLITE_BUSY_RECOVERY: capture logs, DB integrity, process list, concurrent workload; do not enable unbounded retries.
- Pool anomaly: disable public pool endpoint, preserve DB/logs, audit evidence/accounting.

## 10. 14-28 day soak test plan
- Days 1-3: deploy, connectivity, basic pool integration (when approved).
- Days 4-7: block/tx/evidence flow, sync observation.
- Days 8-14: controlled restarts/failures/network interruptions.
- Days 15-21: sustained workload, API load, indexing/resource observation.
- Days 22-28: backup/restore drill, upgrade test, security review, final report.
Record start/end UTC, uptime, block/tx/evidence counts, errors, incidents, resource utilization. Do not mark complete without actual logs.
