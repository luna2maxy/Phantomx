#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${PHANTOMX_BUILD_DIR:-$ROOT/build-verified}/bin"
DATA="$(mktemp -d "${TMPDIR:-/tmp}/phantomx-bft-XXXXXX")"
RUN_LABEL="${E2E_RUN_LABEL:-manual}"
CATCH_DIR="${E2E_CATCH_DIR:-/tmp/e2e-catch}"
FAIL_CLASS=""
export PHANTOMX_E2E_DATA="$DATA"
check_port_range() {
  local base=$1
  for i in 1 2 3 4; do
    if ss -ltn "sport = :$((base+i))" 2>/dev/null | grep -q LISTEN; then return 1; fi
  done
  return 0
}
# Random bases can collide with an ephemeral port or a lingering node; retry
# instead of failing the whole run on the first unlucky draw.
RPC_BASE=0
P2P_BASE=0
for _attempt in $(seq 1 50); do
  RPC_BASE=$((18000 + (RANDOM % 200) * 4))
  P2P_BASE=$((22000 + (RANDOM % 200) * 4))
  if check_port_range "$RPC_BASE" && check_port_range "$P2P_BASE"; then break; fi
  RPC_BASE=0
done
if [ "$RPC_BASE" -eq 0 ] || [ "$P2P_BASE" -eq 0 ]; then
  FAIL_CLASS="FAIL_STARTUP"
  echo "port range unavailable after retries" >&2
  exit 23
fi
SEED_VALUE="${SEED:-$RANDOM}"
printf 'ports rpc_base=%s p2p_base=%s seed=%s\n' "$RPC_BASE" "$P2P_BASE" "$SEED_VALUE" >&2

on_timeout() { FAIL_CLASS="FAIL_TIMEOUT"; exit 124; }
RPC1=$((RPC_BASE + 1))
P2P1=$((P2P_BASE + 1))
mkdir -p "$DATA"
rpc() {
  local port=$1 token=$2 method=$3 params=$4 response
  response=$(curl --fail-with-body -sS -X POST "http://127.0.0.1:$port" -H 'Content-Type: application/json' -d "{\"id\":\"1\",\"method\":\"$method\",\"token\":\"$token\",\"params\":$params}") || { echo "RPC transport failed: $method" >&2; return 1; }
  python3 -c 'import json,sys; x=json.load(sys.stdin); assert x.get("error") is None, x.get("error"); assert x.get("result") is not None, x' <<<"$response" >/dev/null || { echo "RPC error: $method: $response" >&2; return 1; }
  printf '%s' "$response"
}
# start 4 nodes
for i in 1 2 3 4; do
  PORT=$((RPC_BASE+i)); P2P=$((P2P_BASE+i)); DIR="$DATA/node$i"; mkdir -p "$DIR"
  CONNECT=""
  SEEDS=""
  for j in 1 2 3 4; do
    [ "$j" -le "$i" ] && continue
    SEED_PORT=$((P2P_BASE+j))
    if [ -z "$SEEDS" ]; then SEEDS="127.0.0.1:$SEED_PORT"; else SEEDS="$SEEDS,127.0.0.1:$SEED_PORT"; fi
  done
  [ -z "$SEEDS" ] || CONNECT="--connect $SEEDS"
  TOKEN="local-dev-token"
  echo "$TOKEN" > "$DIR/rpc.token"
  "$BIN/phantomxd" --config "$ROOT/config/regtest.json" --datadir "$DIR" --node-index "$i" --rpc-host 127.0.0.1 --rpc-port "$PORT" --rpc-token "$TOKEN" --p2p-host 127.0.0.1 --p2p-port "$P2P" $CONNECT > "$DIR/daemon.log" 2>&1 &
  echo $! > "$DIR/pid"
done
dump_failure() {
  local rc=${1:-1}
  [ -n "$FAIL_CLASS" ] || FAIL_CLASS="FAIL_STARTUP"
  local out="${CATCH_DIR}/fail_${RUN_LABEL}_$(date +%s%N)"
  mkdir -p "$out"
  for i in 1 2 3 4; do
    mkdir -p "$out/node$i"
    cp "$DATA/node$i/daemon.log" "$out/node$i/" 2>/dev/null || true
    cp "$DATA/node$i/rpc.token" "$out/node$i/" 2>/dev/null || true
    cp "$DATA/node$i/pid" "$out/node$i/" 2>/dev/null || true
    cp "$DATA/node$i"/*.json "$out/node$i/" 2>/dev/null || true
  done
  if [ "$FAIL_CLASS" = "FAIL_CONSENSUS" ]; then cp -a "$DATA"/. "$out/datadir-full" 2>/dev/null || true; fi
  { echo "class=$FAIL_CLASS"; echo "rc=$rc"; echo "rpc_base=$RPC_BASE p2p_base=$P2P_BASE"; date -Ins; } > "$out/metadata"
  ss -ltnp > "$out/ss-ltnp" 2>&1 || true
  pgrep -af phantomx > "$out/pgrep-phantomx" 2>&1 || true
  dmesg | tail -50 > "$out/dmesg-tail" 2>&1 || true
  for i in 1 2 3 4; do
    if [ -f "$DATA/node$i/pid" ]; then
      pid=$(cat "$DATA/node$i/pid")
      kill -0 "$pid" 2>/dev/null && echo "node$i pid=$pid alive" || echo "node$i pid=$pid exited"
      tail -n 120 "$DATA/node$i/daemon.log" > "$out/node${i}-tail.log" 2>/dev/null || true
    fi
  done
  echo "failure_class=$FAIL_CLASS artifacts=$out" >&2
}
cleanup() {
  local rc=$?
  local artifact_class="pass"
  [ "$rc" -eq 0 ] || artifact_class="fail"
  if [ "$rc" -ne 0 ]; then dump_failure "$rc"; else
    local out="${CATCH_DIR}/pass_${RUN_LABEL}_$(date +%s%N)"
    mkdir -p "$out"
    for i in 1 2 3 4; do
      mkdir -p "$out/node$i"
      cp "$DATA/node$i/daemon.log" "$out/node$i/" 2>/dev/null || true
      cp "$DATA/node$i/rpc.token" "$out/node$i/" 2>/dev/null || true
      cp "$DATA/node$i/pid" "$out/node$i/" 2>/dev/null || true
      cp "$DATA/node$i"/*.json "$out/node$i/" 2>/dev/null || true
    done
    { echo "class=PASS"; echo "rc=$rc"; echo "rpc_base=$RPC_BASE p2p_base=$P2P_BASE"; date -Ins; } > "$out/metadata"
    echo "success_artifacts=$out" >&2
  fi
  for i in 1 2 3 4; do
    if [ -f "$DATA/node$i/pid" ]; then kill -TERM "$(cat "$DATA/node$i/pid")" 2>/dev/null || true; fi
  done
  for _ in $(seq 1 100); do
    local alive=0
    for i in 1 2 3 4; do
      if [ -f "$DATA/node$i/pid" ] && kill -0 "$(cat "$DATA/node$i/pid")" 2>/dev/null; then alive=1; fi
    done
    [ "$alive" -eq 0 ] && break
    sleep 0.1
  done
  for i in 1 2 3 4; do
    if [ -f "$DATA/node$i/pid" ] && kill -0 "$(cat "$DATA/node$i/pid")" 2>/dev/null; then kill -KILL "$(cat "$DATA/node$i/pid")" 2>/dev/null || true; fi
  done
  for _ in $(seq 1 40); do
    local busy=0
    for i in 1 2 3 4; do
      p=$((RPC_BASE+i))
      if ss -ltn "sport = :$p" 2>/dev/null | grep -q LISTEN; then busy=1; fi
      p=$((P2P_BASE+i))
      if ss -ltn "sport = :$p" 2>/dev/null | grep -q LISTEN; then busy=1; fi
    done
    [ "$busy" -eq 0 ] && return 0
    sleep 0.1
  done
  return "$rc"
}
trap cleanup EXIT INT TERM
for i in 1 2 3 4; do
  PORT=$((RPC_BASE+i)); DIR="$DATA/node$i"; ready=0
  for _ in $(seq 1 300); do
    pid=$(cat "$DIR/pid")
    if ! kill -0 "$pid" 2>/dev/null; then
      FAIL_CLASS="FAIL_STARTUP"; echo "node $i exited before RPC readiness (pid=$pid)" >&2; exit 20
    fi
    if [ -f "$DIR/rpc.token" ]; then
      TOKTMP=$(cat "$DIR/rpc.token")
      if curl --connect-timeout 0.1 --max-time 0.5 -sS -X POST "http://127.0.0.1:$PORT" -H 'Content-Type: application/json' -d "{\"id\":\"ready-$i-$_\",\"method\":\"getblockcount\",\"token\":\"$TOKTMP\",\"params\":{}}" | grep -q result; then ready=1; break; fi
    fi
    sleep 0.1
  done
  if [ "$ready" -ne 1 ]; then FAIL_CLASS="FAIL_STARTUP"; echo "node $i not ready after 30s" >&2; exit 21; fi
done
declare -a PK TOK
for i in 1 2 3 4; do
  PORT=$((RPC_BASE+i)); DIR="$DATA/node$i"; TOK[$i]=$(cat "$DIR/rpc.token")
  PK[$i]=$(rpc $PORT "${TOK[$i]}" bft_local_pubkey '{}' | python3 -c 'import sys,json; print(json.load(sys.stdin)["result"]["pubkey"])')
  echo "node$i pk=${PK[$i]}"
done
# Require a stable, simultaneous transport barrier before proposing.
stable=0
for _ in $(seq 1 120); do
  all_ready=1
  counts=""
  for i in 1 2 3 4; do
    PORT=$((RPC_BASE+i))
    INFO=$(curl -sS -X POST "http://127.0.0.1:$PORT" -H 'Content-Type: application/json' -d "{\"id\":\"ready-$i-$_\",\"method\":\"getnetworkinfo\",\"token\":\"${TOK[$i]}\",\"params\":{}}" || true)
    read -r READY PEERS < <(python3 -c 'import json,sys
try:
 r=json.load(sys.stdin).get("result",{}); print(r.get("ready_peer_count",0), r.get("peer_count",0))
except Exception:
 print(0,0)' <<<"$INFO")
    counts="$counts node$i=ready:$READY peers:$PEERS"; [ "$PEERS" -gt 0 ] || true
    last_info[$i]="$INFO"
    [ "$READY" -gt 0 ] || all_ready=0
  done
  if [ "$all_ready" -eq 1 ]; then
    stable=$((stable+1))
    [ "$stable" -ge 3 ] && break
  else
    stable=0
  fi
  sleep 0.25
done
[ "$stable" -ge 3 ] || { FAIL_CLASS="FAIL_PEERS"; echo "unstable peer readiness:$counts" >&2; exit 22; }
# Query every node; require deterministic proposer agreement. Route proposal to its owner.
declare -a PROP
for i in 1 2 3 4; do
  PORT=$((RPC_BASE+i))
  PROP[$i]=$(rpc "$PORT" "${TOK[$i]}" bft_selected_proposer '{}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["proposer_pubkey"])')
done
[ "${PROP[1]}" = "${PROP[2]}" ] && [ "${PROP[1]}" = "${PROP[3]}" ] && [ "${PROP[1]}" = "${PROP[4]}" ] || { echo "proposer disagreement" >&2; exit 1; }
SELECTED=0
for i in 1 2 3 4; do [ "${PK[$i]}" = "${PROP[1]}" ] && SELECTED=$i; done
[ "$SELECTED" -gt 0 ] || { echo "selected proposer not mapped to node" >&2; exit 1; }
PROPOSE=$(rpc $((RPC_BASE+SELECTED)) "${TOK[$SELECTED]}" bft_propose '{}')
BH=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["block_hash"])' <<<"$PROPOSE")
echo "propose node$SELECTED: $PROPOSE"
python3 -c 'import json,sys; assert json.load(sys.stdin)["result"]["status"] == "proposed"' <<<"$PROPOSE"
wait_stage() {
  local stage=$1
  local timeout=${2:-20}
  local first_node=${3:-1}
  local last_node=${4:-4}
  local deadline=$((SECONDS+timeout))
  while [ "$SECONDS" -lt "$deadline" ]; do
    local ok=1
    for i in $(seq "$first_node" "$last_node"); do grep -q "\"stage\":\"$stage\"" "$DATA/node$i/daemon.log" || ok=0; done
    [ "$ok" -eq 1 ] && return 0
    sleep 0.25
  done
  FAIL_CLASS="FAIL_CONSENSUS"
  echo "timed out waiting for stage=$stage" >&2
  for i in 1 2 3 4; do echo "== node$i log ==" >&2; tail -n 120 "$DATA/node$i/daemon.log" >&2; echo "== node$i status ==" >&2; rpc $((RPC_BASE+i)) "${TOK[$i]}" bft_status '{}' >&2 || true; done
  return 1
}
wait_stage_nodes() {
  local stage=$1
  local timeout=${2:-20}
  shift 2
  local deadline=$((SECONDS+timeout))
  while [ "$SECONDS" -lt "$deadline" ]; do
    local ok=1
    for i in "$@"; do grep -q "\"stage\":\"$stage\"" "$DATA/node$i/daemon.log" || ok=0; done
    [ "$ok" -eq 1 ] && return 0
    sleep 0.25
  done
  FAIL_CLASS="FAIL_CONSENSUS"
  echo "timed out waiting for stage=$stage on nodes $*" >&2
  return 1
}
wait_finality_height_nodes() {
  local want=$1
  local timeout=${2:-25}
  shift 2
  local deadline=$((SECONDS+timeout))
  while [ "$SECONDS" -lt "$deadline" ]; do
    local ok=1
    for i in "$@"; do
      local height
      height=$(rpc $((RPC_BASE+i)) "${TOK[$i]}" getfinalityinfo '{}' 2>/dev/null | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"].get("finalized_height","0"))' 2>/dev/null || echo 0)
      [ "${height:-0}" -ge "$want" ] || ok=0
    done
    [ "$ok" -eq 1 ] && return 0
    sleep 0.25
  done
  FAIL_CLASS="FAIL_CONSENSUS"
  echo "timed out waiting for finality height=$want on nodes $*" >&2
  return 1
}

wait_stage proposal_validated 20 2 4
wait_stage prevote 20 2 4
wait_stage precommit_broadcast 20 1 4
wait_stage commit_finality_applied 20
FINAL_HEIGHT=""; FINAL_HASH=""
for i in 1 2 3 4; do
  PORT=$((RPC_BASE+i))
  echo "== node$i bft_status =="; rpc $PORT "${TOK[$i]}" bft_status '{}'
  FINAL=$(rpc $PORT "${TOK[$i]}" getfinalityinfo '{}')
  echo "== node$i finality =="; echo "$FINAL"
  read -r HEIGHT HASH < <(python3 -c 'import json,sys; r=json.load(sys.stdin)["result"]; print(r.get("finalized_height","0"), r.get("finalized_hash",""))' <<<"$FINAL")
  [ "$HEIGHT" -gt 0 ] || { echo "node$i has no nonzero finality" >&2; exit 1; }
  [ -n "$HASH" ] || { echo "node$i has no finalized hash" >&2; exit 1; }
  if [ -z "$FINAL_HEIGHT" ]; then FINAL_HEIGHT=$HEIGHT; FINAL_HASH=$HASH; else
    [ "$HEIGHT" = "$FINAL_HEIGHT" ] && [ "$HASH" = "$FINAL_HASH" ] || { echo "finality mismatch on node$i" >&2; exit 1; }
  fi
done

# Optional liveness scenario: crash one validator, then require the remaining
# quorum to view-change and finalize. E2E_VIEW_CHANGE_KILL_PROPOSER=1 crashes the
# nominal next-view proposer to exercise the signed-quorum fallback issuer.
if [ "${E2E_VIEW_CHANGE:-0}" = "1" ]; then
  NEXT_PROPOSER=$(rpc $((RPC_BASE+1)) "${TOK[1]}" bft_selected_proposer '{"view":1}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["proposer_pubkey"])')
  VICTIM=0
  NEXT_NODE=0
  for i in 1 2 3 4; do
    [ "${PK[$i]}" = "$NEXT_PROPOSER" ] && NEXT_NODE=$i
  done
  [ "$NEXT_NODE" -gt 0 ] || { echo "next-view proposer not mapped to a node" >&2; exit 1; }
  if [ "${E2E_VIEW_CHANGE_KILL_PROPOSER:-0}" = "1" ]; then
    # Hard mode: the nominal next-view proposer is the crashed validator. The
    # ViewChangeQC signers must elect a live fallback proposer.
    VICTIM=$NEXT_NODE
  else
    # Prefer crashing a leaf validator so the remaining quorum keeps a connected
    # gossip mesh. The harness seeds later nodes into earlier ones and addresses
    # are gossiped, so every validator still discovers the full set.
    for i in 3 4 2 1; do
      if [ "$i" -ne "$NEXT_NODE" ]; then VICTIM=$i; break; fi
    done
  fi
  LIVE=()
  for i in 1 2 3 4; do [ "$i" -ne "$VICTIM" ] && LIVE+=("$i"); done
  echo "view-change scenario: kill node$VICTIM; next proposer=node$NEXT_NODE; live=${LIVE[*]}" >&2
  kill -TERM "$(cat "$DATA/node$VICTIM/pid")" 2>/dev/null || true
  for _ in $(seq 1 100); do
    kill -0 "$(cat "$DATA/node$VICTIM/pid")" 2>/dev/null || break
    sleep 0.1
  done
  wait_stage_nodes view_changed 25 "${LIVE[@]}"
  # The NewView proposer is selected from the VC signer set, so it can differ
  # from the nominal next-view proposer even when that node is still alive.
  FALLBACK_PROPOSER=$(rpc $((RPC_BASE+${LIVE[0]})) "${TOK[${LIVE[0]}]}" bft_selected_proposer '{}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["proposer_pubkey"])')
  PROPOSE_NODE=0
  for i in "${LIVE[@]}"; do [ "${PK[$i]}" = "$FALLBACK_PROPOSER" ] && PROPOSE_NODE=$i; done
  [ "$PROPOSE_NODE" -gt 0 ] || { echo "new-view proposer not mapped to a live node" >&2; exit 1; }
  if [ "${E2E_VIEW_CHANGE_KILL_PROPOSER:-0}" = "1" ]; then
    echo "view-change scenario: nominal proposer node$NEXT_NODE down; fallback proposer=node$PROPOSE_NODE" >&2
  else
    echo "view-change scenario: quorum-elected proposer=node$PROPOSE_NODE (nominal node$NEXT_NODE)" >&2
  fi
  PROPOSE2=$(rpc $((RPC_BASE+PROPOSE_NODE)) "${TOK[$PROPOSE_NODE]}" bft_propose '{}')
  BH2=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["block_hash"])' <<<"$PROPOSE2")
  echo "propose after view change node$PROPOSE_NODE: $PROPOSE2"
  wait_finality_height_nodes 2 30 "${LIVE[@]}"
  for i in "${LIVE[@]}"; do
    FINAL2=$(rpc $((RPC_BASE+i)) "${TOK[$i]}" getfinalityinfo '{}')
    HASH2=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["finalized_hash"])' <<<"$FINAL2")
    [ "$HASH2" = "$BH2" ] || { echo "view-change finality hash mismatch on node$i" >&2; exit 1; }
    grep -q '"reason":"new_view_quorum"' "$DATA/node$i/daemon.log" || { echo "node$i advanced without quorum NewView" >&2; exit 1; }
    if grep -q '"reason":"local_timeout"' "$DATA/node$i/daemon.log"; then
      echo "node$i used unquorumed local timeout" >&2; exit 1
    fi
  done
  echo "view-change scenario: quorum new view and height-2 finality verified" >&2
fi
