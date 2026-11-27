# PHANTOMX — Private Testnet Runbook

**Version:** 1.0 (draft, untested on VPS)  
**Scope:** Isolated private testnet, 4 validators, single-site, localhost RPC.  
**Status:** VPS FROZEN — runbook not VPS-validated.

## Prerequisites
- Build: `cmake --build build-ubuntu -j6` (EXIT=0).
- Binary SHA256: `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`.
- Config: `config/testnet-bft-canonical.json` (owner-accepted).
- 4 operator signing keys (securely generated, not committed).
- 4 separate data directories.

## Start 4 validators
```bash
for i in 0 1 2 3; do
  phantomxd --config config/testnet-bft-canonical.json --testnet \
    --datadir /var/lib/phantomx/node-$i \
    --rpc-host 127.0.0.1 --rpc-port $((18740+i)) \
    --p2p-host 127.0.0.1 --p2p-port $((18742+i)) \
    --connect "127.0.0.1:18743,127.0.0.1:18744,127.0.0.1:18745" \
    --bft-signing-key-file /etc/phantomx/validator-$i.key \
    --rpc-token "$RPC_TOKEN" --rpc-allowip 127.0.0.1 &
done
```
(Adjust --connect per node: each node connects to the other 3.)

## Verify peer connectivity
```bash
for p in 18740 18741 18742 18743; do
  curl -s http://127.0.0.1:$p -H "Content-Type: application/json" \
    -d '{"id":"1","method":"getnetworkinfo","params":{},"token":"'"$RPC_TOKEN"'"}'
done
```
Expected: peers=3/ready=3 on each node.

## Verify block production
```bash
for p in 18740 18741 18742 18743; do
  curl -s http://127.0.0.1:$p -H "Content-Type: application/json" \
    -d '{"id":"1","method":"bft_status","params":{},"token":"'"$RPC_TOKEN"'"}'
done
```
Expected: bft height increasing, locked=true.

## Verify finality
```bash
for p in 18740 18741 18742 18743; do
  curl -s http://127.0.0.1:$p -H "Content-Type: application/json" \
    -d '{"id":"1","method":"getfinality","params":{},"token":"'"$RPC_TOKEN"'"}'
done
```
Expected: finalized_height and finalized_hash identical across all nodes.

## Stop validator safely
```bash
kill <pid>  # graceful shutdown
# Wait for process exit; verify no dangling processes
```

## Restart validator
```bash
# Restart single node; verify catch-up within 60s
# Check: ready peers=3, finalized height converges
```

## Database integrity check
```bash
# Read-only SQLite check (do not run while node is writing)
sqlite3 /var/lib/phantomx/node-0/chain.db "PRAGMA integrity_check;"
```

## Backup
```bash
deploy/testnet/backup/backup.sh
```

## Rollback
- Stop all nodes.
- Restore from backup (deploy/testnet/restore/restore.sh).
- Verify integrity.
- Restart.

## Shutdown all nodes
```bash
pkill -f phantomxd  # or kill individual PIDs
```

## Limitations
- VPS deployment not authorized (FROZEN).
- Pool out of scope.
- Single-site failure not tolerated (accepted limitation).
- OS-level isolation not proven.