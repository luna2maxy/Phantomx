# PhantomX Database

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


## Scope

This document describes the foundation blockchain storage for PhantomX: block store, block index, UTXO state, chainstate, undo data, transaction index, epoch metadata, and snapshot metadata.

## Database Choice

| Option | Durability/Recovery | Performance | License | Ops/Embedding | Decision |
|---|---|---|---|---|---|
| SQLite3 | Excellent WAL + FULL sync, crash recovery, single-file backup | Good for foundation; sufficient for block/index/chainstate | Public domain | Easy embed, cross-platform, tools available | **Selected for foundation** |
| RocksDB | Good with WAL; more tuning needed | Very high for large KV workloads | Apache-2.0/GPLv2 dual (check build) | Heavier dependency, tuning complexity | Future candidate for high-throughput UTXO/index |
| LMDB | Strong mmap durability model | Fast reads, careful write patterns | OpenLDAP Public License | Small, embedded | Candidate for read-heavy indexes |

**Rationale:** SQLite3 provides audited, widely deployed durability/crash recovery, simple backup/restore, integrity checking (`PRAGMA integrity_check`), and low operational complexity for the current foundation stage. It can be replaced behind storage interfaces if scale requires specialized KV stores.

## Schema

SQLite database (WAL mode, `synchronous=FULL`, `foreign_keys=ON`):

- `metadata(key PRIMARY KEY, value)` — chain tip, network, finalized height, schema/version markers.
- `blocks(hash PRIMARY KEY, height, prev_hash, timestamp, header_blob, block_blob, active)` — block store + index fields.
- `utxos(txid, vout, amount, coinbase, height, script_pubkey, PRIMARY KEY(txid,vout))` — current chainstate UTXO set.
- `tx_index(txid PRIMARY KEY, block_hash, tx_index)` — transaction location index.
- `undo(block_hash PRIMARY KEY, undo_blob)` — spent UTXO undo data for block disconnect.
- `snapshots(id PRIMARY KEY, tip_hash, tip_height, created_at, path)` — snapshot metadata.

## Features

| Feature | API | Notes |
|---|---|---|
| Write block | `ChainDatabase::write_block` | Stores header+block blob; idempotent by hash |
| Read block | `ChainDatabase::read_block` | Deserializes stored block blob |
| Validate block | `ChainDatabase::validate_block` | Structural checks, coinbase-first, merkle root, basic tx validation, contextual tx validation against current UTXO set |
| Apply block | `ChainDatabase::apply_block` | Validates, writes, updates UTXOs/tx index/undo, advances tip in SQLite transaction |
| Disconnect block | `ChainDatabase::disconnect_block` | Restores undo, removes outputs, rewinds tip; refuses finalized blocks unless explicitly allowed |
| Recover after crash | `ChainDatabase::recover_after_crash` | Runs integrity check and validates tip consistency on open |
| Verify database | `ChainDatabase::verify_database` | Integrity check + active chain continuity + block hash verification |
| Backup | `ChainDatabase::backup` | SQLite online backup API |
| Restore | `ChainDatabase::restore` | Verifies backup, replaces DB file, reopens and recovers |
| Snapshot verification | `ChainDatabase::create_snapshot`, `verify_snapshot` | Snapshot is backup with metadata; verify checks integrity |

## Finality & Disconnect

Disconnect is allowed only for the current tip. If `metadata.finalized_height` is set, disconnect is refused at or below finalized height unless `allow_finalized=true` is explicitly passed by a privileged recovery path. Normal reorg handling must respect PoI+Verifier finality.

## Recovery & Consistency

- SQLite WAL + `synchronous=FULL` provides atomic commit durability.
- `open()` initializes schema, runs recovery, and applies genesis if needed.
- `recover_after_crash()` performs `PRAGMA integrity_check` and confirms tip points to an active indexed block.
- `verify_database()` walks active chain from tip to genesis, checking hashes and continuity.

## Limitations (Foundation)

- Contextual validation loads full UTXO set; optimize with indexed/iterative validation for production.
- Same-block spend handling in validation/undo build is limited; block application should be extended to in-memory overlay for complex blocks.
- Block validation currently does not enforce full PoI+Verifier consensus commitments, coinbase reward schedule, or finality certificates; those are added in consensus stages.
- Snapshot verification currently checks SQLite integrity; production should also verify snapshot manifest, tip hash, and chainstate commitments.

## Files

- `include/phantomx/database/sqlite_db.hpp`
- `src/database/sqlite_db.cpp`
- `include/phantomx/blockchain/chain.hpp`
- `src/blockchain/chain.cpp`
- `tests/unit/test_database.cpp`
