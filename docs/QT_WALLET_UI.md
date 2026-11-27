# PhantomX Qt Wallet UI

## Purpose

UI/operational guide for `phantomx-qt`: blockheader sync, ETA estimate, connected node peers, and PoI controls.

## Pages

### Overview
- Compact sync status is also shown in the **bottom-left status bar**: `Syncing...` or `Synced`.
- **Balance**: from real wallet/node RPC.
- **Address**: from local keystore. If no wallet exists, the service creates a default local wallet automatically and shows a backup banner.
- **Block Height**: current node block height.
- **Connected Peers (Node)**: node P2P peer count from RPC.
- **Sync Status**:
  - Shows `Syncing blocks: B / H headers` or `Synced: B / H`.
  - Progress bar based on `blocks/headers`.
  - ETA: `~N minutes remaining` if sync rate can be computed from samples; `Not Available` if not computable yet.
  - When node is offline: `Not Available`.

### Send
- Address, amount (atoms), fee presets: **Fast (2 conf)**, **Normal (6 conf, default)**, **Slow (12 conf)**.
- Different fee per preset; actual confirmations depend on network. Sends via wallet core/node RPC.

### Receive
- Shows primary **Treasury Address**.
- Buttons: Copy, Generate New Address, Generate 5 Addresses.
- One keystore can have many addresses (exchange-friendly). New addresses are shown in a popup and can be copied.

### Transactions
- UTXO/history table from wallet core.
- If empty/offline: honest message (`No transactions / UTXOs` or `Not Available`).

### PoI
- Page label: **PoI**.
- Controls: **Start PoI / Stop PoI**, Apply Settings, Refresh Status.
- Bandwidth limit: number + `MB/GB/TB` unit.
- Domain/verifier endpoint for PoI mining/verifier node.
- Status shows: running, enabled, bandwidth, domain, PoI RPC, score, epoch, node, connected node peers.
- If RPC is unsupported, UI shows `Not Available`/backend note.

### Settings
- **Wallet Management**: Create New Wallet, Open/Load, Import secret (local), Backup Keystore.
- **Node/Network**: network, datadir, node RPC.
- Shows keystore path.

## Config

`<datadir>/qt-wallet.ini`:

```ini
[mining]
enabled=true
bandwidthValue=100
bandwidthUnit=MB
```

`mining/*` is used as the internal PoI settings key (simple compatibility). UI displays it as PoI.

## Sync/ETA logic

- `WalletService::refreshNodeStatus()` fetches `chain_height`, `connection_count`, balance, and best-effort `getblockchaininfo`.
- `headers` defaults to `blocks` if RPC does not provide a header target.
- ETA is computed from `(timestamp, height)` samples over a time window; needs enough samples and `headers > blocks`.
- If data is insufficient: `ETA: Not Available`.

## RPC used (best-effort)

- Wallet/node: `getblockchaininfo`, `getnetworkinfo`, `getconnectioncount` (via wallet/node call paths), wallet send/history/balance.
- PoI: `getpoiinfo`, `startpoi`, `stoppoi`.

## Limitations

- PoI bandwidth enforcement in backend may not be active; UI stores local intent/config.
- Amount/fee are still in atoms.
- Password lock/encrypted keystore not complete.
- QR receive not added yet.

## Treasury

- Treasury is the primary address in the same keystore.
- Can be set in Settings > Wallet Management; stored in `qt-wallet.ini` (`wallet/treasury_address`, `wallet/auto_sweep_treasury`).
- Auto-sweep (opt-in): balance from non-treasury addresses is swept to treasury on next refresh cycle.
- `getbalance` shows total of all addresses in the keystore.
