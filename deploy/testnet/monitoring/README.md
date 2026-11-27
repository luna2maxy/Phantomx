# Monitoring (DRAFT)

## Required checks
- Node process up (systemd), RPC responsive on localhost, P2P peer count > 0, chain height advancing or synced.
- Backend `/health`, indexer lag, HTTP 5xx rate, WS connections (when applicable).
- Pool `/health` and evidence accept/reject counters only after pool app is implemented/secured.
- Host: CPU, RAM, disk, disk I/O, network, open file descriptors.

## Suggested stack
- Prometheus node_exporter + blackbox_exporter + app metrics endpoint (if implemented).
- Grafana dashboards per VPS and per service.
- Alertmanager for: service down, disk >80%, peer count 0, chain stalled, RPC errors, indexer lag, high 5xx.

## Log collection
- journald persistent storage, logrotate for app logs.
- Centralized log shipping optional; keep secrets redacted.
