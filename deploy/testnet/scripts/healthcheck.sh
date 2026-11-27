#!/usr/bin/env bash
set -euo pipefail
RPC_URL=${RPC_URL:-http://127.0.0.1:18740}
# POI Pool is OUT OF SCOPE for the initial isolated blockchain testnet.
# Enable pool healthcheck only when a secured pool phase is explicitly approved.
POOL_ENABLED=${POOL_ENABLED:-0}
POOL_URL=${POOL_URL:-http://127.0.0.1:8751/health}

check() { local name=$1 url=$2; if curl -fsS --max-time 5 "$url" >/dev/null; then echo "OK $name"; else echo "FAIL $name $url"; return 1; fi }

fail=0
if [ "$POOL_ENABLED" = "1" ]; then
  check pool "$POOL_URL" || fail=1
else
  echo "SKIP pool (OUT OF SCOPE for initial testnet)"
fi
# RPC check: adjust to actual method; do not expose token in logs
curl -fsS --max-time 5 -H 'Content-Type: application/json' -d '{"jsonrpc":"2.0","id":1,"method":"getstatus","params":{}}' "$RPC_URL" >/dev/null && echo "OK rpc" || { echo "WARN rpc check failed (method/auth may differ)"; fail=1; }
exit $fail
