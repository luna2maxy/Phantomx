# PhantomX Testnet Guide

## Purpose

Run and test a local or small-network PhantomX **testnet** node. This guide is operational and intentionally honest: testnet is runnable for development/testing, but it is **not** proof of mainnet readiness.

## Current status

- **Testnet node execution:** runnable with `phantomxd --testnet` (build from source).
- **Public/stable testnet acceptance:** **NOT PROVEN**.
- **Mainnet:** **NO-GO** until mechanically checked formal proof, independent security audit, long-running multi-node adversarial/partition/Byzantine evidence, production VRF/unbiased randomness, and full on-chain slashing settlement economics.

## Prerequisites

- C++20 compiler (GCC/Clang/MSVC)
- CMake 3.20+
- Ninja or Make
- libsodium
- SQLite3 / jansson / libcurl (as required by build)
- Optional: Qt6 (desktop wallet)

## Build

```bash
cmake -S . -B build-ubuntu -DCMAKE_BUILD_TYPE=Release
cmake --build build-ubuntu --target phantomxd phantomx-cli -j4
ctest --test-dir build-ubuntu --output-on-failure
```

## Run a testnet node

The daemon supports `--testnet`, selects `config/testnet.json` by default, and uses the default testnet datadir unless `--datadir` is provided.

### Quick start (script)

```bash
PHANTOMX_BUILD_DIR=$PWD/build-ubuntu \
PHANTOMX_RPC_TOKEN=local-dev-token \
bash scripts/run-testnet-node.sh
```

Defaults:

- RPC: `http://127.0.0.1:18740`
- P2P: `0.0.0.0:18742`
- Datadir: `$REPO/.phantomx-testnet`
- Config: `$REPO/config/testnet.json`
- Log: `$REPO/.phantomx-testnet/testnet.log`

Stop:

```bash
bash scripts/stop-testnet-node.sh
```

Check:

```bash
PHANTOMX_RPC_TOKEN=local-dev-token bash scripts/check-testnet-node.sh
```

### Manual start

```bash
./build-ubuntu/bin/phantomxd \
  --testnet \
  --config ./config/testnet.json \
  --datadir ./.phantomx-testnet \
  --rpc-host 127.0.0.1 --rpc-port 18740 \
  --rpc-token local-dev-token \
  --rpc-allowip 127.0.0.1 \
  --p2p-host 0.0.0.0 --p2p-port 18742
```

Connect to peers (comma-separated):

```bash
PHANTOMX_CONNECT="host1:18742,host2:18742" bash scripts/run-testnet-node.sh
```

## Basic RPC smoke checks

```bash
TOKEN=local-dev-token
curl -sS http://127.0.0.1:18740 -H 'Content-Type: application/json' \
  -d '{"id":"1","method":"getblockcount","params":{},"token":"'"$TOKEN"'"}'
curl -sS http://127.0.0.1:18740 -H 'Content-Type: application/json' \
  -d '{"id":"1","method":"getnetworkinfo","params":{},"token":"'"$TOKEN"'"}'
curl -sS http://127.0.0.1:18740 -H 'Content-Type: application/json' \
  -d '{"id":"1","method":"getblockchaininfo","params":{},"token":"'"$TOKEN"'"}'
curl -sS http://127.0.0.1:18740 -H 'Content-Type: application/json' \
  -d '{"id":"1","method":"getfinalityinfo","params":{},"token":"'"$TOKEN"'"}'
```

Expected: `network/chain=testnet` in network/chain info, RPC responds, node stays running.

## BFT notes for testnet

- BFT daemon/RPC/P2P wiring has been restored and verified in **regtest** 4-node E2E (`scripts/e2e-bft-4node.sh`, including view-change scenario).
- Testnet config does not enable `bft.enabled` by default; PoI is the default consensus mode unless explicitly configured.
- Do not treat BFT regtest E2E as mainnet/testnet Byzantine safety proof. It is functional evidence only.

## Testnet acceptance checklist (evidence required)

| # | Criterion | Status | Evidence required |
|---:|---|---|---|
| 1 | Node starts and accepts RPC | Runnable locally | Startup log, RPC responses, datadir artifacts |
| 2 | Peer discovery/connectivity | Not proven | Multi-node logs, `getnetworkinfo` peer counts |
| 3 | TX submission and inclusion | Not proven | Signed tx, mempool accept, block inclusion, confirmations |
| 4 | PoI flow | Not proven | PoI submit/heartbeat/score/finality RPC evidence |
| 5 | BFT consensus (if enabled) | Regtest E2E only | Multi-node testnet run, finality/view-change logs |
| 6 | Wallet flow | Not proven | Create/import/backup/restore/sign/send/receive |
| 8 | Crash/restart recovery | Not proven | Restart from datadir, chain/finality continuity |
| 9 | Adversarial/partition behavior | Not proven | Reproducible scripts, logs, failure classification |

## Operational cautions

- Do not expose RPC publicly without auth/TLS/firewalling. Default scripts bind RPC to `127.0.0.1`.
- `config/testnet.json` contains protocol/network metadata; CLI flags override config values.
- Some RPC/wallet/mining paths still contain regtest-era assumptions; validate before public testnet use.
- Keep testnet/mainnet NO-GO until the production gates listed above are satisfied.
