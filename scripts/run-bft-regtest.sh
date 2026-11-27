#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build-ubuntu/bin/phantomxd"
BASE="${BFT_BASE:-/tmp/phx-bft}"
TOKEN="${PHANTOMX_RPC_TOKEN:-local-dev-token}"
mkdir -p "$BASE"

if [[ ! -x "$BIN" ]]; then
  echo "phantomxd not found at $BIN; build first: cmake --build build-ubuntu --target phantomxd -j4" >&2
  exit 1
fi

pkill -f "$BASE/n[1-4]" || true
sleep 1

start_node() {
  local idx=$1 rpc=$2 p2p=$3 peers=$4
  local dir="$BASE/n$idx"
  mkdir -p "$dir"
  nohup "$BIN" \
    --config "$ROOT/config/regtest.json" \
    --regtest \
    --node-index "$idx" \
    --datadir "$dir" \
    --rpc-host 127.0.0.1 --rpc-port "$rpc" \
    --p2p-host 127.0.0.1 --p2p-port "$p2p" \
    --connect "$peers" \
    --rpc-token "$TOKEN" \
    --rpc-allowip 127.0.0.1 \
    > "$dir/node.log" 2>&1 &
  echo $! > "$dir/pid"
  echo "node$idx pid=$(cat "$dir/pid") rpc=$rpc p2p=$p2p"
}

# 4-node full mesh
start_node 1 18741 28441 "127.0.0.1:28442,127.0.0.1:28443,127.0.0.1:28444"
start_node 2 18742 28442 "127.0.0.1:28441,127.0.0.1:28443,127.0.0.1:28444"
start_node 3 18743 28443 "127.0.0.1:28441,127.0.0.1:28442,127.0.0.1:28444"
start_node 4 18744 28444 "127.0.0.1:28441,127.0.0.1:28442,127.0.0.1:28443"

echo "Waiting for RPC..."
for i in $(seq 1 30); do
  ok=0
  for p in 18741 18742 18743 18744; do
    curl -s http://127.0.0.1:$p -H 'Content-Type: application/json' -d '{"id":"1","method":"getblockcount","params":{},"token":"'"$TOKEN"'"}' >/dev/null && ok=$((ok+1)) || true
  done
  if [[ $ok -eq 4 ]]; then echo "All 4 nodes RPC ready"; break; fi
  sleep 1
done

cat <<EOF
BFT regtest cluster started.
RPC: 18741 18742 18743 18744
P2P: 28441 28442 28443 28444
Logs: $BASE/n{1..4}/node.log

Propose a block (try all nodes; only selected proposer will accept):
  for p in 18741 18742 18743 18744; do curl -s http://127.0.0.1:\$p -H 'Content-Type: application/json' -d '{"id":"1","method":"bft_propose","params":{},"token":"$TOKEN"}'; echo; done

Stop cluster:
  pkill -f "$BASE/n[1-4]"
EOF
