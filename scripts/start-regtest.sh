#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${PHANTOMX_BUILD_DIR:-$ROOT/build-verified}/bin"
DATA="$ROOT/.regtest"
mkdir -p "$DATA"
if [ ! -x "$BIN/phantomxd" ]; then echo "missing $BIN/phantomxd; build first"; exit 1; fi

for i in 1 2 3 4; do
  PORT=$((8739 + i))
  P2P=$((18439 + i))
  DIR="$DATA/node$i"
  mkdir -p "$DIR"
  if [ -f "$DIR/phantomxd.pid" ] && kill -0 "$(cat "$DIR/phantomxd.pid")" 2>/dev/null; then
    echo "node$i already running (pid=$(cat "$DIR/phantomxd.pid"))"
    continue
  fi
  CONNECT=""
  if [ "$i" -ne 1 ]; then CONNECT="--connect 127.0.0.1:18440"; fi
  "$BIN/phantomxd" --datadir "$DIR" --rpc-host 127.0.0.1 --rpc-port "$PORT" --rpc-role admin --p2p-host 127.0.0.1 --p2p-port "$P2P" $CONNECT > "$DIR/daemon.log" 2>&1 &
  echo $! > "$DIR/phantomxd.pid"
  echo "started node$i rpc=127.0.0.1:$PORT p2p=127.0.0.1:$P2P pid=$!"
done

# Wait for RPC readiness
for i in 1 2 3 4; do
  PORT=$((8739 + i))
  DIR="$DATA/node$i"
  for _ in $(seq 1 30); do
    if [ -f "$DIR/rpc.token" ]; then
      TOKEN="$(cat "$DIR/rpc.token")"
      if "$BIN/phantomx-cli" --rpc "http://127.0.0.1:$PORT" --rpc-token "$TOKEN" getblockcount >/dev/null 2>&1; then
        echo "node$i RPC ready"
        break
      fi
    fi
    sleep 0.2
  done
done

echo "regtest 4-node processes started with P2P connections (star to node1). NOTE: P2P transport/connectivity wired; BFT vote/QC consensus messages over P2P and full multi-node BFT finality still require integration. Use scripts/check-regtest.sh"
