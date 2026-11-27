#!/usr/bin/env bash
set -euo pipefail

# Reproducible local adversarial BFT cluster run.
# Not independent third-party evidence; it is executable project evidence.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PHANTOMX_BUILD_DIR:-$ROOT/build-ubuntu}"
CATCH_DIR="${E2E_CATCH_DIR:-/tmp/phantomx-work/e2e-catch}"
ITERATIONS="${ADVERSARIAL_ITERATIONS:-5}"
LABEL_PREFIX="${ADVERSARIAL_LABEL_PREFIX:-advbft}"
mkdir -p "$CATCH_DIR"

pass=0
fail=0
for i in $(seq 1 "$ITERATIONS"); do
  label="${LABEL_PREFIX}-${i}"
  echo "[adversarial] iteration $i/$ITERATIONS label=$label"
  if env PHANTOMX_BUILD_DIR="$BUILD_DIR" E2E_CATCH_DIR="$CATCH_DIR" E2E_RUN_LABEL="$label" E2E_VIEW_CHANGE=1 scripts/e2e-bft-4node.sh; then
    pass=$((pass+1))
  else
    fail=$((fail+1))
    echo "[adversarial] FAIL iteration $i label=$label"
  fi
  label="${LABEL_PREFIX}-hard-${i}"
  echo "[adversarial] hard iteration $i/$ITERATIONS label=$label"
  if env PHANTOMX_BUILD_DIR="$BUILD_DIR" E2E_CATCH_DIR="$CATCH_DIR" E2E_RUN_LABEL="$label" E2E_VIEW_CHANGE=1 E2E_VIEW_CHANGE_KILL_PROPOSER=1 scripts/e2e-bft-4node.sh; then
    pass=$((pass+1))
  else
    fail=$((fail+1))
    echo "[adversarial] FAIL hard iteration $i label=$label"
  fi
done

echo "[adversarial] pass=$pass fail=$fail total=$((pass+fail))"
test "$fail" -eq 0
