#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="${PHANTOMX_TESTNET_DATADIR:-$ROOT/.phantomx-testnet}"
PID="$DATA_DIR/phantomxd.pid"
if [[ ! -f "$PID" ]]; then echo "no pid file: $PID"; exit 0; fi
pid=$(cat "$PID")
kill "$pid" 2>/dev/null || true
for _ in $(seq 1 50); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
kill -9 "$pid" 2>/dev/null || true
rm -f "$PID"
echo "stopped testnet node pid=$pid"
