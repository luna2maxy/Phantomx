#!/usr/bin/env bash
set -euo pipefail
# Run from an operator machine with access to both VPS. NOT executed in this audit.
A=${VPS_A_HOST:?}
B=${VPS_B_HOST:?}
for H in "$A" "$B"; do
  echo "== $H =="
  ssh "$H" 'systemctl is-active phantomxd@node-a || systemctl is-active phantomxd@node-b || true'
done
echo "Manual checks required: P2P peers, chain height match, RPC not public, pool disabled until secured"
