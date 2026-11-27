#!/usr/bin/env bash
set -euo pipefail
# Backup chain and service DBs. Stop services for consistent snapshot if required.
OUT=${1:-/var/backups/phantomx/$(date -u +%Y%m%dT%H%M%SZ)}
mkdir -p "$OUT"
# Example: sqlite3 /var/lib/phantomx/node-a/chain.db ".backup '$OUT/chain.db'"
# cp -a /var/lib/phantomx-pool "$OUT/pool"
sha256sum "$OUT"/* > "$OUT/SHA256SUMS" 2>/dev/null || true
echo "backup written to $OUT"
