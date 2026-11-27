#!/usr/bin/env bash
set -euo pipefail
# Idempotent bootstrap. Review before running as root.
apt-get update
apt-get install -y ca-certificates curl git build-essential cmake pkg-config libssl-dev libsodium-dev sqlite3 nginx ufw jq
id -u phantomx >/dev/null 2>&1 || useradd --system --create-home --shell /usr/sbin/nologin phantomx
install -d -o phantomx -g phantomx /var/lib/phantomx /var/lib/phantomx-pool /var/log/phantomx /etc/phantomx
echo "bootstrap complete"
