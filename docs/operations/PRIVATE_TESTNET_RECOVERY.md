# PHANTOMX — Private Testnet Recovery

**Scope:** Isolated private testnet, 4 validators, localhost RPC.

## Single node restart
1. Stop node: `kill <pid>` (graceful).
2. Verify process exited: `kill -0 <pid>` (should fail).
3. Start node with same config and data directory.
4. Verify catch-up: `getnetworkinfo` → peers=3/ready=3; `bft_status` → height converges within 60s.
5. Verify finalized height/hash matches majority.

## All nodes restart (full recovery)
1. Stop all nodes.
2. Verify all stopped.
3. Start all 4 nodes.
4. Verify peer mesh: each node peers=3/ready=3.
5. Verify block production resumes.
6. Verify finality converges.

## Database recovery
1. Stop node.
2. Run `PRAGMA integrity_check` (read-only).
3. If integrity OK: restart node; SQLite WAL recovery is automatic.
4. If integrity FAIL: restore from backup (`deploy/testnet/restore/restore.sh`).
5. Verify restored DB integrity.
6. Restart node; verify catch-up.

## Chain state inconsistency
1. Stop node.
2. Run `verify_database` (if available) or manual block continuity check.
3. If inconsistent: restore from snapshot/backup.
4. Restart; verify height/hash match majority.

## Backup
```bash
deploy/testnet/backup/backup.sh
```
Store backups outside the data directory. Verify backup integrity with `PRAGMA integrity_check` on the backup file.

## Rollback
1. Stop all nodes.
2. Restore from known-good backup.
3. Verify integrity on all restored DBs.
4. Start all nodes.
5. Verify peer mesh, block production, finality.

## Evidence preservation
- Preserve all logs, DB files, and RPC outputs on failure.
- Do not delete or overwrite evidence.
- Use unique evidence directories with timestamps.