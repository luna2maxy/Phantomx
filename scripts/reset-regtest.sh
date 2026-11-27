#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/scripts/stop-regtest.sh" || true
rm -rf "$ROOT/.regtest"
echo "regtest reset"
