# PhantomX Desktop Wallet

## Purpose

`phantomx-qt` is the Qt6 desktop wallet for PhantomX. The UI prioritizes real data from the local wallet core and PhantomX node RPC. There are no dummy values for balance, transactions, peers, sync, or PoI status.

## Prerequisites

- Qt6 Widgets (Core, Gui, Widgets, Network)
- CMake 3.20+
- Local `phantomxd` node if live node data is needed

## Build

```bash
cmake -B build-ubuntu -DPHANTOMX_BUILD_QT=ON
cmake --build build-ubuntu --target phantomx-qt -j4
```

Binary: `build-ubuntu/bin/phantomx-qt`

## Feature status (current)

| Feature | Status | Notes |
|---|---|---|
| Default wallet/address | Implemented | If no keystore exists, a default local wallet is created automatically. UI shows a backup warning banner. |
| Create/Open/Import/Backup | Implemented | Create/load use the local keystore. Import opens a native file browser to select a keystore file (not a secret-key string); Backup opens a native save dialog to write the keystore into the file manager. Secret keys stay local and are never sent to RPC/server. |
| Balance | Implemented | Shows the spendable (available) amount from node RPC `spendable_atoms`, formatted in PHX. Immature coinbase outputs are excluded. Shows `—` when neither node nor local data is available. |
| Immature balance | Implemented | Overview shows the node-reported `immature_atoms` from `getbalance` as `Immature Balance: <amount> PHX`, including `0 PHX` when the node is online. When only local fallback data is available, maturity cannot be determined, so the line is hidden unless a non-zero value is known. |
| Hide/show balance | Implemented | The sidebar toggle masks both the sidebar balance card and the Overview Balance / Immature Balance lines. |
| Send | Implemented (basic) | Presets Fast (2 conf) / Normal (6 conf, default) / Slow (12 conf) with different fees; tx sent via wallet core/node RPC. |
| Receive | Implemented | Single keystore can generate many addresses. Copy + popup after generation. |
| Transactions/UTXOs | Implemented (basic) | From `wallet.history()`/UTXO RPC; `Not Available` when offline. |
| Sync blockheader + ETA | Implemented (best-effort) | Shows blocks/headers, progress bar, ETA minutes from sampled sync rate. If target headers/rate unavailable, ETA is `Not Available`. |
| Connected peers (node) | Implemented | From `getconnectioncount` / node RPC. |
| PoI page | Implemented | Labeled `PoI`. Start/Stop PoI best-effort, bandwidth MB/GB/TB, domain/verifier endpoint, score/epoch/peers/status when available from RPC. |
| Treasury auto-sweep | Implemented (best-effort) | If enabled, wallet attempts to sweep non-treasury UTXOs to treasury address on next refresh cycle (fee deducted from swept funds). Opt-in. |
| `sendtoaddress` RPC | Best-effort | Node handler available; uses local datadir wallet keystore (default regtest) and broadcasts via local RPC. Needs production/multi-network hardening. |
| Bandwidth enforcement backend | Incomplete | UI stores limit; real enforcement needs daemon/PoI service support. UI shows an honest note. |
| Lock/unlock password | Incomplete | Keystore encryption/password lock not fully implemented. |
| QR code receive | Incomplete | Currently address + copy. |

## UI layout

- **Overview**: Balance (spendable), Immature Balance (node-reported immature coinbase outputs), Address, Copy Address, block height, connected peers (node), sync blocks/headers, progress, ETA. Compact sync also shown in bottom-left status bar (`Synced` when complete).
- **Send**: send PHX with Fast/Normal/Slow presets (2/6/12 conf targets), different fees.
- **Receive**: treasury address, Generate New Address, Generate 5 Addresses (one keystore, many addresses).
- **Transactions**: transaction/UTXO table.
- **PoI**: Start/Stop, bandwidth, domain, score/epoch, status.
- **Settings**: wide, left-aligned responsive column using roughly 95% of the main area with 24px padding; compact keystore import/backup buttons; wallet management (create/open, import keystore file via file browser, backup keystore via save dialog, treasury) and node/network config.

## Security

- Seed/private key is never sent to RPC/server.
- Audit log does not contain secrets.
- No production dummy data.

## Configuration

- Qt wallet config: `<datadir>/qt-wallet.ini`
- `qt-wallet.ini` in datadir: UI/PoI preferences, example:

```ini
[mining]
enabled=true
bandwidthValue=100
bandwidthUnit=MB
```

## Troubleshooting

- `qt.qpa.theme.gnome: dbus reply error: ... Did not receive a reply` is usually non-fatal on Linux/GNOME. Try running in a normal desktop session or test with `QT_QPA_PLATFORMTHEME=gtk3`.
- If node is offline, UI displays `Not Available` for live RPC fields.
- If ETA is `Not Available`, target headers/sync rate cannot be honestly computed yet.

## Verification

```bash
cmake -B build-ubuntu -DPHANTOMX_BUILD_QT=ON
cmake --build build-ubuntu --target phantomx-qt -j4
QT_QPA_PLATFORM=offscreen timeout 3 ./build-ubuntu/bin/phantomx-qt
ctest --test-dir build-ubuntu --output-on-failure
```

## Treasury

- Treasury is the primary address in the same keystore.
- Can be set via `treasury=<address>` in config; Qt wallet also stores `wallet/treasury_address` in `qt-wallet.ini`.
- If Auto-sweep is enabled, balances from non-treasury addresses are swept to treasury (normal tx, fee deducted from swept funds).
- `getbalance` shows total of all addresses in the keystore.
