#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA="$ROOT/.regtest"
for i in 1 2 3 4; do
  PIDF="$DATA/node$i/phantomxd.pid"
  if [ -f "$PIDF" ]; then
    PID="$(cat "$PIDF")"
    kill "$PID" 2>/dev/null || true
    rm -f "$PIDF"
    echo "stopped node$i pid=$PID"
  fi
done
