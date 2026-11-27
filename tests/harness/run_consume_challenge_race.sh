#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${PHANTOMX_BUILD_DIR:-$ROOT/build-ubuntu}"
BIN="$BUILD_DIR/tests/consume_challenge_race"
TMP="${TMPDIR:-/tmp}/phx-consume-race-$$"
mkdir -p "$TMP"
ITERATIONS="${1:-100}"
PROCS="${2:-2}"

cat > "$TMP/init.cpp" << 'CPP'
#include "phantomx/blockchain/chain.hpp"
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){
  if(argc<2) return 2;
  std::filesystem::path db=argv[1]; std::filesystem::remove(db);
  auto c=phantomx::blockchain::ChainDatabase::open(db);
  if(!c.ok()){ std::cerr<<c.error().to_string(); return 1; }
  phantomx::blockchain::ChainDatabase::VerifierChallengeRecord r{};
  r.challenge_id=std::string(64,'a'); r.verifier_id="v1"; r.pubkey_hex=std::string(64,'1');
  r.owner="o1"; r.network="regtest"; r.payload_blob={1,2,3}; r.issued_at=100; r.expires_at=200; r.status="issued";
  auto p=c.value().put_verifier_challenge(r); if(!p.ok()){ std::cerr<<p.error().to_string(); return 1; }
  (void)c.value().close(); return 0;
}
CPP

g++ -std=c++20 -I"$ROOT/include" -I/usr/include/p11-kit-1 -I/usr/include/mit-krb5 "$ROOT/tests/harness/consume_challenge_race.cpp" "$BUILD_DIR/libphantomx_common.a" -lsqlite3 -lssl -lcrypto -lsodium -lpthread -ldl -o "$BIN"
g++ -std=c++20 -I"$ROOT/include" -I/usr/include/p11-kit-1 -I/usr/include/mit-krb5 "$TMP/init.cpp" "$BUILD_DIR/libphantomx_common.a" -lsqlite3 -lssl -lcrypto -lsodium -lpthread -ldl -o "$TMP/init_db"

ok_total=0; anomalies=0
for i in $(seq 1 "$ITERATIONS"); do
  DB="$TMP/race_$i.db"; BARRIER="$TMP/barrier_$i"; rm -f "$DB" "$BARRIER"
  "$TMP/init_db" "$DB"
  pids=(); outs=()
  for p in $(seq 1 "$PROCS"); do
    OUT="$TMP/out_${i}_${p}"; outs+=("$OUT")
    "$BIN" "$DB" "$(printf 'a%.0s' {1..64})" "v1" "$(printf '1%.0s' {1..64})" "o1" "regtest" 150 "$BARRIER" "$OUT" &
    pids+=($!)
  done
  sleep 0.05; touch "$BARRIER"
  for pid in "${pids[@]}"; do wait "$pid" || true; done
  oks=0
  for OUT in "${outs[@]}"; do [[ "$(head -1 "$OUT" 2>/dev/null || echo err)" == "ok" ]] && oks=$((oks+1)); done
  if [[ "$oks" -eq 1 ]]; then ok_total=$((ok_total+1)); else anomalies=$((anomalies+1)); echo "anomaly iter=$i oks=$oks"; fi
done
echo "iterations=$ITERATIONS processes=$PROCS exactly_one=$ok_total anomalies=$anomalies"
[[ "$anomalies" -eq 0 ]]
