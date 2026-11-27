#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${PHANTOMX_BUILD_DIR:-$ROOT/build-verified}/bin"
WALLET_DIR="${1:-$ROOT/.regtest/wallet}"
mkdir -p "$WALLET_DIR"
"$BIN/phantomx-wallet" --datadir "$WALLET_DIR" --command create --passphrase "${PHANTOMX_WALLET_PASSPHRASE:-testpass}"
echo "wallet created at $WALLET_DIR"
