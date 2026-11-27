#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${PHANTOMX_BUILD_DIR:-$ROOT/build-verified}/bin"
DATA="$ROOT/.regtest"
ok=0; fail=0
for i in 1 2 3 4; do
  PORT=$((8739 + i))
  DIR="$DATA/node$i"
  TOKEN=""
  if [ -f "$DIR/rpc.token" ]; then TOKEN="$(cat "$DIR/rpc.token")"; fi
  echo "== node$i :$PORT =="
  if out=$("$BIN/phantomx-cli" --rpc "http://127.0.0.1:$PORT" --rpc-token "$TOKEN" getblockcount 2>&1); then
    echo "blockcount: $out"; ok=$((ok+1))
  else
    echo "FAIL: $out"; fail=$((fail+1))
  fi
  "$BIN/phantomx-cli" --rpc "http://127.0.0.1:$PORT" --rpc-token "$TOKEN" getconnectioncount 2>&1 || true
done
echo "summary: ok=$ok fail=$fail"
[ "$fail" -eq 0 ]
