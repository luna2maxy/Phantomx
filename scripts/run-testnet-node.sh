#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PHANTOMX_BUILD_DIR:-$ROOT/build-ubuntu}"
BIN="$BUILD_DIR/bin/phantomxd"
DATA_DIR="${PHANTOMX_TESTNET_DATADIR:-$ROOT/.phantomx-testnet}"
CONFIG="${PHANTOMX_TESTNET_CONFIG:-$ROOT/config/testnet.json}"
RPC_HOST="${PHANTOMX_RPC_HOST:-127.0.0.1}"
RPC_PORT="${PHANTOMX_RPC_PORT:-18740}"
P2P_HOST="${PHANTOMX_P2P_HOST:-0.0.0.0}"
P2P_PORT="${PHANTOMX_P2P_PORT:-18742}"
RPC_TOKEN="${PHANTOMX_RPC_TOKEN:-local-dev-token}"
CONNECT="${PHANTOMX_CONNECT:-}"

if [[ ! -x "$BIN" ]]; then
  echo "phantomxd not found at $BIN" >&2
  echo "Build first: cmake --build build-ubuntu --target phantomxd -j4" >&2
  exit 1
fi
mkdir -p "$DATA_DIR"
LOG="$DATA_DIR/testnet.log"
PID="$DATA_DIR/phantomxd.pid"

if [[ -f "$PID" ]] && kill -0 "$(cat "$PID")" 2>/dev/null; then
  echo "testnet node already running pid=$(cat "$PID")"
  exit 0
fi

ARGS=(
  --testnet
  --config "$CONFIG"
  --datadir "$DATA_DIR"
  --rpc-host "$RPC_HOST"
  --rpc-port "$RPC_PORT"
  --rpc-token "$RPC_TOKEN"
  --rpc-allowip 127.0.0.1
  --p2p-host "$P2P_HOST"
  --p2p-port "$P2P_PORT"
)
if [[ -n "$CONNECT" ]]; then
  ARGS+=(--connect "$CONNECT")
fi

nohup "$BIN" "${ARGS[@]}" > "$LOG" 2>&1 &
echo $! > "$PID"
echo "started testnet node pid=$(cat "$PID") rpc=http://$RPC_HOST:$RPC_PORT p2p=$P2P_HOST:$P2P_PORT"
echo "log=$LOG"

for _ in $(seq 1 60); do
  if curl -sS -X POST "http://$RPC_HOST:$RPC_PORT" -H 'Content-Type: application/json' \
    -d "{\"id\":\"1\",\"method\":\"getblockcount\",\"params\":{},\"token\":\"$RPC_TOKEN\"}" | grep -q '"result"'; then
    echo "RPC ready"
    exit 0
  fi
  if ! kill -0 "$(cat "$PID")" 2>/dev/null; then
    echo "daemon exited early; see $LOG" >&2
    tail -n 60 "$LOG" >&2 || true
    exit 1
  fi
  sleep 0.5
done
echo "RPC not ready after 30s; see $LOG" >&2
tail -n 60 "$LOG" >&2 || true
exit 1
