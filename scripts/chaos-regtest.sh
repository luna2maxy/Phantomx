#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${PHANTOMX_BUILD_DIR:-$ROOT/build-verified}/bin"
DATA="$ROOT/.regtest"
# Simple chaos: kill/restart random node, verify others still respond.
for round in 1 2 3; do
  i=$(( (RANDOM % 4) + 1 ))
  DIR="$DATA/node$i"; PORT=$((8739+i))
  if [ -f "$DIR/phantomxd.pid" ]; then
    pid=$(cat "$DIR/phantomxd.pid")
    echo "chaos round $round: killing node$i pid=$pid"
    kill "$pid" 2>/dev/null || true
    sleep 1
    "$BIN/phantomxd" --datadir "$DIR" --rpc-host 127.0.0.1 --rpc-port "$PORT" --rpc-role admin > "$DIR/daemon.log" 2>&1 &
    echo $! > "$DIR/phantomxd.pid"
    sleep 1
  fi
  for j in 1 2 3 4; do
    P=$((8739+j)); D="$DATA/node$j"; T=""
    [ -f "$D/rpc.token" ] && T="$(cat "$D/rpc.token")"
    "$BIN/phantomx-cli" --rpc "http://127.0.0.1:$P" --rpc-token "$T" getblockcount >/dev/null 2>&1 && echo "node$j ok" || echo "node$j down"
  done
done
echo "chaos run complete (process-level). Does not prove BFT consensus safety under partition."
