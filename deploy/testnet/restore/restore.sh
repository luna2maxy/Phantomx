#!/usr/bin/env bash
set -euo pipefail
# Restore requires explicit confirmation and stopped services.
SRC=${1:?backup dir required}
[[ -d "$SRC" ]] || { echo "missing $SRC"; exit 1; }
echo "STOP services before restore. Verify checksums: (cd $SRC && sha256sum -c SHA256SUMS)"
echo "Restore is manual by design to avoid accidental data loss."
