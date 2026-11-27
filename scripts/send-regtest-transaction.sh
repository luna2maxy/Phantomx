#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${PHANTOMX_BUILD_DIR:-$ROOT/build-verified}/bin"
DATA="$ROOT/.regtest"
PORT="${1:-8740}"
TOKEN=""
if [ -f "$DATA/node1/rpc.token" ]; then TOKEN="$(cat "$DATA/node1/rpc.token")"; fi
shift || true
echo "Usage: $0 [rpc-port] --from ... --to ... --amount-atoms ..." >&2
"$BIN/phantomx-cli" --rpc "http://127.0.0.1:$PORT" --rpc-token "$TOKEN" sendtoaddress "$@" 2>&1 || true
