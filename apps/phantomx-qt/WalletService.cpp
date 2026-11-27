#include "WalletService.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSysInfo>
#include <QTextStream>

#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/common/config.hpp"
#include "phantomx/common/paths.hpp"
#include "phantomx/common/serialization.hpp"
#include "phantomx/common/types.hpp"
#include "phantomx/database/sqlite_db.hpp"
#include "phantomx/primitives/bech32m.hpp"
#include "phantomx/primitives/transaction.hpp"
#include "phantomx/utxo/tx_builder.hpp"
#include "phantomx/utxo/validation.hpp"
#include "phantomx/wallet/keystore.hpp"

#include <jansson.h>

namespace {

QString errText(const phantomx::Error& e) { return QString::fromStdString(e.to_string()); }
QString toQString(const std::string& s) { return QString::fromStdString(s); }

quint64 unitMultiplier(const QString& unit) {
  const auto u = unit.trimmed().toUpper();
  if (u == "TB") return 1024ULL * 1024ULL * 1024ULL * 1024ULL;
  if (u == "GB") return 1024ULL * 1024ULL * 1024ULL;
  return 1024ULL * 1024ULL; // MB default
}

QString defaultNodeRpcForNetwork(phantomx::Network n) {
  return QString("http://127.0.0.1:%1").arg(phantomx::default_rpc_port(n));
}

using HeightSample = std::pair<qint64, std::uint32_t>;

void pruneSamples(std::deque<HeightSample>& samples, qint64 now, qint64 windowMs, std::size_t maxSamples) {
  while (samples.size() > maxSamples) samples.pop_front();
  while (!samples.empty() && (now - samples.front().first) > windowMs) samples.pop_front();
}

double rollingBlocksPerSecond(const std::deque<HeightSample>& samples, qint64 minSpanMs) {
  if (samples.size() < 2) return -1.0;
  const auto& first = samples.front();
  const auto& last = samples.back();
  const qint64 dt = last.first - first.first;
  if (dt < minSpanMs) return -1.0;
  const qint64 dh = last.second >= first.second ? static_cast<qint64>(last.second - first.second) : 0;
  if (dh <= 0) return -1.0;
  return static_cast<double>(dh) * 1000.0 / static_cast<double>(dt);
}

}  // namespace

quint64 MiningSettings::bandwidthBytes() const { return bandwidthValue * unitMultiplier(bandwidthUnit); }
QString MiningSettings::displayLimit() const { return QString::number(bandwidthValue) + " " + bandwidthUnit.toUpper(); }

WalletService::WalletService(QObject* parent) : QObject(parent) {
  (void)phantomx::crypto::init();
  initFromDefaults();
  rebuildWallet();
  (void)ensureWallet();
  audit("startup", QStringLiteral("network=%1 datadir=%2 rpc=%3").arg(network_, datadir_, node_rpc_));
}

void WalletService::initFromDefaults() {
  const auto n = phantomx::Network::kTestnet;
  network_ = QString::fromStdString(std::string(phantomx::network_name(n)));
  datadir_ = QString::fromStdString(phantomx::default_datadir(n).string());
  config_path_ = QString::fromStdString(phantomx::default_config_path(phantomx::default_datadir(n)).string());
  node_rpc_ = defaultNodeRpcForNetwork(n);
  loadConfig();
  applyNetworkDefaults();
}

void WalletService::loadConfig(bool preserve_network) {
  QSettings s(QString::fromStdString((std::filesystem::path(datadir_.toStdString()) / "qt-wallet.ini").string()), QSettings::IniFormat);
  mining_.enabled = s.value("mining/enabled", false).toBool();
  mining_.bandwidthValue = s.value("mining/bandwidthValue", 100).toULongLong();
  mining_.bandwidthUnit = s.value("mining/bandwidthUnit", "MB").toString().toUpper();
  mining_.domain = s.value("mining/domain", "").toString().trimmed();
  if (mining_.bandwidthUnit != "MB" && mining_.bandwidthUnit != "GB" && mining_.bandwidthUnit != "TB") mining_.bandwidthUnit = "MB";
  if (mining_.bandwidthValue == 0) mining_.bandwidthValue = 1;
  poi_running_ = mining_.enabled;
  treasury_address_ = s.value("wallet/treasury_address", "").toString().trimmed();
  auto_sweep_treasury_ = s.value("wallet/auto_sweep_treasury", false).toBool();

  const auto cfgPath = std::filesystem::path(config_path_.toStdString());
  if (!std::filesystem::exists(cfgPath)) return;
  auto loaded = phantomx::Config::load_file(cfgPath);
  if (!loaded.ok()) return;
  const auto& cfg = loaded.value();
  auto testnet = cfg.get("testnet");
  auto regtest = cfg.get("regtest");
  auto chain = cfg.get("chain");
  const bool conf_has_network = testnet.has_value() || regtest.has_value() || chain.has_value();
  auto nr = phantomx::resolve_network(false, false, testnet, regtest, chain);
  if (!preserve_network) {
    if (conf_has_network && nr.ok()) {
      network_ = QString::fromStdString(std::string(phantomx::network_name(nr.value())));
      datadir_ = QString::fromStdString(phantomx::default_datadir(nr.value()).string());
      config_path_ = QString::fromStdString(phantomx::default_config_path(phantomx::default_datadir(nr.value())).string());
    } else {
      const auto savedNet = s.value("network/name", "").toString().trimmed();
      if (!savedNet.isEmpty()) {
        auto sn = phantomx::network_from_string(savedNet.toStdString());
        if (sn.has_value()) {
          network_ = QString::fromStdString(std::string(phantomx::network_name(*sn)));
          datadir_ = QString::fromStdString(phantomx::default_datadir(*sn).string());
          config_path_ = QString::fromStdString(phantomx::default_config_path(phantomx::default_datadir(*sn)).string());
        }
      }
    }
  }
  auto rp = cfg.get("rpcport");
  auto rb = cfg.get("rpcbind");
  const auto host = rb.has_value() && *rb != "0.0.0.0" ? QString::fromStdString(*rb) : QStringLiteral("127.0.0.1");
  if (rp.has_value()) node_rpc_ = QString("http://%1:%2").arg(host, QString::fromStdString(*rp));
  if (auto rt = cfg.get("rpctoken"); rt.has_value() && !rt->empty()) rpc_token_ = QString::fromStdString(*rt);
  else if (auto pw = cfg.get("rpcpassword"); pw.has_value() && !pw->empty()) rpc_token_ = QString::fromStdString(*pw);
  if (auto tr = cfg.get("treasury"); tr.has_value() && treasury_address_.isEmpty()) treasury_address_ = QString::fromStdString(*tr);
  if (auto pd = cfg.get("poi_domain"); pd.has_value() && mining_.domain.isEmpty()) mining_.domain = QString::fromStdString(*pd);
}

void WalletService::applyNetworkDefaults() {
  auto n = phantomx::network_from_string(network_.toStdString());
  if (!n.has_value()) n = phantomx::Network::kTestnet;
  if (datadir_.isEmpty()) datadir_ = QString::fromStdString(phantomx::default_datadir(*n).string());
  if (config_path_.isEmpty()) config_path_ = QString::fromStdString(phantomx::default_config_path(std::filesystem::path(datadir_.toStdString())).string());
  if (node_rpc_.isEmpty()) node_rpc_ = defaultNodeRpcForNetwork(*n);
}

void WalletService::rebuildWallet() {
  applyNetworkDefaults();
  const auto keystore = std::filesystem::path(keystorePath().toStdString());
  std::error_code ec;
  std::filesystem::create_directories(keystore.parent_path(), ec);
  wallet_ = phantomx::wallet::Wallet(keystore.string(), node_rpc_.toStdString());
  wallet_.set_network(network_.toStdString());
  wallet_.set_rpc_token(rpc_token_.toStdString());
  (void)wallet_.open();
  emit walletChanged();
  emit networkChanged();
}

QString WalletService::keystorePath() const {
  return QString::fromStdString((std::filesystem::path(datadir_.toStdString()) / "wallet" / "wallet.keystore").string());
}

bool WalletService::ensureWallet(QString* error) {
  if (wallet_.has_wallet()) return true;
  const auto p = std::filesystem::path(keystorePath().toStdString());
  if (std::filesystem::exists(p)) {
    // Keystore exists but no key loaded - try open, don't auto-overwrite.
    auto opened = wallet_.open();
    if (opened.ok() && wallet_.has_wallet()) { emit walletChanged(); return true; }
    if (error) *error = opened.ok() ? tr("Keystore exists but no wallet key found.") : errText(opened.error());
    return false;
  }
  auto created = wallet_.create();
  if (!created.ok()) {
    if (error) *error = errText(created.error());
    emit message(errText(created.error()), true);
    return false;
  }
  auto_created_wallet_ = true;
  audit("auto_create_wallet", QString::fromStdString(p.string()));
  emit message(tr("Default wallet created automatically. Please backup your keystore."), false);
  emit walletChanged();
  return true;
}

void WalletService::setNetwork(const QString& network) {
  const auto n = phantomx::network_from_string(network.toStdString());
  if (!n.has_value()) return;
  network_ = QString::fromStdString(std::string(phantomx::network_name(*n)));
  datadir_ = QString::fromStdString(phantomx::default_datadir(*n).string());
  config_path_ = QString::fromStdString(phantomx::default_config_path(phantomx::default_datadir(*n)).string());
  node_rpc_ = defaultNodeRpcForNetwork(*n);
  sync_samples_.clear();
  wallet_scan_samples_.clear();
  wallet_scan_available_ = false;
  wallet_scan_syncing_ = false;
  wallet_scan_blocks_per_second_ = -1.0;
  wallet_scan_eta_seconds_ = -1;
  node_blocks_per_second_ = -1.0;
  node_eta_seconds_ = -1;

  {
    QSettings s(QString::fromStdString((std::filesystem::path(datadir_.toStdString()) / "qt-wallet.ini").string()), QSettings::IniFormat);
    s.setValue("network/name", network_);
  }
  {
    const auto mainIni = phantomx::default_datadir(phantomx::Network::kMainnet) / "qt-wallet.ini";
    QSettings s(QString::fromStdString(mainIni.string()), QSettings::IniFormat);
    s.setValue("network/name", network_);
  }
  loadConfig(true);
  applyNetworkDefaults();
  rebuildWallet();
  (void)ensureWallet();
  audit("set_network", network_);
}

void WalletService::setNodeRpc(const QString& rpc) {
  if (rpc.trimmed().isEmpty() || node_rpc_ == rpc.trimmed()) return;
  node_rpc_ = rpc.trimmed();
  rebuildWallet();
  (void)ensureWallet();
  audit("set_rpc", node_rpc_);
}

void WalletService::setDatadir(const QString& datadir) {
  if (datadir.trimmed().isEmpty() || datadir_ == datadir.trimmed()) return;
  datadir_ = datadir.trimmed();
  config_path_ = QString::fromStdString((std::filesystem::path(datadir_.toStdString()) / "phantomx.conf").string());
  sync_samples_.clear();
  wallet_scan_samples_.clear();
  wallet_scan_available_ = false;
  wallet_scan_syncing_ = false;
  wallet_scan_blocks_per_second_ = -1.0;
  wallet_scan_eta_seconds_ = -1;
  node_blocks_per_second_ = -1.0;
  node_eta_seconds_ = -1;

  loadConfig();
  rebuildWallet();
  (void)ensureWallet();
  audit("set_datadir", datadir_);
}

bool WalletService::createWallet(QString* error) {
  rebuildWallet();
  auto created = wallet_.create();
  if (!created.ok()) { if (error) *error = errText(created.error()); emit message(errText(created.error()), true); return false; }
  auto_created_wallet_ = false;
  emit message(tr("Wallet created. Back up your keystore."), false);
  audit("create_wallet", QString::fromStdString(wallet_.keystore_path().string()));
  emit walletChanged();
  return true;
}

bool WalletService::loadWallet(QString* error) {
  rebuildWallet();
  auto opened = wallet_.open();
  if (!opened.ok()) { if (error) *error = errText(opened.error()); emit message(errText(opened.error()), true); return false; }
  if (!wallet_.has_wallet()) { if (error) *error = tr("No wallet key found in keystore."); emit message(*error, true); return false; }
  emit message(tr("Wallet loaded."), false);
  audit("load_wallet", QString::fromStdString(wallet_.keystore_path().string()));
  emit walletChanged();
  return true;
}

bool WalletService::importWallet(const QString& secret_hex, QString* error) {
  rebuildWallet();
  auto imported = wallet_.import_secret(secret_hex.trimmed().toStdString());
  if (!imported.ok()) { if (error) *error = errText(imported.error()); emit message(errText(imported.error()), true); return false; }
  auto_created_wallet_ = false;
  emit message(tr("Wallet imported. Secret key stays local."), false);
  audit("import_wallet", QString::fromStdString(wallet_.keystore_path().string()));
  emit walletChanged();
  return true;
}

bool WalletService::importWalletFile(const QString& sourcePath, QString* error) {
  const QString trimmed = sourcePath.trimmed();
  if (trimmed.isEmpty()) {
    if (error) *error = tr("Keystore file path is empty.");
    return false;
  }

  const std::filesystem::path src(trimmed.toStdString());
  std::error_code ec;
  if (!std::filesystem::is_regular_file(src, ec)) {
    const QString msg = tr("Keystore file not found: %1").arg(trimmed);
    if (error) *error = msg;
    emit message(msg, true);
    return false;
  }

  // Validate before replacing the active keystore so a bad file cannot destroy it.
  auto checked = phantomx::wallet::Keystore::load(src);
  if (!checked.ok()) {
    const QString msg = errText(checked.error());
    if (error) *error = msg;
    emit message(msg, true);
    return false;
  }

  rebuildWallet();
  const auto dest = wallet_.keystore_path();
  bool sameFile = false;
  if (std::filesystem::exists(dest, ec)) {
    std::error_code eq;
    sameFile = std::filesystem::equivalent(src, dest, eq) && !eq;
  }

  if (!sameFile) {
    if (dest.has_parent_path()) {
      std::filesystem::create_directories(dest.parent_path(), ec);
      if (ec) {
        const QString msg = tr("Unable to create keystore directory: %1").arg(QString::fromStdString(ec.message()));
        if (error) *error = msg;
        emit message(msg, true);
        return false;
      }
    }
    std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      const QString msg = tr("Keystore import failed: %1").arg(QString::fromStdString(ec.message()));
      if (error) *error = msg;
      emit message(msg, true);
      return false;
    }
  }

  auto opened = wallet_.open();
  if (!opened.ok()) {
    const QString msg = errText(opened.error());
    if (error) *error = msg;
    emit message(msg, true);
    return false;
  }
  if (!wallet_.has_wallet()) {
    const QString msg = tr("No wallet key found in the selected keystore.");
    if (error) *error = msg;
    emit message(msg, true);
    return false;
  }

  auto_created_wallet_ = false;
  emit message(tr("Keystore file imported. Secret keys stay local."), false);
  audit("import_wallet_file", QString::fromStdString(dest.string()));
  emit walletChanged();
  return true;
}

bool WalletService::backupWallet(const QString& destinationPath, QString* error) {
  if (destinationPath.trimmed().isEmpty()) { if (error) *error = tr("Backup path is empty."); return false; }
  auto r = wallet_.backup(std::filesystem::path(destinationPath.toStdString()));
  if (!r.ok()) { if (error) *error = errText(r.error()); emit message(errText(r.error()), true); return false; }
  audit("backup_wallet", destinationPath);
  emit message(tr("Wallet backup saved."), false);
  return true;
}

QStringList WalletService::accountLabels() const {
  QStringList out;
  for (const auto& l : wallet_.account_labels()) out << toQString(l);
  if (out.isEmpty() && wallet_.has_wallet()) out << "default";
  return out;
}

QString WalletService::addressFor(const QString& label) const {
  const auto a = wallet_.address_for(label.toStdString());
  return a.empty() ? address() : toQString(a);
}

QString WalletService::auditPath() const {
  return QString::fromStdString((std::filesystem::path(datadir_.toStdString()) / "wallet" / "audit.md").string());
}

void WalletService::audit(const QString& action, const QString& details) const {
  std::error_code ec;
  const auto p = std::filesystem::path(auditPath().toStdString());
  std::filesystem::create_directories(p.parent_path(), ec);
  QFile f(auditPath());
  if (!f.open(QFile::Append | QFile::Text)) return;
  QTextStream out(&f);
  out << QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd HH:mm:ss 'UTC'") << " | " << action;
  if (!details.isEmpty()) out << " | " << details;
  out << "\n";
}

phantomx::Result<phantomx::rpc::RpcResponse> WalletService::nodeCall(const std::string& method,
                                                                       std::unordered_map<std::string, std::string> params) const {
  auto r = wallet_.call_rpc(method, std::move(params));
  audit("rpc_call", QString("%1 ok=%2").arg(toQString(method), r.ok() ? "true" : "false"));
  return r;
}

void WalletService::updateSyncEstimate() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  node_height_ = chain_height_;
  network_height_ = header_height_;

  const std::uint32_t previousHeight = sync_samples_.empty() ? chain_height_ : sync_samples_.back().second;
  if (last_node_block_ms_ == 0 || chain_height_ > previousHeight) last_node_block_ms_ = now;
  if (node_online_) last_successful_sync_ms_ = now;

  sync_samples_.push_back({now, chain_height_});
  pruneSamples(sync_samples_, now, 60 * 1000, 60);
  const double rate = rollingBlocksPerSecond(sync_samples_, 30 * 1000);

  if (network_height_ <= node_height_) {
    syncing_ = false;
    sync_progress_ = node_online_ ? 1.0 : 0.0;
    eta_minutes_ = node_online_ ? 0 : -1;
    node_blocks_per_second_ = rate;
    node_eta_seconds_ = -1;
    return;
  }

  syncing_ = true;
  sync_progress_ = static_cast<double>(node_height_) / static_cast<double>(network_height_);
  if (sync_progress_ > 1.0) sync_progress_ = 1.0;
  node_blocks_per_second_ = rate;
  const std::uint64_t remaining = network_height_ - node_height_;
  node_eta_seconds_ = rate > 0.0 ? static_cast<qint64>(static_cast<double>(remaining) / rate) : -1;
  eta_minutes_ = node_eta_seconds_ >= 0 ? static_cast<int>((node_eta_seconds_ + 30000) / 60000) : -1;
}

void WalletService::updateWalletScanEstimate() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  wallet_scan_target_height_ = chain_height_;
  if (!node_online_ || !hasWallet()) {
    wallet_scan_available_ = false;
    wallet_scan_syncing_ = false;
    wallet_scan_blocks_per_second_ = -1.0;
    wallet_scan_eta_seconds_ = -1;
    return;
  }

  auto history = wallet_.history();
  if (!history.ok()) {
    wallet_scan_available_ = false;
    wallet_scan_syncing_ = false;
    wallet_scan_blocks_per_second_ = -1.0;
    wallet_scan_eta_seconds_ = -1;
    return;
  }

  std::uint32_t indexedHeight = 0;
  for (const auto& tx : history.value()) indexedHeight = std::max(indexedHeight, tx.height);
  wallet_scan_height_ = indexedHeight;
  wallet_scan_available_ = true;
  last_wallet_scan_ms_ = now;

  wallet_scan_samples_.push_back({now, wallet_scan_height_});
  pruneSamples(wallet_scan_samples_, now, 60 * 1000, 60);
  wallet_scan_blocks_per_second_ = rollingBlocksPerSecond(wallet_scan_samples_, 30 * 1000);

  wallet_scan_syncing_ = wallet_scan_target_height_ > wallet_scan_height_;
  const std::uint64_t remaining = wallet_scan_syncing_ ? wallet_scan_target_height_ - wallet_scan_height_ : 0;
  wallet_scan_eta_seconds_ = wallet_scan_blocks_per_second_ > 0.0
      ? static_cast<qint64>(static_cast<double>(remaining) / wallet_scan_blocks_per_second_)
      : -1;
}

void WalletService::loadLocalChainFallback() {
  using_local_data_ = false;
  data_source_note_.clear();
  local_tx_history_.clear();

  const auto dbPath = std::filesystem::path(datadir_.toStdString()) / "chain.db";
  std::error_code ec;
  if (!std::filesystem::exists(dbPath, ec) || ec) {
    data_source_note_ = tr("Node offline; local chain.db not found.");
    return;
  }

  auto dbRes = phantomx::db::SqliteDb::open_readonly(dbPath);
  if (!dbRes.ok()) {
    data_source_note_ = tr("Node offline; unable to read local chain: %1").arg(errText(dbRes.error()));
    return;
  }
  auto db = std::move(dbRes.value());

  // Tip height from metadata.
  std::uint32_t localHeight = 0;
  auto tipStmt = db.prepare("SELECT value FROM metadata WHERE key='tip_height'");
  if (tipStmt.ok()) {
    auto row = tipStmt.value().step();
    if (row.ok() && row.value()) {
      try { localHeight = static_cast<std::uint32_t>(std::stoul(tipStmt.value().column_text(0))); } catch (...) {}
    }
  }
  if (localHeight == 0) {
    auto hStmt = db.prepare("SELECT MAX(height) FROM blocks WHERE active=1");
    if (hStmt.ok()) {
      auto row = hStmt.value().step();
      if (row.ok() && row.value() && !hStmt.value().column_is_null(0)) {
        localHeight = static_cast<std::uint32_t>(hStmt.value().column_int64(0));
      }
    }
  }

  // Build script_pubkeys for wallet accounts.
  struct Acc { QString label; std::vector<std::uint8_t> spk; };
  std::vector<Acc> accs;
  for (const auto& info : wallet_.accounts()) {
    std::vector<std::uint8_t> pub;
    if (!phantomx::from_hex(info.public_key_hex, pub) || pub.size() != 32) continue;
    Acc a;
    a.label = QString::fromStdString(info.label);
    a.spk = phantomx::script_pubkey_for_pubkey(pub);
    accs.push_back(std::move(a));
  }
  // Fallback to default key if accounts() is empty.
  if (accs.empty() && wallet_.has_wallet()) {
    const auto pkHex = wallet_.public_key_hex();
    std::vector<std::uint8_t> pub;
    if (phantomx::from_hex(pkHex, pub) && pub.size() == 32) {
      Acc a;
      a.label = QStringLiteral("default");
      a.spk = phantomx::script_pubkey_for_pubkey(pub);
      accs.push_back(std::move(a));
    }
  }

  std::uint64_t bal = 0;
  QVariantList txs;
  for (const auto& acc : accs) {
    auto st = db.prepare("SELECT txid, vout, amount, height, script_pubkey, coinbase FROM utxos WHERE script_pubkey=? ORDER BY height DESC, rowid DESC LIMIT 200");
    if (!st.ok()) continue;
    auto bnd = st.value().bind_blob(1, std::span<const std::uint8_t>(acc.spk.data(), acc.spk.size()));
    if (!bnd.ok()) continue;
    while (true) {
      auto row = st.value().step();
      if (!row.ok() || !row.value()) break;
      const std::string txid = st.value().column_text(0);
      const std::uint32_t vout = st.value().column_u32(1);
      const std::uint64_t amount = st.value().column_u64(2);
      const std::uint32_t height = st.value().column_u32(3);
      bal += amount;
      QVariantMap m;
      m["txid"] = QString::fromStdString(txid);
      m["vout"] = vout;
      m["amount_atoms"] = static_cast<qulonglong>(amount);
      m["height"] = height;
      m["spent"] = false; // UTXO set contains unspent outputs
      m["account"] = acc.label;
      txs.push_back(m);
    }
  }

  // Sort by height desc, then txid.
  std::sort(txs.begin(), txs.end(), [](const QVariant& a, const QVariant& b) {
    const auto am = a.toMap();
    const auto bm = b.toMap();
    const auto ah = am.value("height").toUInt();
    const auto bh = bm.value("height").toUInt();
    if (ah != bh) return ah > bh;
    return am.value("txid").toString() > bm.value("txid").toString();
  });
  if (txs.size() > 200) txs = txs.mid(0, 200);

  chain_height_ = std::max(chain_height_, localHeight);
  header_height_ = std::max(header_height_, chain_height_);
  node_height_ = chain_height_;
  network_height_ = 0;  // No peer target is available while offline.
  node_blocks_per_second_ = -1.0;
  node_eta_seconds_ = -1;
  wallet_scan_blocks_per_second_ = -1.0;
  wallet_scan_eta_seconds_ = -1;
  balance_atoms_ = bal;
  spendable_balance_atoms_ = bal;
  immature_balance_atoms_ = 0; // local fallback cannot reliably determine maturity
  local_tx_history_ = txs;
  using_local_data_ = true;
  sync_progress_ = localHeight > 0 ? 1.0 : 0.0; // unknown live sync; local data is available
  syncing_ = false;
  eta_minutes_ = -1;
  data_source_note_ = tr("Node offline - showing local blockchain data (height %1, %2 UTXO).")
                          .arg(localHeight)
                          .arg(txs.size());
}

QVariantMap WalletService::refreshNodeStatus() {
  QVariantMap out;
  out["online"] = false;
  rpc_error_.clear();
  auto h = wallet_.chain_height();
  auto c = wallet_.connection_count();
  node_online_ = h.ok();
  chain_height_ = h.ok() ? h.value() : 0;
  connection_count_ = c.ok() ? c.value() : 0;
  balance_atoms_ = 0;
  spendable_balance_atoms_ = 0;
  immature_balance_atoms_ = 0;
  if (node_online_) {
    for (const auto& acc : wallet_.accounts()) {
      std::unordered_map<std::string, std::string> params;
      params["public_key_hex"] = acc.public_key_hex;
      auto resp = wallet_.call_rpc("getbalance", params);
      if (!resp.ok()) continue;
      const auto& r = resp.value().result;
      uint64_t spendable = 0;
      uint64_t immature = 0;
      if (auto it = r.find("spendable_atoms"); it != r.end()) { try { spendable = static_cast<uint64_t>(std::stoull(it->second)); } catch (...) {} }
      else if (auto it2 = r.find("atoms"); it2 != r.end()) { try { spendable = static_cast<uint64_t>(std::stoull(it2->second)); } catch (...) {} }
      if (auto it = r.find("immature_atoms"); it != r.end()) { try { immature = static_cast<uint64_t>(std::stoull(it->second)); } catch (...) {} }
      spendable_balance_atoms_ += spendable;
      immature_balance_atoms_ += immature;
    }
    balance_atoms_ = spendable_balance_atoms_ + immature_balance_atoms_;
  }
  using_local_data_ = false;
  data_source_note_.clear();
  local_tx_history_.clear();
  if (!h.ok()) rpc_error_ = errText(h.error());

  header_height_ = chain_height_;
  // Best-effort blockchain info for the observed network target and tip time.
  auto bc = nodeCall("getblockchaininfo", {});
  if (bc.ok()) {
    const auto& r = bc.value().result;
    try {
      if (auto it = r.find("headers"); it != r.end()) header_height_ = static_cast<uint32_t>(std::stoul(it->second));
      if (auto it = r.find("blocks"); it != r.end()) chain_height_ = std::max(chain_height_, static_cast<uint32_t>(std::stoul(it->second)));
      if (auto it = r.find("blocktime"); it != r.end()) {
        const auto seconds = std::stoull(it->second);
        if (seconds > 0) last_node_block_ms_ = static_cast<qint64>(seconds) * 1000;
      }
    } catch (...) {
      // Keep the last known values when a node returns malformed sync data.
    }
  }
  if (header_height_ < chain_height_) header_height_ = chain_height_;

  if (!node_online_) {
    loadLocalChainFallback();
  } else {
    updateSyncEstimate();
    updateWalletScanEstimate();
  }

  out["online"] = node_online_;
  out["using_local_data"] = using_local_data_;
  out["data_source_note"] = data_source_note_;
  out["height"] = chain_height_;
  out["headers"] = header_height_;
  out["connections"] = connection_count_;
  out["balance_atoms"] = static_cast<qulonglong>(balance_atoms_);
  out["syncing"] = syncing_;
  out["sync_progress"] = sync_progress_;
  out["eta_minutes"] = eta_minutes_;
  out["node_height"] = node_height_;
  out["network_height"] = network_height_;
  out["node_blocks_per_second"] = node_blocks_per_second_;
  out["node_eta_seconds"] = node_eta_seconds_;
  out["last_node_block_ms"] = last_node_block_ms_;
  out["last_successful_sync_ms"] = last_successful_sync_ms_;
  out["wallet_scan_height"] = wallet_scan_height_;
  out["wallet_scan_target_height"] = wallet_scan_target_height_;
  out["wallet_blocks_per_second"] = wallet_scan_blocks_per_second_;
  out["wallet_eta_seconds"] = wallet_scan_eta_seconds_;
  out["last_wallet_scan_ms"] = last_wallet_scan_ms_;
  out["wallet_scan_available"] = wallet_scan_available_;
  out["error"] = rpc_error_;
  if (node_online_) maybeAutoSweep();
  emit rpcStatusChanged();
  emit walletChanged();
  return out;
}

QVariantMap WalletService::sendToAddress(const QString& destination, quint64 amountAtoms, quint64 feeAtoms) {
  QVariantMap out;
  out["ok"] = false;
  auto r = wallet_.send(destination.trimmed().toStdString(), static_cast<std::uint64_t>(amountAtoms), static_cast<std::uint64_t>(feeAtoms));
  if (!r.ok()) {
    out["error"] = errText(r.error());
    emit message(out["error"].toString(), true);
    return out;
  }
  out["ok"] = true;
  out["txid"] = toQString(r.value());
  emit message(tr("Transaction sent: %1").arg(out["txid"].toString()), false);
  audit("send", QString("to=%1 amount=%2 fee=%3 txid=%4").arg(destination).arg(amountAtoms).arg(feeAtoms).arg(out["txid"].toString()));
  refreshNodeStatus();
  return out;
}

QVariantList WalletService::transactionHistory() {
  if (!node_online_ && using_local_data_) return local_tx_history_;
  QVariantList list;
  auto h = wallet_.history();
  if (!h.ok()) {
    if (!node_online_ && !local_tx_history_.isEmpty()) return local_tx_history_;
    return list;
  }
  for (const auto& tx : h.value()) {
    QVariantMap m;
    m["txid"] = toQString(tx.txid);
    m["vout"] = tx.vout;
    m["amount_atoms"] = static_cast<qulonglong>(tx.amount_atoms);
    m["height"] = tx.height;
    m["spent"] = tx.spent;
    m["account"] = toQString(tx.account);
    list.push_back(m);
  }
  return list;
}

void WalletService::setMiningSettings(const MiningSettings& settings) {
  mining_ = settings;
  if (mining_.bandwidthUnit != "MB" && mining_.bandwidthUnit != "GB" && mining_.bandwidthUnit != "TB") mining_.bandwidthUnit = "MB";
  if (mining_.bandwidthValue == 0) mining_.bandwidthValue = 1;
  poi_running_ = mining_.enabled;
  saveMiningConfig();
  audit("poi_settings", QString("enabled=%1 limit=%2").arg(mining_.enabled ? "true" : "false", mining_.displayLimit()));
  emit miningChanged();
}

void WalletService::saveMiningConfig() const {
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(datadir_.toStdString()), ec);
  QSettings s(QString::fromStdString((std::filesystem::path(datadir_.toStdString()) / "qt-wallet.ini").string()), QSettings::IniFormat);
  s.setValue("mining/enabled", mining_.enabled);
  s.setValue("mining/bandwidthValue", static_cast<qulonglong>(mining_.bandwidthValue));
  s.setValue("mining/bandwidthUnit", mining_.bandwidthUnit.toUpper());
  s.setValue("mining/domain", mining_.domain.trimmed());
  s.setValue("wallet/treasury_address", treasury_address_);
  s.setValue("wallet/auto_sweep_treasury", auto_sweep_treasury_);
  s.sync();
}

QVariantMap WalletService::startPoi() {
  MiningSettings s = mining_;
  s.enabled = true;
  setMiningSettings(s);
  std::unordered_map<std::string, std::string> params{{"bandwidth_bytes", std::to_string(mining_.bandwidthBytes())},
                                                     {"bandwidth_value", std::to_string(mining_.bandwidthValue)},
                                                     {"bandwidth_unit", mining_.bandwidthUnit.toStdString()}};
  if (!mining_.domain.trimmed().isEmpty()) params["domain"] = mining_.domain.trimmed().toStdString();
  const auto reward = address().trimmed();
  if (!reward.isEmpty()) {
    params["reward_address"] = reward.toStdString();
    params["owner_address"] = reward.toStdString();
  }
  params["participant"] = reward.isEmpty() ? "local" : reward.toStdString();
  params["machine_id"] = QSysInfo::machineHostName().toStdString();
  auto r = nodeCall("startpoi", params);
  QVariantMap out;
  if (r.ok()) {
    poi_running_ = true;
    mining_.enabled = true;
    saveMiningConfig();
    out["ok"] = true;
    out["status"] = QString::fromStdString(r.value().result.count("status") ? r.value().result.at("status") : "running");
    out["backend_supported"] = true;
    emit message(tr("PoI started."), false);
  } else {
    // Keep local intent; backend control unavailable.
    poi_running_ = mining_.enabled;
    out["ok"] = false;
    out["backend_supported"] = false;
    out["error"] = errText(r.error());
    out["status"] = "Not Available";
    emit message(tr("PoI start RPC not available; setting saved locally."), true);
  }
  emit miningChanged();
  return out;
}

QVariantMap WalletService::stopPoi() {
  MiningSettings s = mining_;
  s.enabled = false;
  setMiningSettings(s);
  auto r = nodeCall("stoppoi", {});
  QVariantMap out;
  if (r.ok()) {
    poi_running_ = false;
    mining_.enabled = false;
    saveMiningConfig();
    out["ok"] = true;
    out["status"] = QString::fromStdString(r.value().result.count("status") ? r.value().result.at("status") : "stopped");
    out["backend_supported"] = true;
    emit message(tr("PoI stopped."), false);
  } else {
    poi_running_ = false;
    out["ok"] = false;
    out["backend_supported"] = false;
    out["error"] = errText(r.error());
    out["status"] = "Not Available";
    emit message(tr("PoI stop RPC not available; setting saved locally."), true);
  }
  emit miningChanged();
  return out;
}

QVariantMap WalletService::miningStatus() {
  QVariantMap out;
  out["enabled"] = mining_.enabled;
  out["running"] = poi_running_;
  out["bandwidth_value"] = static_cast<qulonglong>(mining_.bandwidthValue);
  out["bandwidth_unit"] = mining_.bandwidthUnit;
  out["bandwidth_bytes"] = static_cast<qulonglong>(mining_.bandwidthBytes());
  out["display_limit"] = mining_.displayLimit();
  out["domain"] = mining_.domain;
  out["node_online"] = node_online_;
  out["connected_peers"] = connection_count_;
  out["backend_supported"] = false;
  out["backend_note"] = tr("Bandwidth limit/domain is stored in the Qt wallet config. PoI backend enforcement/control requires RPC/daemon support.");
  auto r = nodeCall("getpoiinfo", {});
  if (r.ok()) {
    out["poi_available"] = true;
    out["backend_supported"] = true;
    const auto& res = r.value().result;
    out["poi_status"] = QString::fromStdString(res.count("status") ? res.at("status") : "ok");
    out["poi_score"] = QString::fromStdString(res.count("score") ? res.at("score") : "Not Available");
    out["poi_epoch"] = QString::fromStdString(res.count("epoch") ? res.at("epoch") : "Not Available");
    out["poi_active_miners"] = QString::fromStdString(res.count("active_miners") ? res.at("active_miners") : "Not Available");
    out["poi_target_block_interval_ms"] = QString::fromStdString(res.count("target_block_interval_ms") ? res.at("target_block_interval_ms") : "Not Available");
    out["poi_score_partial"] = QString::fromStdString(res.count("score_partial") ? res.at("score_partial") : "false");
    out["poi_score_bandwidth"] = QString::fromStdString(res.count("score_bandwidth") ? res.at("score_bandwidth") : "Not Available");
    out["poi_score_latency"] = QString::fromStdString(res.count("score_latency") ? res.at("score_latency") : "Not Available");
    out["poi_score_uptime"] = QString::fromStdString(res.count("score_uptime") ? res.at("score_uptime") : "Not Available");
    out["poi_score_relay"] = QString::fromStdString(res.count("score_relay") ? res.at("score_relay") : "Not Available");
    out["poi_score_network"] = QString::fromStdString(res.count("score_network") ? res.at("score_network") : "Not Available");
    out["poi_latency_ms"] = QString::fromStdString(res.count("latency_ms") ? res.at("latency_ms") : "Not Available");
    out["poi_relay_ratio"] = QString::fromStdString(res.count("relay_ratio") ? res.at("relay_ratio") : "Not Available");
    out["poi_uptime_ratio"] = QString::fromStdString(res.count("uptime_ratio") ? res.at("uptime_ratio") : "Not Available");
    out["poi_network_bandwidth_mbps"] = QString::fromStdString(res.count("poi_network_bandwidth_mbps") ? res.at("poi_network_bandwidth_mbps") : "Not Available");
    out["poi_network_bandwidth_display"] = QString::fromStdString(res.count("poi_network_bandwidth_display") ? res.at("poi_network_bandwidth_display") : "Not Available");
    out["poi_bandwidth_unit"] = QString::fromStdString(res.count("poi_bandwidth_unit") ? res.at("poi_bandwidth_unit") : "Mbps");
    out["score_formula_version"] = QString::fromStdString(res.count("score_formula_version") ? res.at("score_formula_version") : "Not Available");
    out["score_weights"] = QString::fromStdString(res.count("score_weights") ? res.at("score_weights") : "Not Available");
  } else {
    out["poi_available"] = false;
    out["poi_status"] = "Not Available";
    out["poi_score"] = "Not Available";
    out["poi_epoch"] = "Not Available";
  }
  return out;
}

QString WalletService::generateNewAddress(const QString& label, QString* error) {
  if (!ensureWallet(error)) return {};
  QString lbl = label.trimmed();
  if (lbl.isEmpty()) lbl = QString("address_%1").arg(accountLabels().size() + 1);
  auto r = wallet_.create_account(lbl.toStdString());
  if (!r.ok()) { if (error) *error = errText(r.error()); emit message(errText(r.error()), true); return {}; }
  const auto addr = addressFor(lbl);
  audit("generate_address", QString("label=%1").arg(lbl));
  emit walletChanged();
  return addr;
}

QString WalletService::treasuryAddress() const {
  if (!treasury_address_.isEmpty()) return treasury_address_;
  return address();
}

void WalletService::setTreasuryAddress(const QString& address) {
  treasury_address_ = address.trimmed();
  saveMiningConfig();
  audit("set_treasury", treasury_address_);
  emit walletChanged();
}

void WalletService::setAutoSweepToTreasury(bool enabled) {
  auto_sweep_treasury_ = enabled;
  saveMiningConfig();
  audit("set_auto_sweep_treasury", enabled ? "true" : "false");
}

void WalletService::maybeAutoSweep() {
  if (!auto_sweep_treasury_ || !node_online_ || !wallet_.has_wallet()) return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - last_sweep_ms_ < 60000) return; // cooldown
  const QString treasury = treasuryAddress().trimmed();
  if (treasury.isEmpty()) return;

  auto net = phantomx::network_from_string(network_.toStdString());
  if (!net.has_value()) net = phantomx::Network::kTestnet;
  auto dest = phantomx::script_pubkey_for_address(treasury.toStdString(), phantomx::address_hrp(*net));
  if (!dest.ok()) return;

  const auto labels = wallet_.account_labels();
  for (const auto& lbl : labels) {
    const auto addr = wallet_.address_for(lbl);
    if (QString::fromStdString(addr) == treasury) continue;
    auto kp = wallet_.key_pair_for(lbl);
    if (!kp.has_value()) continue;
    const auto pubhex = phantomx::crypto::serialize_public_key_hex(kp->pub);
    auto r = nodeCall("getutxos", {{"public_key_hex", pubhex}});
    if (!r.ok()) continue;
    auto it = r.value().result.find("utxos_json");
    if (it == r.value().result.end()) continue;

    json_error_t jerr{};
    json_t* arr = json_loads(it->second.c_str(), 0, &jerr);
    if (arr == nullptr || !json_is_array(arr)) { json_decref(arr); continue; }

    phantomx::TransactionBuilder builder;
    std::uint64_t total_in = 0;
    for (size_t i = 0; i < json_array_size(arr); ++i) {
      json_t* obj = json_array_get(arr, i);
      const json_t* txid = json_object_get(obj, "txid");
      const json_t* vout = json_object_get(obj, "vout");
      const json_t* amount = json_object_get(obj, "amount");
      const json_t* script = json_object_get(obj, "script_pubkey_hex");
      if (!txid || !json_is_string(txid) || !script || !json_is_string(script)) continue;
      std::vector<std::uint8_t> txid_bytes;
      if (!phantomx::from_hex(json_string_value(txid), txid_bytes) || txid_bytes.size() != 32) continue;
      phantomx::Hash256Bytes id{};
      std::copy(txid_bytes.begin(), txid_bytes.end(), id.begin());
      phantomx::UtxoEntry entry;
      if (json_is_string(amount)) entry.amount_atoms = std::stoull(json_string_value(amount));
      else if (json_is_number(amount)) entry.amount_atoms = static_cast<std::uint64_t>(json_integer_value(amount));
      if (!phantomx::from_hex(json_string_value(script), entry.script_pubkey)) continue;
      const uint32_t vo = json_is_number(vout) ? static_cast<uint32_t>(json_integer_value(vout)) : 0;
      builder.add_input(phantomx::BuilderInput{phantomx::OutPoint{id, vo}, entry, kp->sec, kp->pub});
      total_in += entry.amount_atoms;
    }
    json_decref(arr);

    const std::uint64_t fee = 1000ULL;
    if (total_in <= fee + 1000ULL) continue; // too small/dust
    builder.add_output(phantomx::BuilderOutput{total_in - fee, dest.value()});
    auto tx = builder.build_and_sign();
    if (!tx.ok()) continue;
    std::unordered_map<std::string, std::string> params;
    params["hex"] = phantomx::to_hex(phantomx::serialize_transaction(tx.value()));
    auto sendResp = nodeCall("sendrawtransaction", params);
    if (sendResp.ok()) {
      last_sweep_ms_ = now;
      audit("auto_sweep", QString("from=%1 to=%2 amount=%3").arg(QString::fromStdString(lbl), treasury).arg(total_in - fee));
      emit message(tr("Auto-sweep to treasury: %1 atoms").arg(total_in - fee), false);
      break; // one sweep per cycle
    }
  }
}
