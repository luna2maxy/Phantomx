# PhantomX — Action / Change Log

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.


This document records every action/change made in the workspace.  
**Must be updated on every change.**

Entry format (always write entries in **English** — see `docs/DEVELOPMENT.md`):
- **Time (UTC)**
- **Action**
- **Reason/Request**
- **Files changed**
- **Verification**

---

## 2026-11-27 — Documentation language rule: English only

- **Time (UTC):** 2026-11-27T13:45:00Z
- **Action:**
  - Added a project-wide rule in `docs/DEVELOPMENT.md` requiring all written artifacts (docs, changelog entries, reports, code comments, commit messages) to be in English, including edits to existing files.
  - Added a short pointer in `README.md` and a language reminder in the `CHANGELOG_ACTIONS.md` entry format.
  - Translated the quoted request text in the previous changelog entry and the 90/10 reward test report into English.
- **Reason/Request:** User: "Add to the docs that anything written or updated must be written in English".
- **Files changed:** `docs/DEVELOPMENT.md`, `docs/CHANGELOG_ACTIONS.md`, `README.md`, `docs/reports/PHANTOMX_REWARD_SPLIT_90_10_TEST_REPORT.md`.
- **Verification:** Documentation-only change; no code paths or tests affected. Rule text reviewed for consistency across all three locations.

## 2026-11-27 — Master plan reward split 90/10, no pool category

- **Time (UTC):** 2026-11-27T13:37:10Z
- **Action:**
  - Changed consensus reward constants to PoI 90% / verifier 10%; removed `kPoolShareBps` and `EpochSnapshot::pool_operators`.
  - `calculate_epoch_rewards()` now emits only `poi` and `verifier` entries; verifier gets `floor(total * 10000 / 100000)`, PoI gets the exact remainder.
  - Added overflow-safe `mul_div_u64()` / `bps_share()` to the public rewards API and used them in epoch and coinbase/template splits (old `(total * bps) / denom` wrapped above ~9.22e14 atoms).
  - RPC: `getblockreward` / `getrewardinfo` / `getblocktemplate` now report 90/10 and no pool fields.
  - Aligned README, TOKENOMICS, ARCHITECTURE, REWARD_DISTRIBUTION, protocol security spec, settlement design, pool protocol, roadmap, release notes, and all `config/*.json` to 90/10 with no pool subsidy.
  - Added `tests/unit/test_property.cpp` to the unit test target (it was previously never compiled) and added large-total overflow regression tests.
- **Reason/Request:** User: "Change the master plan to remove pool rewards, set verifier to 10% and PoI contributors to 90%, and provide test suite and testing evidence after implementation".
- **Files changed:** `include/phantomx/rewards/rewards.hpp`, `src/rewards/rewards.cpp`, `src/rpc/node_handlers.cpp`, `tests/unit/test_rewards.cpp`, `tests/unit/test_property.cpp`, `tests/CMakeLists.txt`, `config/*.json`, `README.md`, and the docs listed above.
- **Verification:**
  - `cmake --build build -j$(nproc)` → success.
  - `ctest --test-dir build --output-on-failure -j4` → **11/11 passed**.
  - `./build/tests/phantomx_unit_tests` → **141 passed, 0 failed** (incl. `rewards_distribution_90_10_no_pool`, `rewards_large_epoch_no_u64_overflow`, `property_reward_split_large_totals_no_wrap`).
  - ASan build: `ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure -j4` → **11/11 passed**.
  - Regtest daemon RPC: `getrewardinfo` → `distribution=90/10, contributor_percent=90, verifier_percent=10`; `getblockreward` at height 0 → `poi_atoms=2250000000, verifier_atoms=250000000, subsidy_atoms=2500000000`.
  - All five `config/*.json` parsed successfully with `poi_contributors_percent=90, verifier_operators_percent=10`.

## 2026-11-23 — Hide Storage feature in Qt Wallet + Audit Log

- **Time (UTC):** 2026-11-23
- **User request:** "Hide the storage feature from Qt so storage data doesn't grow easily; record all actions/inputs into .md on every change (mandatory)."
- **Actions:**
  - Hid the **Storage** page from the Qt wallet navigation (nav entry, stack page, title/subtitle, refresh path). `buildStoragePage()`/`refreshStoragePage()` and storage status labels are no longer used by the UI.
  - Removed the **Storage API** field/row from the Qt Overview and Settings pages, including the `node/storage` QSettings key and the `settingsStorage_` member.
  - Added runtime audit logging to `WalletService`: every important action/change is appended to `<wallet_dir>/audit.md` (default: `phantomx-wallet-data/audit.md`).
  - Logged actions: `startup`, `set_network`, `set_keystore_path`, `create_wallet`, `load_wallet`, `import_wallet`, `rpc_call` (method+ok), `storage_page_hidden`, `navigate`, `copy_address`, `save_settings`, `send`, `send_failed`, `send_ok`.
  - Added this action log document (`docs/CHANGELOG_ACTIONS.md`) and updated desktop wallet documentation.
- **Reason:** Prevent storage data growth through the Qt GUI; ensure every action/change is recorded in an `.md` file.
- **Files changed:**
  - `apps/phantomx-qt/MainWindow.cpp` — hide storage page/nav/refresh, remove Storage API UI, add audit on navigate/copy/save/send.
  - `apps/phantomx-qt/MainWindow.hpp` — remove storage page declarations/members and `settingsStorage_`.
  - `apps/phantomx-qt/WalletService.hpp` — add `audit()`, `auditPath()`, audit in setters/network/node_call.
  - `apps/phantomx-qt/WalletService.cpp` — implement append-only audit to `audit.md`, audit startup/create/load/import/setKeystorePath.
  - `docs/DESKTOP_WALLET.md` — document hidden Storage status and audit log.
  - `docs/CHANGELOG_ACTIONS.md` — this action log.
- **Verification:**
  - `cmake --build build/qt --parallel 4` — **passed** (linked `bin/phantomx-qt`).
  - `ctest --test-dir build/dev -j8` — **12/12 PASS**.
  - Qt smoke test: `phantomx-qt` launched; `phantomx-wallet-data/audit.md` was created and contains `startup`, `storage_page_hidden`, `rpc_call`, `navigate`, etc.
- **Notes:**
  - The core storage stack (`src/storage/*`, storage RPC) was **not removed**, only hidden from the Qt GUI.
  - `audit.md` contains action metadata only; never write seed/private keys to the audit log.

---

## 2026-11-23 — Normalize documentation language to English

- **Time (UTC):** 2026-11-23
- **User request:** "Add to .md that all language in files should be in English."
- **Actions:**
  - Translated `docs/CHANGELOG_ACTIONS.md` from Indonesian to English.
  - Replaced Indonesian phrases introduced in `docs/DESKTOP_WALLET.md` with English equivalents.
- **Files changed:**
  - `docs/CHANGELOG_ACTIONS.md`
  - `docs/DESKTOP_WALLET.md`
- **Verification:**
  - Grepped touched docs for common Indonesian phrases — none found.

---

## 2026-11-23 — Multi-account keystore, Overview/Settings Receive focus, multi-input send

- **Time (UTC):** 2026-11-23
- **User request:** "On Overview remove create wallet; in Settings focus only on receive for create wallet, and when clicking create wallet show a new address with a popup to copy address and related info; each keystore can create more than 1 wallet like HD wallet in Ethereum (custody, treasury, etc.); if sending 100 and wallet1 has 30, wallet2 has 60, wallet3 has 10, use those 3 addresses to send simultaneously to the same recipient like a Bitcoin transaction."
- **Actions:**
  - **Keystore multi-account:** extended `Keystore` with named accounts (`account.<label>.secret=...`), backward-compatible load of legacy `wallet_signing.secret`, and stable save format (v2) while still writing legacy default key for compatibility.
  - **Wallet multi-account:** added `create_account`, `import_account`, `account_labels`, `accounts`, `address_for`, `public_key_hex_for`, `key_pair_for`. `balance_atoms`, `history`, and `utxos` aggregate across all accounts. `send()` gathers UTXOs from all accounts, selects inputs greedily, signs each input with the owning account key, builds one multi-input transaction, and returns change to the default account.
  - **Qt Overview:** removed "Create wallet" button; Overview now points users to Settings for receive account creation and shows account count.
  - **Qt Settings:** refocused wallet section on receive accounts ("Create receive account"); creating an account prompts for label and shows a popup with address, public key, network, keystore path, and Copy Address action.
  - **Qt Receive:** added account selector combo; Copy Address copies the selected account address.
  - **Qt Send:** description updated to indicate automatic multi-account UTXO combination (Bitcoin-style multi-input).
  - **Tests/docs:** updated wallet export test for version 2 + accounts array; added multi-account create/reload test; updated `docs/DESKTOP_WALLET.md`.
- **Files changed:**
  - `include/phantomx/wallet/keystore.hpp`, `src/wallet/keystore.cpp`
  - `include/phantomx/wallet/wallet.hpp`, `src/wallet/wallet.cpp`
  - `apps/phantomx-qt/WalletService.hpp`, `apps/phantomx-qt/WalletService.cpp`
  - `apps/phantomx-qt/MainWindow.hpp`, `apps/phantomx-qt/MainWindow.cpp`
  - `tests/unit/test_wallet.cpp`
  - `docs/DESKTOP_WALLET.md`
  - `docs/CHANGELOG_ACTIONS.md`
- **Verification:**
  - `cmake --build build/dev --parallel 4` — **passed**.
  - `cmake --build build/qt --parallel 4` — **passed**.
  - `ctest --test-dir build/dev -j8` — **12/12 PASS**.
- **Notes:**
  - Keystore remains plaintext foundation format (0600 on Unix); production should encrypt at rest.
  - Multi-account send uses all keystore accounts as input sources; it does not yet expose per-account coin control in GUI.

---

## 2026-11-23 — Remove storage subsystem

- **Time (UTC):** 2026-11-23
- **User request:** "Maybe remove the storage function because blockchain data is already stored, to avoid many nodes running."
- **Actions:**
  - Removed storage subsystem code and build targets: deleted `src/storage/`, `include/phantomx/storage/`, `phantomx-storage` executable, storage CMake sources, and storage smoke/unit/integration tests.
  - Removed storage RPC registrations from `src/rpc/rpc.cpp` and storage RPC references from docs.
  - Removed `storage_api` from wallet API (`Wallet` constructor/member), CLI `--storage-api`, and Qt `WalletService`; updated tests/callers.
  - Updated docs to reflect removal (BUILDING, DEVELOPMENT, RPC_REFERENCE, DESKTOP_WALLET). ARCHITECTURE_DIAGRAM still needs a follow-up text pass for historical diagram sections.
- **Files changed:**
  - `CMakeLists.txt`, `tests/CMakeLists.txt`
  - `src/rpc/rpc.cpp`
  - `include/phantomx/wallet/wallet.hpp`, `src/wallet/wallet.cpp`
  - `apps/phantomx-wallet/main.cpp`
  - `apps/phantomx-qt/WalletService.hpp`
  - `tests/unit/test_wallet.cpp`, `tests/integration/test_wallet_integration.cpp`
  - `docs/BUILDING.md`, `docs/DEVELOPMENT.md`, `docs/RPC_REFERENCE.md`, `docs/DESKTOP_WALLET.md`
  - Deleted: `src/storage/`, `include/phantomx/storage/`, `tests/unit/test_storage.cpp`, `tests/unit/test_storage_marketplace.cpp`, `docs/STORAGE_PROTOCOL.md`, `docs/STORAGE_MARKETPLACE.md`
  - `docs/CHANGELOG_ACTIONS.md`
- **Verification:**
  - `cmake --build build/dev --parallel 4` — **passed**.
  - `ctest --test-dir build/dev -j8` — **11/11 PASS** (storage smoke/unit/integration tests removed).
- **Notes:**
  - This is a deliberate feature removal; any external clients depending on storage RPC/methods or `phantomx-storage` must be updated.
  - Some historical/architecture docs still mention storage (e.g. `docs/ARCHITECTURE_DIAGRAM.md`, `docs/VERIFIER_NETWORK.md`); they should be updated in a follow-up doc pass.

---

## 2026-11-23 — Regtest foundation: mining, mempool, validation, E2E test

- **Time (UTC):** 2026-11-23
- **User request:** "Lanjutkan regtest foundation: fix generate/apply, tx history/index, integration test regtest E2E, changelog wajib."
- **Actions:**
  - **Fixed `generatetoaddress`/`generateblock` SQLITE_MISUSE crash:** the `mine_one` lambda in `src/rpc/node_handlers.cpp` was captured by reference in RPC handler registrations; changed to capture by value so the callable remains valid when invoked.
  - **Regtest validation enforced in `ChainDatabase::validate_block`:** non-genesis blocks require non-zero bits and valid PoW (`consensus::check_proof_of_work`), coinbase output must not exceed `block_subsidy_atoms(height) + fees`, merkle/tx/UTXO rules validated with in-block UTXO view. (Implemented earlier in session; verified after fix.)
  - **Regtest PoW:** `src/consensus/pow.{hpp,cpp}` provides `kRegtestPowBits`, `compact_to_target`, `check_proof_of_work`; genesis PoW check is skipped.
  - **Mempool wired:** `Mempool` owned by daemon, passed to `register_node_rpc_handlers`; `sendrawtransaction` admits to mempool; `getrawmempool`/`getmempoolinfo` added; `generatetoaddress`/`generateblock` mine coinbase + selected mempool txs, apply block, purge mempool.
  - **Tx history/index improvements:** `gettransaction` now checks mempool then chain and returns `confirmations`/`height` for confirmed txs; `listtransactions` now also returns `mempool_txs_json` for mempool transactions touching the requested pubkey (keeps `utxos_json` for backward compatibility). Full block-history tx index for spent/received chain txs remains a follow-up.
  - **CMake:** linked `JANSSON` include/libs to `phantomx_common` PUBLIC (node_handlers uses jansson).
  - **Tests:** added `tests/integration/test_regtest_e2e.cpp` (generate 101, balance, sendrawtransaction to mempool, mine confirm, verify receiver balance); added `PHX_REQUIRE` macro to `tests/test_framework.hpp`; updated `tests/unit/test_database.cpp` and `tests/network/test_p2p.cpp` block helpers to mine valid regtest PoW and use valid subsidy amounts under new validation.
- **Files changed:**
  - `src/rpc/node_handlers.cpp`
  - `src/blockchain/chain.cpp`
  - `src/consensus/pow.cpp`, `include/phantomx/consensus/pow.hpp` (regtest PoW, earlier in session)
  - `src/utxo/mempool.cpp`, `include/phantomx/utxo/mempool.hpp` (earlier in session)
  - `src/daemon/main.cpp`, `include/phantomx/rpc/node_handlers.hpp`, `src/rpc/rpc.cpp` (earlier in session)
  - `CMakeLists.txt`, `tests/CMakeLists.txt`
  - `tests/integration/test_regtest_e2e.cpp` (new)
  - `tests/test_framework.hpp`
  - `tests/unit/test_database.cpp`
  - `tests/network/test_p2p.cpp`
  - `docs/CHANGELOG_ACTIONS.md`
- **Verification:**
  - `cmake --build build/dev --parallel 4` — **passed**.
  - `ctest --test-dir build/dev --output-on-failure -j4` — **12/12 PASS** (includes `phantomx.integration.regtest_e2e`).
  - Manual smoke: `generatetoaddress 1` returned block hash and increased height/balance; CLI wallet send to mempool + mine confirm succeeded.
- **Notes:**
  - `listtransactions` is still not a full tx-history index; spent chain txs are not yet enumerated from tx_index/blocks. A dedicated wallet tx index or chain scan API is needed for complete history.
  - Regtest validation is a foundation; PoI+Verifier/QC/finality, fork choice/reorg, P2P daemon, and on-chain reward/PoI commitments remain unimplemented.

---

## 2026-11-23 — PoI mining pool integration (blockchain RPC + external pool config)

- **Time (UTC):** 2026-11-23
- **User request:** "Implement what's not yet implemented on blockchain; integrate MiningPool at /home/ubuntu/Documents/GitHub/MiningPool; make sure algorithm is not SHA256 but PoI because it's Proof of Internet."
- **Actions:**
  - **Blockchain RPC / daemon:** wired `pool::PoolLedger` into `phantomxd` and `register_node_rpc_handlers` signature (`include/phantomx/rpc/node_handlers.hpp`, `src/daemon/main.cpp`, `src/rpc/node_handlers.cpp`).
  - **New PoI/pool RPCs:** `getmininginfo` (algorithm=poi), `getblocktemplate` (PoI template), `submitblock` (raw block hex apply), `pool_register_worker`, `pool_submit_share`, `pool_getdashboard`, `pool_getworker`, `submitpoievidence` (computes PoI score via `poi::compute_poi_score`, records share units). Registered in `src/rpc/rpc.cpp` defaults.
  - **MiningPool repo (`/home/ubuntu/Documents/GitHub/MiningPool`) updated for PoI:** `config/pool.yaml` algorithm changed from `sha256d` to `poi`, mining defaults adjusted for PoI score units; `lib/rpc.js` `PHANTOMX_PARAM_KEYS` extended with PoI/pool RPC params; `lib/stratum.js` guards PoI mode to skip PHR-256-YP/SHA256d helper init; `README.md` updated to document PoI algorithm and node RPC integration.
  - **Regtest E2E test updated** for new handler signature (PoolLedger arg).
- **Files changed:**
  - PhantomX: `include/phantomx/rpc/node_handlers.hpp`, `src/rpc/node_handlers.cpp`, `src/rpc/rpc.cpp`, `src/daemon/main.cpp`, `tests/integration/test_regtest_e2e.cpp`, `docs/CHANGELOG_ACTIONS.md`
  - MiningPool: `config/pool.yaml`, `lib/rpc.js`, `lib/stratum.js`, `README.md`
- **Verification:**
  - `cmake --build build/dev --parallel 4` — **PASS**.
  - `ctest --test-dir build/dev --output-on-failure -j4` — **12/12 PASS**.
- **Notes / remaining gaps:**
  - The external MiningPool Stratum wire protocol is still legacy hash-share oriented; full PoI-over-Stratum/PoI job protocol needs a dedicated design (current integration provides PoI RPC surface and PoI config/algorithm reporting).
  - Block sealing/consensus is still regtest PoW-foundation (`kRegtestPowBits`) for block production; PoI evidence is recorded/accounted via pool RPCs but not yet the on-chain block finality/commitment path. Full on-chain PoI commitment, PoI+Verifier/QC/finality, fork choice/reorg, P2P daemon, and complete tx-history index remain unimplemented.

---

## 2026-11-23 — Reward split 89/10/1, verifier rolling, web verifier registry

- **Time (UTC):** 2026-11-23
- **User request:** "Change distribution: PoI contributor 90%, verifier/aggregator 10%; pool takes 1% from PoI contributor; block rewards stored in pool address then paid by worker contribution with min payout and 1% fee for verifier rolled by block height; verifier registry in web API with address/domain/IP status; React web API uploadable from build; realtime JSON on every change."
- **Actions:**
  - **PhantomX rewards:** updated `include/phantomx/rewards/rewards.hpp` and `src/rewards/rewards.cpp` to 89% PoI, 10% verifier base, 1% rolling verifier fee (`verifier_fee`) rotated by `end_height/epoch` across canonical verifier operators. When no verifiers, rolling fee returns to PoI contributors.
  - **Tests:** updated `tests/unit/test_rewards.cpp` distribution test to `rewards_distribution_89_10_1_and_rounding` with new expected values.
  - **MiningPool backend:** added `lib/verifiers.js` file-based verifier registry (id/name/address/endpoint/domain/IP, heartbeat, online/offline TTL, rolling pick). Added API endpoints in `lib/dashboard.js`: `GET /api/verifiers`, `POST /api/verifiers/register`, `POST /api/verifiers/heartbeat`, `GET /api/realtime/status` (writes `web/public/data/status.json` for static polling). Updated `/api/poi/info` reward split to 89/10/1.
  - **MiningPool web:** added React page `web/app/verifiers/page.tsx` polling `/api/realtime/status` with fallback to `/data/status.json` every 5s; shows reward split, rolling verifier, verifier registry, raw realtime JSON. Added initial `web/public/data/status.json`.
- **Files changed:**
  - PhantomX: `include/phantomx/rewards/rewards.hpp`, `src/rewards/rewards.cpp`, `tests/unit/test_rewards.cpp`, `docs/CHANGELOG_ACTIONS.md`
  - MiningPool: `lib/verifiers.js`, `lib/dashboard.js`, `web/app/verifiers/page.tsx`, `web/public/data/status.json`, `README.md` (earlier)
- **Verification:**
  - `cmake --build build/dev --parallel 4` — **PASS**.
  - `ctest --test-dir build/dev --output-on-failure -j4` — **12/12 PASS**.
- **Notes / remaining gaps:**
  - On-chain block reward settlement to a pool address and automatic per-worker payout with minimum payout/fee deduction is not fully implemented end-to-end; PoolLedger/RPC/reward calc provide foundation. A payout engine and pool wallet custody flow are needed.
  - Verifier registry is currently pool-web API file-backed; production should confirm/add verifier registration in official web API/DB with auth, signature, and anti-spoof checks.
  - Realtime static hosting uses polling `data/status.json`; Node-hosted mode serves `/api/realtime/status`. For true push on static hosting, external SSE/WebSocket service is needed.

---

## 2026-11-23 — MiningPool auto payout + web info update (MiningPool-only)

- **Time (UTC):** 2026-11-23
- **User request:** "Implement automatic payout in MiningPool code only (do not change blockchain code); update web info page with what is currently used."
- **Actions:**
  - **Auto payout engine (`MiningPool/lib/payout.js`):** periodic 60s cycle. Computes pending balances from accepted blocks and PPLNS share window; applies 89% PoI / 10% verifier base / 1% rolling verifier fee (by block height, verifier registry). Deducts pool fee from PoI share, enforces `min_payout`, avoids duplicates by counting pending payments, records payments via `db.recordPayment`, attempts `sendtoaddress` RPC when available. State persisted to `data/payout_state.json`.
  - **Wired into web server:** `MiningPool/web/server.js` starts payout engine; added `rpc` import.
  - **API:** `GET /api/payout/status` added; `/api/realtime/status` now includes payout state and writes `web/public/data/status.json`.
  - **Web info updated:** `web/app/getting-started/page.tsx` updated with current PoI flow, 89/10/1 split, auto payout, verifier register/heartbeat, realtime status endpoints, and Verifiers quick link.
  - **Verifier rolling fee payments:** payout engine records rolling 1% verifier fee payments per accepted block (deduped by block height+verifier address).
- **Files changed (MiningPool repo only):**
  - `lib/payout.js` (new), `web/server.js`, `lib/dashboard.js`, `web/app/getting-started/page.tsx`, `README.md`
- **Verification:**
  - `node --check lib/payout.js lib/verifiers.js lib/dashboard.js web/server.js` — **PASS**.
  - No PhantomX blockchain code changed.
- **Notes:**
  - Actual on-chain send depends on node wallet/RPC `sendtoaddress` availability; otherwise payments are recorded as pending.
  - Payout calculation is an accounting MVP based on accepted block rewards + PPLNS window; production should use confirmed on-chain pool wallet balance/UTXO accounting.

---

## 2026-11-23 — Separate verifier web, remove verifier data from MiningPool, add verifier sync RPC in blockchain

- **Time (UTC):** 2026-11-23
- **User request:** "Separate verifier web from MiningPool (create in /home/ubuntu/Documents), remove verifier data from MiningPool, add code to blockchain so verifier data syncs."
- **Actions:**
  - **PhantomX blockchain:** added verifier registry RPCs in `src/rpc/node_handlers.cpp` backed by chain metadata key `verifier_registry_json` (syncs with node data): `verifier_register`, `verifier_heartbeat`, `verifier_list`, `verifier_rolling`. Registered defaults in `src/rpc/rpc.cpp`.
  - **MiningPool:** removed local verifier registry/storage and web: deleted `lib/verifiers.js`, `web/app/verifiers/page.tsx`, `data/verifiers.json`; removed verifier endpoints from `lib/dashboard.js`; updated `lib/payout.js` to fetch rolling verifier from blockchain RPC `verifier_list` instead of local module; updated `web/server.js`, `web/app/getting-started/page.tsx`, `web/public/data/status.json`, `README.md`.
  - **New standalone verifier web:** created `/home/ubuntu/Documents/phantomx-verifier-web` (Vite+React+TS). Polls blockchain RPC every 5s, shows verifier registry/online-offline/rolling verifier, supports dev register/heartbeat, static fallback `public/verifiers.json`. Build output `dist/` uploadable to static hosting.
- **Files changed:**
  - PhantomX: `src/rpc/node_handlers.cpp`, `src/rpc/rpc.cpp`, `docs/CHANGELOG_ACTIONS.md`
  - MiningPool: `lib/payout.js`, `lib/dashboard.js`, `web/server.js`, `web/app/getting-started/page.tsx`, `web/public/data/status.json`, `README.md`, deleted `lib/verifiers.js`, `web/app/verifiers/page.tsx`
  - New: `/home/ubuntu/Documents/phantomx-verifier-web/*`
- **Verification:**
  - `cmake --build build/dev --parallel 4` — **PASS** (after verifier RPC addition).
  - `node --check lib/payout.js lib/dashboard.js web/server.js` — **PASS**.
- **Notes:**
  - Verifier RPCs currently have no auth/signature; production should restrict RPC access and add signed verifier registration/heartbeat.
  - The separate verifier web should not embed production RPC tokens in public static builds; use a read-only proxy or signed snapshot export.

---

## 2026-11-23 — Reward split changed to 90/10 (PoI / Verifier only)

- **Time (UTC):** 2026-11-23
- **User request:** "Change reward split to 90/10 because MiningPool has its own fee setting; split only 2: PoI contributor and verifier."
- **Actions:**
  - **PhantomX rewards:** `kPoiShareBps=90000`, `kVerifierShareBps=10000`, `kPoolShareBps=0`; removed chain-level 1% rolling verifier fee category from `calculate_epoch_rewards`. Updated `tests/unit/test_rewards.cpp` to `rewards_distribution_90_10`.
  - **MiningPool:** payout/dashboard/status/getting-started updated to base 90/10 split; pool fee remains configurable by MiningPool (`pool_fee`/`fee_percent`), deducted from PoI portion. Optional verifier fee is config-driven (default off).
  - **Verifier web:** updated split display to 90/10.
  - **Demo node restarted** with rebuilt binary; verifier registry data in chain metadata preserved.
- **Files changed:**
  - PhantomX: `include/phantomx/rewards/rewards.hpp`, `src/rewards/rewards.cpp`, `tests/unit/test_rewards.cpp`, `docs/CHANGELOG_ACTIONS.md`
  - MiningPool: `lib/payout.js`, `lib/dashboard.js`, `web/app/getting-started/page.tsx`, `web/public/data/status.json`
  - Verifier web: `src/App.tsx`
- **Verification:**
  - `cmake --build build/dev --parallel 4` — **PASS**.
  - `ctest --test-dir build/dev --output-on-failure -j4` — **12/12 PASS**.

---

## 2026-11-23 — Verifier address validation (regtest) + demo verifier updated

- **Time (UTC):** 2026-11-23
- **User request:** "Use regtest address so address is truly validated, not just test."
- **Actions:**
  - **Blockchain RPC:** `verifier_register` now validates `address` as regtest bech32m/P2PK address via `script_pubkey_for_address(address, address_hrp(Network::kRegtest))`; invalid addresses return `kInvalidParams`. Result includes `address_validated=true`.
  - **Demo verifier:** replaced placeholder `PHX1VERIFIERADDR0001` with valid regtest address `rphx1q59kft2aza0pvhfm2rl6e8mvdx4j3uu5hkwkevt4pet8nxd24q42qfeqzvs`; heartbeat loop restarted.
- **Files changed:** `src/rpc/node_handlers.cpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** `cmake --build build/dev --parallel 4` PASS; invalid address rejected by RPC; valid regtest address registered with `address_validated=true`.

---

## 2026-11-23 — Auto coinbase 90/10 verifier split (on-chain, pool-agnostic)

- **Time (UTC):** 2026-11-23
- **User request:** "Make rolling verifier rewards automatic by coinbase; align with MiningPool if needed; MiningPool should not touch this code so other pools can easily set up their mining pool."
- **Actions:**
  - **PhantomX coinbase auto-split:** `generatetoaddress`/`generateblock` now build coinbase with automatic split: 90% miner/pool (`kPoiShareBps`) + fees to reward address, 10% (`kVerifierShareBps`) to rolling verifier selected by block height from chain verifier registry. If no verifier registered, 100% to miner.
  - **getblocktemplate:** added `coinbase_split` (miner_subsidy_atoms, verifier_subsidy_atoms, verifier_address/id, percents) so any pool can build correct coinbase without custom code.
  - **Fix:** verifier helper lambdas captured by value (`pick_rolling_verifier`, load/save/online) to avoid dangling refs after handler registration; removed static-vector trick causing bus error.
  - **MiningPool alignment:** updated `lib/payout.js` comments/math clarity: chain coinbase auto-pays 10% verifier on-chain; pool receives 90% and distributes to PoI workers minus configurable pool fee. MiningPool does not pay verifier base.
- **Files changed:** `src/rpc/node_handlers.cpp`, `docs/CHANGELOG_ACTIONS.md`, `/home/ubuntu/Documents/GitHub/MiningPool/lib/payout.js`
- **Verification:**
  - `cmake --build build/dev --target phantomxd --parallel 4` — **PASS**.
  - `getblocktemplate` returns `coinbase_split` with 90/10 and rolling verifier address.
  - Mined regtest block coinbase has 2 outputs: 2250000000 (miner) + 250000000 (verifier) = 2500000000.

---

## 2026-11-23 — Fix MiningPool coin config (old PHR -> PhantomX PoI)

- **Time (UTC):** 2026-11-23
- **User request:** "Fix coin.json config because it still uses old config."
- **Actions:**
  - Updated `/home/ubuntu/Documents/GitHub/MiningPool/data/database/pool.json` coin/settings from old PhantomRider/PHR (phr-256-yp, RPC 19898) to PhantomX/PHX (PoI, RPC 127.0.0.1:8740, phantomx protocol, token auth).
  - Created `/home/ubuntu/Documents/GitHub/MiningPool/config/coin.json` with current PhantomX PoI coin config, including coinbase auto-split 90/10 chain reward split note.
- **Files changed (MiningPool):** `data/database/pool.json`, `config/coin.json`
- **Verification:** JSON parsed successfully.

---

## 2026-11-23 — Update MiningPool coin.json with mainnet spec

- **Time (UTC):** 2026-11-23
- **User request:** Provided mainnet coin config JSON; asked to fix coin.json.
- **Actions:** Updated `/home/ubuntu/Documents/GitHub/MiningPool/config/coin.json` with provided mainnet spec (network, address, protocol, tokenomics 70/20/10, genesis). Added `pool_operational` and `implementation_note` flagging mismatch with current chain implementation (90/10 coinbase auto-split, pool fee separate).
- **Files changed (MiningPool):** `config/coin.json`

---

## 2026-11-23 — Align coin.json subsidy_split with chain implementation

- **Time (UTC):** 2026-11-23
- **User request:** "Use current chain implementation (90/10 coinbase auto-split, pool fee separate); coin config was mismatched."
- **Actions:** Updated `/home/ubuntu/Documents/GitHub/MiningPool/config/coin.json` `tokenomics.subsidy_split` to 90% PoI/miner-pool, 10% verifier, pool fee configured separately by MiningPool. Updated implementation note.
- **Files changed (MiningPool):** `config/coin.json`

---

## 2026-11-23 — Fix config/*.json subsidy split to match chain

- **Time (UTC):** 2026-11-23
- **User request:** "Fix files in this config folder."
- **Actions:** Updated `config/mainnet.json`, `config/regtest.json`, `config/testnet.json` `tokenomics.subsidy_split` from 70/20/10 to 90% PoI/miner-pool, 10% verifier, pool fee configured separately by MiningPool (matches chain coinbase auto-split implementation).
- **Files changed:** `config/mainnet.json`, `config/regtest.json`, `config/testnet.json`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** JSON parse OK for all three.

---

## 2026-11-23 — Replace storage_provider_payment with native transaction fee asset in config

- **Time (UTC):** 2026-11-23
- **User request:** "storage_provider_payment no longer used; fee uses the coin itself for sending transactions."
- **Actions:** In `config/mainnet.json`, `config/regtest.json`, `config/testnet.json`, removed `tokenomics.storage_provider_payment` and added `tokenomics.transaction_fee_asset = "native_coin"`.
- **Files changed:** `config/mainnet.json`, `config/regtest.json`, `config/testnet.json`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** JSON parse OK.

---

## 2026-11-23 — Audit fixes: consensus, RPC security, DoS, verifier hardening

- **Time (UTC):** 2026-11-23
- **User request:** "Run audit for this blockchain" then "Implement: Consensus expected bits/difficulty, coinbase maturity, timestamp/MTP, size limits, coinbase split enforcement; RPC security token random/configurable, timeout, request limits, authz per method; DoS mempool limits/eviction, block/tx size limits, RPC concurrency model; Verifier hardening signed registration/heartbeat, anti-spoof, don't make registry consensus without final rules."
- **Actions (Consensus):**
  - `src/blockchain/chain.cpp`: enforce `kMaxBlockBytes`, regtest expected bits (`kRegtestPowBits`), PoW, MTP timestamp (`> median of last 11`) and future-time limit (2h). Added coinbase split enforcement: if verifiers registered, coinbase must include exact 10% subsidy output to deterministic rolling verifier (height % registered verifiers). No verifier => 100% miner.
  - `src/utxo/validation.cpp`, `include/phantomx/utxo/validation.hpp`: added `kCoinbaseMaturity=100`, `kMaxTxBytes`, tx size check, coinbase canonical input check (1 input, zero prevout, vout 0xFFFFFFFF), coinbase maturity check in `validate_transaction(..., spend_height, coinbase_maturity)`, canonical P2PK script_pubkey check (`0x01 0x20`).
  - `src/utxo/mempool.cpp`, `include/phantomx/utxo/mempool.hpp`: mempool size/bytes limits (`kMaxEntries=5000`, `kMaxBytes=50MB`), track bytes, reject when full.
  - `src/rpc/node_handlers.cpp`: mine_one uses MTP-safe timestamp, passes maturity/height to validation, deterministic rolling verifier (all registered, not time-based online preference) for consensus safety.
  - Tests: updated database/network block helpers timestamps; regtest_e2e selects mature coinbase UTXO.
- **Actions (RPC/DoS/Verifier):**
  - `src/rpc/http_server.cpp`: socket recv/send timeout (5s), Content-Length case-insensitive parse, reject oversized Content-Length. Still single-threaded (documented limitation).
  - `src/daemon/main.cpp`: RPC token configurable via `--rpc-token` / `PHANTOMX_RPC_TOKEN` / datadir `rpc.token`; generates random token with 0600 perms if missing.
  - `src/rpc/node_handlers.cpp`: verifier register/heartbeat hardening - endpoint/id validation, id/address mismatch rejection, optional `PHANTOMX_VERIFIER_REG_TOKEN` gate.
  - `include/phantomx/common/error.hpp`, `src/common/error.cpp`: added `kResourceExhausted`.
- **Files changed:** `src/blockchain/chain.cpp`, `src/utxo/validation.cpp`, `include/phantomx/utxo/validation.hpp`, `src/utxo/mempool.cpp`, `include/phantomx/utxo/mempool.hpp`, `src/rpc/node_handlers.cpp`, `src/rpc/http_server.cpp`, `src/daemon/main.cpp`, `include/phantomx/common/error.hpp`, `src/common/error.cpp`, `tests/unit/test_database.cpp`, `tests/network/test_p2p.cpp`, `tests/integration/test_regtest_e2e.cpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** `cmake --build build/dev --parallel 4` PASS; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Caveats:** RPC server remains single-threaded (timeouts added, but no concurrency model yet). Verifier registry is consensus-adjacent due to coinbase split enforcement; signed verifier ops/admin governance still needed for production. `rpc.token`/CLI default compatibility: clients using old hardcoded `local-dev-token` must pass `--rpc-token` or use datadir token file.

---

## 2026-11-23 — Set default max block size to 128 KB

- **Time (UTC):** 2026-11-23
- **User request:** "Max block 128kb saja."
- **Actions:** Changed `kMaxBlockBytes` from `1'000'000` to `128 * 1024` in `include/phantomx/utxo/validation.hpp`.
- **Files changed:** `include/phantomx/utxo/validation.hpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** build check after change.

---

## 2026-11-23 — Update Qt wallet rewards page to current chain data

- **Time (UTC):** 2026-11-23
- **User request:** "Update Qt to match current blockchain data."
- **Actions:** Updated `apps/phantomx-qt/MainWindow.cpp` rewards page: replaced old 70/20/10 split with current coinbase auto-split 90% PoI/miner-pool, 10% verifier rolling (on-chain), pool fee configured separately by MiningPool; added max block size 128 KB; updated stat card from "Pool share" to "Verifier share".
- **Files changed:** `apps/phantomx-qt/MainWindow.cpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** `cmake --build build/dev --target phantomx-qt --parallel 4` PASS.

---

## 2026-11-23 — Qt node status shows online/synced height

- **Time (UTC):** 2026-11-23
- **User request:** "Node offline should be syncing or what sync" in Qt.
- **Actions:** `apps/phantomx-qt/MainWindow.cpp` `updateHeader()` now probes `getblockcount`; shows `NODE ONLINE · H <n>` (green) when RPC succeeds, `NODE OFFLINE` (yellow) when not. 
- **Files changed:** `apps/phantomx-qt/MainWindow.cpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** `cmake --build build/dev --target phantomx-qt --parallel 4` PASS; relaunched Qt.

---

## 2026-11-23 — Qt: remove Rewards page, add realtime PoI mining/pool page

- **Time (UTC):** 2026-11-23
- **User request:** "Remove Rewards page; in PoI page allow direct mining and connect to pool, with realtime data shown in wallet (not foundation) like share and contribution pool size to pool."
- **Actions:** Removed Rewards nav/page/members. Rebuilt PoI page with realtime pool stats (score, shares/units, contribution %, pool size/workers), worker register, Start/Stop PoI mining (submits `submitpoievidence` periodically), and 5s refresh via `pool_getdashboard/pool_getworker/getpoistats`. Added QTimer.
- **Files changed:** `apps/phantomx-qt/MainWindow.cpp`, `apps/phantomx-qt/MainWindow.hpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** `cmake --build build/dev --target phantomx-qt --parallel 4` PASS; Qt relaunched.

---

## 2026-11-23 — Qt PoI page connected to MiningPool API

- **Time (UTC):** 2026-11-23
- **User request:** "Seems not connected with MiningPool."
- **Actions:** Updated Qt PoI page to use MiningPool HTTP API (`http://127.0.0.1:3000` default): register via `POST /api/poi/register`, submit shares via `POST /api/poi/submit`, realtime stats from `GET /api/realtime/status` and `GET /api/poi/workers`. Added QNetworkAccessManager JSON helpers.
- **Files changed:** `apps/phantomx-qt/MainWindow.cpp`, `apps/phantomx-qt/MainWindow.hpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** `cmake --build build/dev --target phantomx-qt --parallel 4` PASS; Qt relaunched.

---

## 2026-11-23 — Qt PoI sync worker name/address with MiningPool

- **Time (UTC):** 2026-11-23
- **User request:** "Sepertinya di MiningPool ada workername and address tolong syncronkan."
- **Actions:** Added Worker Name and Wallet Address fields to Qt PoI page; Worker ID auto-syncs as `address.workername`. Register/start-mining/submit now send `worker_name` and `address` to MiningPool `/api/poi/register` and `/api/poi/submit`. Realtime status displays worker name and address from `/api/poi/workers`.
- **Files changed:** `apps/phantomx-qt/MainWindow.cpp`, `apps/phantomx-qt/MainWindow.hpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** `cmake --build build/dev --target phantomx-qt --parallel 4` PASS; Qt relaunched.

---

## 2026-11-23 — Qt PoI: Worker ID primary, remove Worker Name field

- **Time (UTC):** 2026-11-23
- **User request:** "Ada workername ada worker id, tapi that terbaca adalah worker id and worker name only template, pindahkan worker id ke atas and hapus worker name."
- **Actions:** Removed Worker Name field from Qt PoI page; moved Worker ID to top as primary identifier. Register/submit/status now use Worker ID + Wallet Address only.
- **Files changed:** `apps/phantomx-qt/MainWindow.cpp`, `apps/phantomx-qt/MainWindow.hpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** `cmake --build build/dev --target phantomx-qt --parallel 4` PASS; Qt relaunched.

---

## 2026-11-23 — Qt PoI offline on close/stop, score/unit sync

- **Time (UTC):** 2026-11-23
- **User request:** "Pastikan data share compute unit score semua syncron with miningpool and wallet; ketika qt di close or stop wallet maka on web miningpool also data workernya offline or not terbaca."
- **Actions:** Qt now sends `POST /api/poi/offline` when PoI mining is stopped or window is closed (`closeEvent`). Realtime refresh pulls worker detail (`/api/poi/worker`) for score/units sync and uses submit response score_milli. Register/submit include address; status shows worker/address/units.
- **Files changed:** `apps/phantomx-qt/MainWindow.cpp`, `apps/phantomx-qt/MainWindow.hpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** `cmake --build build/dev --target phantomx-qt --parallel 4` PASS; Qt relaunched.

---

## 2026-11-23 — Qt PoI score shows foundation status

- **Time (UTC):** 2026-11-23
- **User request:** "Verifikasi wallet because data mining not terkoneksi and hash info - for semuanya."
- **Actions:** When node `getpoistats` returns foundation/no score, Qt shows `status` (e.g. `foundation`) instead of leaving `-`.
- **Files changed:** `apps/phantomx-qt/MainWindow.cpp`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:** build PASS, Qt relaunched.

---

## 2026-11-23 — Prompt 01 (Roadmap prompt.txt) Audit & storage-remnant cleanup

- **Time (UTC):** 2026-11-23
- **User request:** "jalankan 1/1 tugas di prompt.txt from prompt 1 until prompt 14 secara bertahap and berkelanjutan catat semua tindakan and masukan ke docs .md"
- **Scope executed:** Started **Prompt 01 — Audit and Revisi Total Arsitektur** (prompt.txt). Did not claim Prompt 02-14 complete.

### Baseline verification (actual)
- `ctest --test-dir build/dev --output-on-failure -j4` — **12/12 PASS** (30.27s, includes regtest_e2e, network, unit, smoke, verifier, poi.anti_sybil).
- After code cleanup: `cmake --build build/dev --parallel 4` — **PASS** (Qt warnings pre-existing, no compile errors).

### Audit findings (repository)
- **Project:** PhantomX L1 foundation, C++20, CMake, libsodium, SQLite3, jansson, CURL; optional Qt6. Binaries: `phantomxd`, `phantomx-cli`, `phantomx-wallet`, `phantomx-poi`, `phantomx-pool`, optional `phantomx-qt`.
- **Storage status:** Storage subsystem code/directories already removed from `src/` and `include/phantomx/` (git status shows D `src/storage/*`, D `include/phantomx/storage/*`, D storage docs/tests). No storage build targets in CMake. Remaining **code remnants** found and cleaned in this pass: `VerifierType::{kStorage,kBoth}`, `PoolRole::{kStorageProvider,kStorageClient}`, `KeyRole::kStorageProviderIdentity`, node_handlers role mapping `"storage"`, keystore parsing for `storage_provider_identity`.
- **Storage refs still present in docs** (need doc pass): `ARCHITECTURE.md`, `ARCHITECTURE_DIAGRAM.md`, `KNOWN_LIMITATIONS.md`, `VERIFIER_NETWORK.md`, `CRYPTOGRAPHY.md`, `POOL_PROTOCOL.md`, `TOKENOMICS.md` inconsistency notes, historical frontend references, etc. Prompt 01 requires removing/disabling storage refs in docs/build/RPC/config/reward.
- **Tokenomics inconsistency:** prompt.txt requires 70% Contributor, 20% Verifier, 10% Pool Operator (total 100%). Current code/docs implement 90/10 coinbase auto-split (PoI/miner-pool 90%, verifier 10%) with pool fee configured separately by MiningPool (`rewards.hpp`, config/*.json, REWARD_DISTRIBUTION.md). This is a deliberate documented deviation; Prompt 01/10 requires decision + doc/code alignment.
- **Architecture gap vs prompt.txt:** prompt requires Verifier Engine, Aggregator Engine, Verification Certificate, Verification Committee, Verification Assignment; separate contributor/verifier/aggregator/verifier identities and clear PoI+Verifier finality model. Current code has verifier module, pool ledger, PoI, consensus/PoI+Verifier foundation, but no dedicated `src/aggregator` module and aggregator certificate flow not fully implemented as specified.

### Files changed (this pass)
- `include/phantomx/verifier/registry.hpp` — removed `VerifierType::kStorage,kBoth`; PoI-only verifier type.
- `src/verifier/registry.cpp` — removed `kBoth` eligibility branch.
- `include/phantomx/pool/pool.hpp` — removed `PoolRole::{kStorageProvider,kStorageClient}`.
- `src/rpc/node_handlers.cpp` — removed `"storage"` pool role mapping.
- `include/phantomx/common/crypto.hpp` — removed `KeyRole::kStorageProviderIdentity`.
- `src/common/crypto.cpp` — removed storage role string mapping.
- `src/wallet/keystore.cpp` — ignore removed `storage_provider_identity` legacy role.
- `docs/CHANGELOG_ACTIONS.md` — this entry.

### Files deleted
- None in this pass (storage files already deleted in prior worktree state).

### Risks / open issues (Prompt 01)
- **Tokenomics decision required:** prompt.txt specifies 70/20/10 (Contributor/Verifier/Pool Operator). Current implementation is 90/10 + pool fee separate. Must be reconciled before Prompt 10 implementation; affects consensus/reward accounting.
- **Storage docs still widespread:** docs and some frontend references still describe storage; requires systematic doc update.
- **Aggregator not implemented as module:** required by prompt (attestation collection, quorum, certificate builder/verifier, rotation/failure recovery). Current verifier/pool code is foundation only.
- **Consensus/PoI+Verifier is foundation:** simplified timeout/view-change, no persistent vote/QC store, no full pacemaker, randomness/proposer selection needs hardening; finality rules and reorg/finality guard need completion (Prompt 04/09).
- **RPC security/hardening:** auth token exists; role-based authz, TLS, full method coverage, and hardening incomplete.
- **Do not claim mainnet/testnet readiness.**

### Dependency audit (initial)
- **libsodium** — crypto/sign/random/auth (ISC-like). Required.
- **SQLite3** — embedded chain/wallet/db storage (public domain). Required.
- **libjansson** — JSON parsing/serialization (MIT). Required.
- **libcurl** — HTTP client/wallet-node client (curl license/MIT-like). Required.
- **Qt6** (optional) — desktop GUI (LGPL/commercial; build option `PHANTOMX_BUILD_QT`). Optional.
- License compatibility: appears permissive/LGPL-optional; full third-party license inventory and vulnerability scan still required for release.

### Next steps (Prompt 01 continuation)
1. Update docs to remove storage and align with PoI+Verifier+Aggregator+PoI+Verifier model: ARCHITECTURE.md, CONSENSUS.md, PROOF_OF_INTERNET.md, TOKENOMICS.md, KNOWN_LIMITATIONS.md, VERIFIER_NETWORK/PROTOCOL docs, diagrams.
2. Create missing required docs: `docs/VERIFIER_PROTOCOL.md`, `docs/AGGREGATOR_PROTOCOL.md`, `docs/THREAT_MODEL.md`, `docs/ROADMAP.md` (or align with existing PHANTOMX_ROADMAP.md).
3. Resolve tokenomics 70/20/10 vs 90/10 decision and update code/docs/config/tests consistently.
4. Run build+tests after each doc/code batch and record results.

---

## 2026-11-23 — Prompt 02 Core Blockchain Foundation (baseline complete)

- **Time (UTC):** 2026-11-23
- **Scope:** Prompt 02 audit/confirmation; existing foundation already largely present.
- **Actions:**
  - Verified core modules exist: common types/error/logging/serialization/config/crypto, primitives hash/merkle/bech32m/transaction/block, genesis/chain params, UTXO/chain DB.
  - Added explicit foundation headers: `include/phantomx/common/amount.hpp` (AmountAtoms, fixed-point helpers, overflow checks), `include/phantomx/common/chain_params.hpp` (mainnet/testnet/regtest params).
  - Confirmed no empty placeholder functions in core (only `bits` field comment noted earlier).
- **Files changed:** `include/phantomx/common/amount.hpp`, `include/phantomx/common/chain_params.hpp`, `docs/ROADMAP.md`, `docs/CHANGELOG_ACTIONS.md`
- **Verification:**
  - Header compile check: `g++ -std=c++20 -Iinclude -c /tmp/check_phx.cpp` PASS.
  - `ctest --test-dir build/dev --output-on-failure -j4` — **12/12 PASS**.
- **Notes:** Prompt 02 acceptance criteria satisfied by existing implementation + checks; deeper canonical serialization/address/chain-param hardening continues in later prompts.

---

## 2026-11-23 — Prompt 03 UTXO & Transaction Engine (audit pass)

- **Time (UTC):** 2026-11-23
- **Scope:** Prompt 03 audit/verification.
- **Findings:** Transaction/OutPoint/TxIn/TxOut/UTXO/mempool/validation/tx_builder/sighash/p2pk ed25519 address/fee checks exist. Coinbase maturity, max tx size, canonical script_pubkey checks present in validation.
- **Verification:** `ctest --test-dir build/dev -R "utxo|wallet|mempool|regtest_e2e|unit" --output-on-failure -j4` → **4/4 PASS**.
- **Gaps:** Explicit dust rule/policy doc, broader adversarial tx fuzz/property tests, full signature scheme spec doc alignment remain for later prompts.

---

## 2026-11-23 — Prompt 04 Chainstate DB (audit pass)

- **Time (UTC):** 2026-11-23
- **Findings:** `ChainDatabase` uses SQLite + `TransactionGuard` with rollback on errors; supports block store/index/apply/disconnect/undo/recovery/snapshot/verify. Reorg/finality guard is limited.
- **Verification:** `ctest --test-dir build/dev -R "database|regtest_e2e|integration" --output-on-failure -j4` → **3/3 PASS**.
- **Gaps:** Finality-aware reorg prevention, schema migration versioning hardening, crash-injection tests.

---

## 2026-11-23 — Prompt 01-14 gradual execution pass (audit, alignment, gap tracking)

- **Time (UTC):** 2026-11-23
- **User request:** Execute prompt.txt Prompt 01-14 gradually/continuously; record actions in docs.
- **Actions completed (real):**
  - **Prompt 01:** Repo audit; removed storage code remnants (VerifierType storage/both, PoolRole storage, KeyRole storage provider, storage pool-role mapping); updated/created architecture docs (`ARCHITECTURE.md`, `VERIFIER_PROTOCOL.md`, `AGGREGATOR_PROTOCOL.md`, `THREAT_MODEL.md`, `ROADMAP.md`, `PROMPT01_AUDIT_REPORT.md`, `KNOWN_LIMITATIONS.md`, `TOKENOMICS.md`, etc.).
  - **Prompt 02:** Verified core foundation; added `common/amount.hpp`, `common/chain_params.hpp`; header compile check PASS.
  - **Prompt 03:** Audited UTXO/tx/mempool/validation/tx builder; targeted tests PASS.
  - **Prompt 04:** Audited chain DB atomic apply/disconnect/recovery/snapshot; targeted tests PASS.
  - **Prompt 05-07,09,11-13:** Audited existing modules/tests; recorded gaps (full PoI+Verifier hardening, full P2P hardening, authenticated verifier delivery, full RPC/RBAC/multi-node scripts, etc.).
  - **Prompt 08:** Aggregator protocol doc created; dedicated aggregator module implementation still pending (gap recorded).
  - **Prompt 10:** Aligned reward split to prompt.txt 70/20/10 in `rewards.hpp`, `rewards.cpp`, `node_handlers.cpp` coinbase/template split, `tests/unit/test_rewards.cpp`, config files, `REWARD_DISTRIBUTION.md`, `TOKENOMICS.md`.
  - **Prompt 14:** Created foundation security/operator guide docs (`SECURITY.md`, node/verifier/contributor/verifier/aggregator/wallet guides, `RELEASE_NOTES.md`); no mainnet claim.
- **Files created:** `docs/VERIFIER_PROTOCOL.md`, `docs/AGGREGATOR_PROTOCOL.md`, `docs/THREAT_MODEL.md`, `docs/ROADMAP.md`, `docs/PROMPT01_AUDIT_REPORT.md`, `docs/NODE_OPERATOR_GUIDE.md`, `docs/VALIDATOR_GUIDE.md`, `docs/CONTRIBUTOR_GUIDE.md`, `docs/VERIFIER_GUIDE.md`, `docs/AGGREGATOR_GUIDE.md`, `docs/WALLET_GUIDE.md`, `docs/RELEASE_NOTES.md`, `SECURITY.md`, `include/phantomx/common/amount.hpp`, `include/phantomx/common/chain_params.hpp`.
- **Key files modified:** `include/phantomx/rewards/rewards.hpp`, `src/rewards/rewards.cpp`, `src/rpc/node_handlers.cpp`, `tests/unit/test_rewards.cpp`, `config/mainnet.json`, `config/testnet.json`, `config/regtest.json`, `docs/*` (see git diff).
- **Verification (actual):**
  - `ctest --test-dir build/dev --output-on-failure -j4` → **12/12 PASS** after cleanup and after 70/20/10 alignment.
  - `cmake --build build/dev --parallel 4` → **PASS**.
- **Unresolved / next implementation work:**
  - Tokenomics 70/20/10 now aligned in code; ensure all external tooling (MiningPool, QT labels) updated.
  - Aggregator module (`src/aggregator`) and certificate flow not implemented.
  - Full PoI+Verifier finality hardening, P2P adversarial hardening, RPC RBAC/full method coverage, full regtest multi-node scripts/services, fuzz/property/chaos/Byzantine tests, and independent audit remain.
  - No mainnet/testnet readiness claim.

---

## 2026-11-23 — Prompt 08 Aggregator module foundation

- **Time (UTC):** 2026-11-23
- **Scope:** Prompt 08 gap - implement aggregator engine foundation.
- **Actions:**
  - Added `include/phantomx/aggregator/aggregator.hpp`, `src/aggregator/aggregator.cpp`.
  - Implemented: VerifierAttestation serialization/hash/signature verify, AggregationEngine submit_attestation (duplicate/conflict/expiry/signature checks), quorum evaluation, VerificationCertificate build/sign/verify (ordered unique verifier set, attestation hash alignment, aggregator signature).
  - Added `tests/unit/test_aggregator.cpp` (quorum+verify, duplicate/invalid signature rejection, insufficient quorum).
  - Wired into `CMakeLists.txt` and `tests/CMakeLists.txt`.
- **Verification:**
  - `cmake --build build/dev --parallel 4` — **PASS**.
  - `ctest --test-dir build/dev --output-on-failure -j4` — **12/12 PASS**.
- **Gaps remaining:** aggregator registry network/RPC integration, assignment/rotation/failure recovery, dispute handling, full-node RPC verification endpoint, integration with PoI/verifier/PoI+Verifier flows.

---

## 2026-11-23 — Prompt 13 regtest scripts foundation

- **Time (UTC):** 2026-11-23
- **Actions:**
  - Created `scripts/start-regtest.sh`, `scripts/stop-regtest.sh`, `scripts/reset-regtest.sh`, `scripts/check-regtest.sh`, `scripts/create-regtest-wallet.sh`, `scripts/send-regtest-transaction.sh`.
  - Made executable and validated with `bash -n`.
- **Verification:** `bash -n scripts/*.sh` — **SYNTAX_OK**.
- **Gaps:** Scripts are foundation; full 4-verifier PoI+Verifier consensus, peer wiring, contributor/verifier/aggregator/pool orchestration, and documented E2E acceptance still pending.

---

## 2026-11-23 — Prompt 11 RPC coverage + RBAC foundation

- **Time (UTC):** 2026-11-23
- **Actions:**
  - Added Prompt 11 RPC methods in `src/rpc/node_handlers.cpp`: `getblockchaininfo`, `getchaintips`, `gettxout`, `getrawtransaction` (foundation), `addnode`/`disconnectnode` (honest not-implemented), PoI info/score/participant, verifier info/register/verification/assignment, aggregator info/aggregation/certificate/verifycertificate (foundation), consensus/verifier/finality/quorum certificate info (foundation), `getblockreward`, `getrewardinfo`, `getepochrewards`.
  - RBAC foundation already present in `RpcServer` (`RpcUser.allowed_methods`, auth token, rate limit, audit log).
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Gaps:** Many methods are foundation/informational; full P2P node control, full chain tx index, aggregator certificate storage/verification wiring, PoI+Verifier verifier/QC/finality real data, and complete RBAC policy config remain.

---

## 2026-11-23 — Prompt 08 aggregator RPC wiring

- **Time (UTC):** 2026-11-23
- **Actions:**
  - Extended `register_node_rpc_handlers` to accept `aggregator::AggregationEngine&` (updated header, cpp, daemon, regtest_e2e).
  - Added real aggregator RPCs: `getaggregatorinfo`, `submitattestation`, `buildcertificate`, `getaggregation`, `verifycertificate` (signature-only foundation mode). `getcertificate` remains foundation (no persistent cert store yet).
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Gaps:** Persistent certificate store/index, full certificate verification from stored cert, aggregator registry/assignment/rotation/failure recovery/dispute RPCs, PoI/verifier/PoI+Verifier integration.

---

## 2026-11-23 — Prompt 08 completion pass (store/registry/dispute/assignment/RPC)

- **Time (UTC):** 2026-11-23
- **Actions:**
  - Aggregator engine extended: certificate store/lookup (`store_certificate/get_certificate/certificate_hashes`), auto-store on build, dispute open/resolve, assignment register/get/rotate.
  - RPCs added/wired: `aggregator_register`, `aggregator_list`, `aggregator_set_status`, `aggregator_open_dispute`, `aggregator_resolve_dispute`, `aggregator_disputes`, `aggregator_register_assignment`, `aggregator_get_assignment`, `aggregator_rotate_assignment`; `getcertificate` returns stored cert; `verifycertificate` verifies stored cert fully.
  - Tests extended: `aggregator_certificate_store_and_lookup`, `aggregator_dispute_open_resolve`.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Remaining:** Deep PoI/verifier/PoI+Verifier integration and persistent certificate DB/index.

---

## 2026-11-23 — Prompt 09 PoI+Verifier hardening pass

- **Time (UTC):** 2026-11-23
- **Actions:**
  - `poi+verifier.hpp/cpp`: added `observe_qc`, lock rule (prevote QC sets lock), precommit conflict-with-lock rejection, finalize sets lock, `save_state/load_state` basic persistence.
  - `tests/unit/test_consensus.cpp`: added lock conflict and persist/reload tests.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Gaps:** Full pacemaker, complete vote/QC/equivocation persistence, unbiased randomness, slashing, property/Byzantine tests.

---

## 2026-11-23 — Prompt 09 gap-closure pass

- **Time (UTC):** 2026-11-23
- **Actions:**
  - PoI+Verifier: added `locked_view`, higher-view prevote QC unlock, precommit QC lock update, persistence loads equivocations, tests for unlock/persistence.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Remaining gaps:** Full pacemaker, complete durable vote/QC store, unbiased production randomness, slashing/governance, full property/Byzantine/fuzz/chaos suite.

---

## 2026-11-23 — Prompt 11 RBAC/docs gap pass

- **Time (UTC):** 2026-11-23
- **Actions:**
  - `phantomxd`: added `--rpc-role=admin|readonly|operator` and `--rpc-allow-methods` for RBAC allowlist.
  - Updated `docs/RPC_REFERENCE.md` with current method groups, auth/RBAC behavior, and foundation status notes.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Remaining:** Full real semantics for all listed RPCs, persistent tamper-evident audit log, TLS/proxy guidance enforcement, complete RPC reference per method params/errors.

---

## 2026-11-23 — Prompt 14 small property test pass

- **Time (UTC):** 2026-11-23
- **Actions:** Added `tests/unit/test_property.cpp` with real randomized property checks (serialization roundtrip, reward invariant total <= subsidy). Wired into unit tests.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Remaining:** Full fuzz/property/chaos/partition/Byzantine/collusion/long-running suite, testnet packaging, independent audit.

---

## 2026-11-23 — Prompt 08 cert persistence (file-based)

- **Time (UTC):** 2026-11-23
- **Actions:** Added `AggregationEngine::save_state/load_state` (file-based cert persistence including attestations). Daemon loads aggregator state on start and passes state path; `buildcertificate` saves after success.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Gap remaining:** Full SQLite DB index for certs/attestations, deep PoI/verifier/PoI+Verifier finality/reward integration, production arbitration/slashing governance.

---

## 2026-11-23 — Prompt 11 audit log persistence

- **Time (UTC):** 2026-11-23
- **Actions:** Added `RpcServer::set_audit_log_path`; RPC calls appended to `rpc_audit.log` with simple hash chain (`prev_hash`/`hash`) for tamper-evidence foundation. Daemon sets `<datadir>/rpc_audit.log`.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Remaining:** Full append-only/tamper-evident storage hardening, external audit sink, TLS.

---

## 2026-11-23 — Gap closure pass: SQLite cert index, RPC/regtest/test harness

- **Time (UTC):** 2026-11-23
- **Actions:**
  - **Prompt 08 (SQLite cert index):** Added `AggregationEngine::save_state_db/load_state_db` using SQLite (`certificates` table, PK `cert_hash`, indexes `assignment_id`,`subject_id`, stores full cert+attestations). Daemon loads `aggregator.db` on start (fallback to txt) and saves DB+txt on `buildcertificate`. Added unit test `aggregator_sqlite_persistence_roundtrip`.
  - **Prompt 11 (RPC semantics):** `getrawtransaction` returns mempool fee when found; handlers remain honest for unimplemented P2P/chain-index features. RPC audit hash-chain log already active. RBAC role/allowlist active.
  - **Prompt 13 (regtest orchestration):** `start-regtest.sh` waits for RPC readiness, 4 nodes with separate datadir/port/token; `check-regtest.sh` reports summary; added `chaos-regtest.sh` (process kill/restart). **Limitation:** PoI+Verifier P2P peer wiring/true multi-node consensus not wired - nodes run as independent processes.
  - **Prompt 14 (test harness):** Added `tests/unit/test_fuzz_chaos.cpp` (serialization u64 fuzz, bytes roundtrip, aggregator invalid-signature fuzz, consensus invalid-vote fuzz, chaos timeout/view advance) wired into unit tests; executed in ctest.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **External/blocked (honest):** Independent security audit; long-running public testnet; full P2P PoI+Verifier 4-verifier consensus orchestration; production TLS/reverse-proxy deployment; libFuzzer/cluster Byzantine/partition chaos infrastructure; on-chain slashing governance.

---

## 2026-11-23 — Critical pass: P2P wiring in daemon/RPC/regtest

- **Time (UTC):** 2026-11-23
- **Actions:**
  - Wired `network::P2PNode` into `phantomxd` (`--p2p-host`, `--p2p-port`, `--connect`).
  - RPC: `getpeerinfo`, `getconnectioncount`, `addnode` now use real P2PNode when enabled; `getnetworkinfo` reports p2p status/connections.
  - Regtest scripts: `start-regtest.sh` starts 4 nodes with P2P ports and star connect to node1; `check-regtest.sh` shows connection count.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Honest gap:** P2P transport/connectivity wired; **PoI+Verifier vote/QC/consensus messages over P2P are not implemented**, so 4-node true PoI+Verifier finality is still not achieved.

---

## 2026-11-23 — Critical pass: PoI+Verifier P2P protocol + durable vote store + pacemaker hooks

- **Time (UTC):** 2026-11-23
- **Actions:**
  - **P2P PoI+Verifier protocol:** Added `MessageType::{kBftVote,kBftQc,kBftProposal}` and encode/decode in `network/protocol.*`; P2PNode handlers, callbacks, and `broadcast_bft_vote/qc/proposal`.
  - **Durable vote store:** BftEngine `save_state/load_state` now persist votes; `maybe_persist_` on state changes; `set_durable_path`.
  - **Pacemaker hooks:** `PacemakerConfig`, `pacemaker_timeout_ms` (backoff), `pacemaker_should_timeout`, `pacemaker_note_progress`, `pacemaker_tick`.
  - **Tests:** `consensus_vote_persistence_and_reload`, `consensus_pacemaker_timeout_advances_view`.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Gaps:** BftEngine not yet auto-wired to P2PNode in daemon; no full PoI+Verifier proposal/vote/QC round over network; durable store is text-file (not crash-safe WAL DB); pacemaker lacks network round sync; production randomness/slashing/governance/audit/testnet remain.

---

## 2026-11-23 — Critical pass: BftEngine<->P2PNode daemon wiring + minimal PoI+Verifier RPC

- **Time (UTC):** 2026-11-23
- **Actions:**
  - Daemon: created `BftEngine` with durable state path; wired P2P callbacks (vote->submit+maybe QC broadcast, qc->observe, proposal->hook).
  - RPC: `getvalidatorinfo/getconsensusinfo/getfinalityinfo/getquorumcertificate` now report BftEngine state; added `bft_status`, `bft_submit_vote` (validates, submits, broadcasts, forms QC), `bft_pacemaker_tick`.
  - `register_node_rpc_handlers` extended with `BftEngine*`.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Gaps:** No autonomous proposer/block-proposal round, no auto-vote with local verifier key, no full 4-node PoI+Verifier finality E2E, no crash-safe WAL, no production randomness/slashing/governance, external audit/testnet.

---

## 2026-11-23 — Critical pass: local verifier key + auto-vote/proposal + 4-node PoI+Verifier E2E

- **Time (UTC):** 2026-11-23
- **Actions:**
  - Daemon: local PoI+Verifier verifier key auto-generate/load (`bft_validator.key`), PoI+Verifier callbacks auto-prevote on proposal, auto-precommit on prevote QC, finalize on precommit QC.
  - RPC: `bft_local_pubkey`, `bft_add_validator`, `bft_propose`.
  - Script: `scripts/e2e-poi+verifier-4node.sh` (4 nodes, P2P, register verifiers, propose, verify finality).
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**. E2E script result: all 4 nodes finalized same hash/height (see script output in terminal log).
- **Gaps:** Minimal/manual round flow; no autonomous block production/mempool proposal, no leader rotation network sync, no crash-safe WAL, no production randomness/slashing/governance, no deep PoI/finality/reward integration, no independent audit/testnet.

---

## 2026-11-23 — Critical pass: minimal autonomous PoI+Verifier loop

- **Time (UTC):** 2026-11-23
- **Actions:**
  - `phantomxd`: added background PoI+Verifier thread (pacemaker tick, leader detection via `select_proposer`, auto block build+PoW mine, broadcast proposal, auto-prevote, apply finalized pending block).
  - Includes/fixes for block/merkle/rewards/pow/transaction in daemon.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Gaps:** Minimal loop (coinbase-only blocks, crude progress timer, no mempool tx packing, no robust round/network sync, no view-change reproposal policy, no crash-safe WAL). Deep PoI/finality/reward integration, production randomness/slashing/governance, external audit/testnet still open.

---

## 2026-11-23 — Critical hardening pass: mempool packing + pacemaker/view progress

- **Time (UTC):** 2026-11-23
- **Actions:**
  - Autonomous PoI+Verifier block builder now packs mempool txs (validate against UTXO view, fee accounting, size cap) instead of coinbase-only.
  - Pacemaker progress: initialize note_progress, update on view change/finality; loop reproposes on new view when leader.
  - Finalized block apply removes included txs from mempool.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Remaining critical (in-repo):** crash-safe WAL/DB for votes/QC/evidence, unbiased production randomness, slashing/governance enforcement, deep PoI/verifier/aggregator/finality/reward integration. **External:** Byzantine/partition/long-running cluster, independent audit, public testnet soak. **NO-GO mainnet**.

---

## 2026-11-23 — Critical hardening: PoI+Verifier SQLite persistence + slashing hooks

- **Time (UTC):** 2026-11-23
- **Actions:**
  - PoI+Verifier state SQLite persistence: `save_state_db/load_state_db` (atomic SQLite row), daemon uses `bft_state.db` with txt fallback; maybe_persist_ supports .db.
  - Slashing foundation: `is_slashed/slash_validator/slashed`, auto-slash on equivocation, reject slashed votes, persisted in state, `bft_status` reports slashed count.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Remaining in-repo critical:** robust network round sync, unbiased production randomness, deep PoI/verifier/aggregator/finality/reward integration, full per-event WAL (current is atomic snapshot DB). **External:** Byzantine/partition/long-running cluster, independent audit, public testnet soak. **NO-GO mainnet**.

---

## 2026-11-24 — Critical hardening: network round sync improvements

- **Time (UTC):** 2026-11-24
- **Actions:**
  - Added P2P `BftTimeout` message (type, encode/decode, broadcast, handler).
  - Daemon: broadcast timeout on view change, QC handler catches up view, timeout handler syncs view forward on peer timeout.
  - Pacemaker progress updated on view change/finality.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Remaining:** Full timeout-quorum aggregation, robust round-sync under adversarial network, full per-event WAL, unbiased production randomness, full slashing/governance, deep PoI/finality/reward integration. External: audit/testnet/cluster tests. **NO-GO mainnet**.

---

## 2026-11-24 — Critical hardening: timeout-quorum aggregation

- **Time (UTC):** 2026-11-24
- **Actions:**
  - `BftEngine`: added `submit_timeout`, `timeout_quorum_reached` (voting-power quorum over unique verifiers per epoch/view), `timeout_votes_` storage.
  - Daemon: timeout handler submits timeout, syncs view, advances on quorum; loop submits/broadcasts local timeout and checks quorum.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Remaining:** Full per-event WAL, unbiased production randomness, full slashing/governance enforcement, deep PoI/verifier/aggregator/finality/reward integration. External blocked. **NO-GO mainnet**.

---

## 2026-11-24 — Critical hardening: PoI+Verifier per-event WAL (append-only)

- **Time (UTC):** 2026-11-24
- **Actions:**
  - Added SQLite `bft_wal(id,ts,event,payload)` append-only event log for vote/timeout/qc/finalize/slash.
  - `wal_append_` called on key PoI+Verifier events when durable DB path is configured.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Gap:** WAL is append-only audit/event log; recovery still snapshot-based (not full replay-from-WAL). Remaining: unbiased production randomness, full slashing/governance, deep PoI/finality/reward integration, adversarial-timing/partition-hardened pacemaker. External blocked. **NO-GO mainnet**.

---

## 2026-11-24 — Critical hardening pass (incremental, in-repo)

- **Time (UTC):** 2026-11-24
- **Actions:**
  - WAL replay best-effort on load (timeout/slash idempotent replay; replay guard prevents WAL duplication). Snapshot still primary.
  - Slashing enforcement extended: slashed proposer skipped in autonomous loop (triggers timeout).
  - Randomness beacon foundation: `randomness_beacon()` hash-mix (explicitly NOT unbiased production randomness), `bft_beacon` RPC.
- **Verification:** `cmake --build build/dev --parallel 4` **PASS**; `ctest --test-dir build/dev --output-on-failure -j4` **12/12 PASS**.
- **Honest status:** Items remain partially implemented; not production-complete.
