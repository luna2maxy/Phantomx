# PhantomX Node Operator Guide (Foundation)

## Status
Foundation draft. Verify commands against current binaries before production/testnet operation.

## Basic
- Build: `cmake -S . -B build/dev -DPHANTOMX_BUILD_TESTS=ON && cmake --build build/dev -j`
- Run daemon: `./build/dev/bin/phantomxd --datadir <dir> --rpc-host 127.0.0.1 --rpc-port 8740`
- RPC token: `--rpc-token`, `PHANTOMX_RPC_TOKEN`, or `<datadir>/rpc.token`.

## Safety
- Do not expose RPC publicly without auth/TLS/reverse proxy.
- Backup datadir and wallet keystore securely.
- No mainnet claim.
