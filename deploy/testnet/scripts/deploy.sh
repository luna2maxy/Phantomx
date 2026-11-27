#!/usr/bin/env bash
set -euo pipefail
# Idempotent deployment. Does NOT delete chain/db data.
REPO_DIR=${REPO_DIR:-/opt/phantomx}
ENV_FILE=${ENV_FILE:-/etc/phantomx/node.env}

if [[ ! -f "$ENV_FILE" ]]; then echo "missing $ENV_FILE"; exit 1; fi
set -a; source "$ENV_FILE"; set +a

install -d -o phantomx -g phantomx "$PHANTOMX_DATADIR" /var/lib/phantomx-pool /var/log/phantomx
cd "$REPO_DIR"

# Build only if requested
if [[ "${BUILD:-0}" == "1" ]]; then
  cmake -S . -B build-ubuntu -DCMAKE_BUILD_TYPE=Release
  cmake --build build-ubuntu -j"$(nproc)"
fi

# Install service files (templates must be reviewed/instantiated first)
# cp deploy/testnet/systemd/*.service /etc/systemd/system/
# systemctl daemon-reload
# systemctl enable --now phantomxd@node-a
# systemctl enable --now phantomx-pool

echo "deploy script completed (review commented install steps before enabling services)"
