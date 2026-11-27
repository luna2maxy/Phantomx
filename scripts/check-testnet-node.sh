#!/usr/bin/env bash
set -euo pipefail
RPC_HOST="${PHANTOMX_RPC_HOST:-127.0.0.1}"
RPC_PORT="${PHANTOMX_RPC_PORT:-18740}"
RPC_TOKEN="${PHANTOMX_RPC_TOKEN:-local-dev-token}"
rpc() {
  local method=$1 params=${2:-'{}'}
  curl -sS -X POST "http://$RPC_HOST:$RPC_PORT" -H 'Content-Type: application/json' \
    -d "{\"id\":\"1\",\"method\":\"$method\",\"params\":$params,\"token\":\"$RPC_TOKEN\"}"
}
for m in getblockcount getnetworkinfo getblockchaininfo getfinalityinfo; do
  echo "== $m"
  rpc "$m"
  echo
done
