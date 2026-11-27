# Rancangan: Default Datadir, `phantomx.conf`, and Network via Config

- Status: Draft desain (belum diimplementasikan)
- Date: 2026-11-25
- Ruang lingkup: `phantomxd`, `phantomx-cli`, `phantomx-qt`, `phantomx-wallet`

## 1. Tujuan

Menyederhanakan pengalaman menjalankan PhantomX agar mirip coin on umumnya:

- User not wajib mengisi `--datadir`.
- Data and config punya lokasi default that mudah diakses.
- `phantomx.conf` dibuat otomatis when pertama kali dijalankan.
- Build only menghasilkan 4 binary; network dipilih lewat config (`testnet=1` / `regtest=1`) or argumen CLI.

## 2. Non-goal

- Menggabungkan logika domain aggregator/verifier/verifier/pool menjadi satu modul.
- Mengubah format wire P2P, konsensus, or struktur DB chain.
- Menghapus `--datadir` / `--config`; keduanya tetap jadi override.

## 3. Keputusan Desain

### 3.1 Default datadir per network

Default mengikuti pola coin umum: data di direktori user, dipisah per network agar not tercampur. Network dipilih by config/argumen, tetapi binary tetap sama.

| Network | Linux | Windows | macOS (opsional) |
|---|---|---|---|
| Mainnet | `~/.phantomx/` | `%APPDATA%\PhantomX\` | `~/Library/Application Support/PhantomX/` |
| Testnet | `~/.phantomx/testnet/` | `%APPDATA%\PhantomX\testnet\` | `~/Library/Application Support/PhantomX/testnet/` |
| Regtest | `~/.phantomx/regtest/` | `%APPDATA%\PhantomX\regtest\` | `~/Library/Application Support/PhantomX/regtest/` |

Notes:

- `~` diselesaikan from `$HOME`. If `$HOME` kosong, fallback ke direktori kerja currently and log warning.
- Windows uses `%APPDATA%`; fallback `%USERPROFILE%\AppData\Roaming\PhantomX` bila env not ada.
- Untuk root Linux, default **bukan** `./`. Gunakan home root (`/root/.phantomx`) or kebijakan service `/var/lib/phantomx`. Rekomendasi awal: ikut home that resolved; deployment systemd mendokumentasikan `--datadir /var/lib/phantomx`.
- `--datadir <path>` selalu menang and melewati default.
- If `testnet=1` or `regtest=1` aktif, datadir default otomatis berpindah ke subdirektori network that sesuai, kecuali `--datadir` di-set eksplisit.

### 3.2 Layout datadir

```text
<datadir>/
  phantomx.conf
  chain.db
  bft_wal.bin
  logs/
  wallet/
    wallet.keystore
  poi/            (opsional, saat service PoI aktif)
  pool/           (opsional, saat service pool aktif)
```

Binary that sama uses datadir default network masing-masing, bukan datadir that sama.

### 3.3 Seleksi network

Network dipilih with precedence:

1. Argumen CLI: `--regtest` or `--testnet` (if didukung)
2. Config: `regtest=1` or `testnet=1`
3. Default: `mainnet`

Aturan:

- `regtest=1` and `testnet=1` not boleh aktif bersamaan; if keduanya aktif, startup gagal with error config that jelas.
- `chain=main|testnet|regtest` boleh ditambahkan as alias that lebih eksplisit, tetapi `testnet/regtest` gaya Bitcoin tetap didukung.
- When the network is not mainnet, UI/CLI/RPC/log must clearly display the active network to prevent wrong-network usage.

### 3.4 Auto-create `phantomx.conf`

Saat startup:

1. Resolve network efektif (CLI/config/default).
2. Resolve datadir efektif berdasarkan network.
3. `create_directories(datadir)`.
4. If `<datadir>/phantomx.conf` belum ada, tulis template `phantomx.conf` berisi opsi that dikomentari (`#`) as referensi.
5. Load config.
6. Terapkan precedence: **CLI args > config file > default internal**.

Aturan:

- Jangan overwrite config that already ada.
- Semua entri template dikomentari agar daemon not langsung membaca/mengaktifkannya; daemon uses default internal until user meng-uncomment.
- Linux: permission file `0600` if memungkinkan.
- `rpcpassword`/`rpctoken` di template can diisi nilai acak when pertama dibuat (tetap dikomentari), agar user punya secret siap pakai when mengaktifkan RPC.
- Log path config that dibuat, tapi jangan log nilai secret.

### 3.5 Format `phantomx.conf`

Gunakan format key=value gaya Bitcoin, satu key per baris, `#` for komentar. Parser `Config::load_file` currently already mendukung JSON and fallback key=value; perlu dipastikan kompatibilitas penuh for `.conf`.

Template awal (semua baris konfigurasi dikomentari agar not langsung dibaca daemon; daemon uses default internal. User tinggal meng-uncomment and mengubah nilai that dibutuhkan):

```conf
# PhantomX configuration
# This file is created automatically. All options are commented out (#) so they are not active by default.
# Remove the # at the beginning of a line to enable that option.

# =========================
# Network
# =========================
# Choose one. Default: mainnet.
#testnet=1
#regtest=1

# =========================
# Service
# =========================
#server=1
#listen=1

# =========================
# RPC
# =========================
#rpcbind=0.0.0.0
#rpcport=8741          # mainnet; testnet/regtest pakai port network masing-masing
#rpcallowip=127.0.0.1
#rpcuser=phantomxrpc
#rpcpassword=__AUTO_GENERATED__
#rpctoken=__AUTO_GENERATED__

# =========================
# P2P
# =========================
#port=18444            # mainnet; testnet/regtest pakai port network masing-masing

# =========================
# Logging
# =========================
#loglevel=info
```

> Notes keamanan: `rpcbind=0.0.0.0` membuka RPC ke semua interface. Jangan aktifkan tanpa `rpcallowip`/firewall/auth that benar. Default internal that aman tetap `127.0.0.1`.

Kunci that direncanakan (default internal; template ditulis in bentuk komentar):

| Key | Tipe | Default internal | Contoh di template | Notes |
|---|---|---|---|---|
| `testnet` | bool/int | `0` | `#testnet=1` | Aktifkan testnet. Tidak boleh bersamaan with `regtest=1`. |
| `regtest` | bool/int | `0` | `#regtest=1` | Aktifkan regtest. Tidak boleh bersamaan with `testnet=1`. |
| `chain` | string | `main` | - | Alias opsional: `main`, `testnet`, `regtest`. |
| `server` | bool/int | `1` | `#server=1` | Aktifkan RPC server. |
| `listen` | bool/int | `1` | `#listen=1` | Aktifkan P2P listen. |
| `rpcbind` | string | `0.0.0.0`* | `#rpcbind=0.0.0.0` | *Permintaan user: default bind semua interface. Risiko keamanan tinggi; wajib batasi `rpcallowip`/firewall. Alternatif aman: `127.0.0.1`. |
| `rpcport` | int | network-specific | `#rpcport=8741` | Mainnet/testnet/regtest berbeda. |
| `rpcuser` | string | `phantomxrpc` | `#rpcuser=phantomxrpc` | Untuk auth gaya user/pass (target). |
| `rpcpassword` | string | random | `#rpcpassword=__AUTO_GENERATED__` | Secret; auto-generate when template dibuat. |
| `rpctoken` | string | random/opsional | `#rpctoken=__AUTO_GENERATED__` | Kompatibilitas with auth token currently. |
| `rpcallowip` | string/list | `127.0.0.1` | `#rpcallowip=127.0.0.1` | Harus di-enforce di RPC layer. |
| `port` | int | network-specific | `#port=18444` | P2P port. |
| `datadir` | path | default resolved | - | Opsional; CLI `--datadir` tetap override. |
| `loglevel` | string | `info` | `#loglevel=info` | debug/info/warn/error. |

### 3.6 Binary that dihasilkan

Build only menghasilkan 4 binary:

```text
phantomxd
phantomx-cli
phantomx-qt
phantomx-wallet
```

Network not ditentukan by nama binary, melainkan by config/argumen:

- Mainnet: default, or `testnet=0` and `regtest=0`
- Testnet: `testnet=1`
- Regtest: `regtest=1`

Contoh:

```conf
# ~/.phantomx/testnet/phantomx.conf
testnet=1
server=1
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
```

### 3.7 Default per network

| Network | Selector config | Datadir default | RPC port default | P2P port default |
|---|---|---|---:|---:|
| Mainnet | default | `~/.phantomx` | 8741* | 18444* |
| Testnet | `testnet=1` | `~/.phantomx/testnet` | 18741* | 28444* |
| Regtest | `regtest=1` | `~/.phantomx/regtest` | 18841* | 38444* |

\* Nilai port final must dikunci when implementasi sesuai konvensi repo/config that already ada.

`phantomx-cli` default `--rpc` mengikuti network aktif from config that dibaca. `phantomx-wallet` and `phantomx-qt` uses datadir/wallet network that sesuai.

## 4. Dampak Komponen

### 4.1 Common path/config

File baru/disesuaikan:

- `include/phantomx/common/paths.hpp`
- `src/common/paths.cpp`
- `include/phantomx/common/config.hpp`
- `src/common/config.cpp`

Tanggung jawab:

- Resolve home/appdata.
- Resolve datadir default per network.
- Buat config template bila belum ada.
- Parse key=value `.conf` with aman.
- Sediakan helper masking secret when logging.

### 4.2 Daemon

- `src/daemon/main.cpp`
- `src/rpc/http_server.cpp` / `src/rpc/rpc.cpp` bila perlu `rpcallowip`.

Perubahan:

- Ganti default `--datadir .phantomx` with default resolved.
- Network dipilih from `testnet/regtest/chain` di config or argumen CLI.
- Auto-create `phantomx.conf`.
- Baca `testnet/regtest/chain/rpcbind/rpcport/rpcallowip/rpctoken/rpcpassword/port/listen/loglevel`.
- Enforce `rpcallowip` sebelum memproses request RPC.

### 4.3 CLI

- `apps/phantomx-cli/main.cpp`

Perubahan:

- Default RPC endpoint from network aktif.
- Bisa baca `testnet/regtest/rpcport/rpctoken/rpcpassword` from `phantomx.conf` datadir default bila argumen not diberikan.
- Jadikan remote HTTP RPC client ke daemon as jalur utama; mode in-process only dev/debug bila dipertahankan.

### 4.4 Wallet / Qt

- `apps/phantomx-wallet/main.cpp`
- `src/wallet/main.cpp`
- `apps/phantomx-qt/*`

Perubahan:

- Default datadir/wallet ke `<datadir>/wallet`.
- Network-aware defaults berdasarkan config/argumen, bukan nama binary.
- Jangan pernah tulis seed/private key ke log.

### 4.5 Build/install

- `CMakeLists.txt`
- `apps/phantomx-qt/CMakeLists.txt`

Perubahan:

- Pertahankan 4 target binary utama, tanpa suffix network.
- `--version` displays binary name, version, and active network at runtime.
- Install/packaging sederhana: 4 binary + config/docs.

## 5. Kompatibilitas and Migrasi

- `--datadir` lama tetap bekerja.
- If data lama ada di `./.phantomx` (CWD) and datadir default baru belum ada:
  - Rekomendasi: tampilkan warning + instruksi migrasi, jangan auto-move diam-diam.
- `phantomx.conf` baru not overwrite config existing.
- JSON config lama tetap didukung if memungkinkan; `.conf` menjadi default baru.

## 6. Keamanan

- Template config ditulis with semua opsi dikomentari; daemon uses default internal until user mengaktifkan opsi secara eksplisit.
- `rpcbind` default internal diminta `0.0.0.0`. Ini **berisiko** if RPC terekspos publik. Implementasi must tetap meng-enforce `rpcallowip`, auth token/password, and dokumentasi firewall that jelas. Pertimbangkan default aman `127.0.0.1` if not ada kebutuhan publik.
- `rpcallowip` default only localhost.
- Secret RPC auto-generate secara random; permission file dibatasi (`0600` Linux bila memungkinkan).
- Jangan log `rpcpassword`, `rpctoken`, seed, private key.
- Pisahkan datadir per network for mencegah salah chain/db.
- Deployment root/service didokumentasikan with user terpisah and datadir sistem.

## 7. Pengujian

### Unit

- Path resolver: Linux home, HOME kosong, Windows APPDATA, override `--datadir`.
- Config generator: buat file if missing, not overwrite, permission.
- Config parser: key=value, komentar, spasi, nilai kosong, list `rpcallowip`.

### Integration

- `phantomxd` tanpa argumen membuat datadir default + `phantomx.conf` mainnet.
- `phantomxd` with `testnet=1` di config menulis ke datadir testnet and uses port testnet.
- `phantomxd` with `regtest=1` di config menulis ke datadir regtest and uses port regtest.
- CLI membaca config default network and bisa konek ke RPC daemon.
- `rpcallowip` menolak IP non-allowed.
- `testnet=1` and `regtest=1` bersamaan menghasilkan error config that jelas.

### Build/smoke

- CMake configure/build menghasilkan 4 binary.
- `--help`/`--version` displays the binary name; runtime displays the active network.
- Smoke run daemon with temp HOME/APPDATA for mainnet/testnet/regtest.

## 8. Risiko

- **Campur data network** if default datadir salah: mitigasi with network-aware path and test.
- **Breaking behavior** bagi user that bergantung on `.phantomx` di CWD: mitigasi warning migrasi.
- **Eksposur RPC** if `rpcallowip` not di-enforce: mitigasi enforcement + test.
- **Salah pilih network** because binary sama: mitigasi with log/banner network aktif, datadir network terpisah, and error if `testnet/regtest` konflik.
- **Konfig lama/not lengkap**: mitigasi auto-create template, validasi key, and default aman.

## 9. Urutan Implementasi

1. Common: `paths` + config template/generator + tests.
2. Daemon: default datadir/config auto-create + RPC config enforcement.
3. CLI: remote RPC client/config discovery per network.
4. Wallet/Qt: network-aware datadir and defaults.
5. CMake: pastikan 4 binary utama + install/help/version.
6. Docs: README, run guides, config reference, migration notes.
7. Verifikasi: build, unit/integration, smoke.

## 10. Keputusan that Perlu Dikonfirmasi

1. Port resmi mainnet/testnet/regtest for P2P and RPC.
2. Default when dijalankan as root: home root or `/var/lib/phantomx`.
3. Apakah `rpcallowip` mendukung CIDR/subnet or only IP tunggal/list IP.
4. Apakah config lama JSON tetap didukung penuh, or `.conf` menjadi format utama with JSON only legacy.
5. Apakah seleksi network cukup lewat config (`testnet=1/regtest=1`), or tetap perlu flag CLI `--testnet/--regtest` as override nyaman.
6. Konfirmasi keamanan `rpcbind` default internal `0.0.0.0`: tetap seperti permintaan, or default aman `127.0.0.1` with contoh `0.0.0.0` di template.
