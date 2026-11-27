#!/usr/bin/env bash
set -euo pipefail
# Run on each VPS as root. Review before applying.
ufw default deny incoming
ufw default allow outgoing
ufw allow 22/tcp
ufw allow 80/tcp
ufw allow 443/tcp
# P2P - restrict to peer VPS IP if possible
ufw allow from ${PEER_VPS_IP:-0.0.0.0/0} to any port 18742 proto tcp
# Explicitly deny RPC admin/public DB ports if present
ufw deny 18740/tcp
ufw deny 5432/tcp
ufw deny 6379/tcp
ufw --force enable
ufw status verbose
