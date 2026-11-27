# PHANTOMX — Private Testnet Troubleshooting

**Scope:** Isolated private testnet, 4 validators, localhost RPC.

## Common issues

### Node won't start
- Check config: `--testnet` flag, valid `--config`, data directory writable.
- Check port conflicts: `ss -tlnp | grep 1874`.
- Check key file: exists, readable, correct permissions (600).
- Check logs: `n*.log` for errors.

### No peer connectivity
- Verify `--connect` lists all 3 peers with correct ports.
- Check network identity: logs should show `p2p_network_identity` with testnet magic `0x54505801`.
- Check firewall: localhost only; no blocking rules.
- Verify `ready_peer_count` via `getnetworkinfo`.

### No block production
- Check validator key is registered in BFT validator set.
- Check BFT status: `bft_status` RPC.
- Verify quorum: 3/4 validators must be online.
- Check logs for proposal/vote errors.

### No finality
- Check attestation count vs quorum requirement.
- Verify all 4 validators are online and synced.
- Check `finalized_height`/`finalized_hash` consistency.

### Database integrity failure
- Stop node immediately.
- Run `PRAGMA integrity_check` (read-only, node stopped).
- Restore from backup if needed.
- Escalate per TB-001 stop conditions.

### High resource usage
- Check connection count (max 64).
- Check rate limit (500 msg/s per peer).
- Check DB size and WAL files.
- Review logs for spam/ban events.

## Escalation
- Conflicting finalized hash → critical incident.
- Database integrity failure → stop node, preserve evidence.
- Unauthorized peer → stop node, investigate.