#include "WalletBridge.hpp"

#include <QClipboard>
#include <QFileDialog>
#include <QGuiApplication>
#include <QtGlobal>

#include "WalletService.hpp"

namespace {
QString atomsToPhx(quint64 atoms) {
  const quint64 whole = atoms / 100000000ULL;
  quint64 frac = atoms % 100000000ULL;
  QString fracStr = QString::number(frac).rightJustified(8, '0');
  while (fracStr.endsWith('0')) fracStr.chop(1);
  if (fracStr.isEmpty()) fracStr = "0";
  return QString::number(whole) + "." + fracStr + " PHX";
}
}  // namespace

WalletBridge::WalletBridge(WalletService* service, QObject* parent) : QObject(parent), service_(service) {
  if (!service_) return;
  connect(service_, &WalletService::rpcStatusChanged, this, [this]() { refreshPoiState(); emit stateChanged(); });
  connect(service_, &WalletService::walletChanged, this, [this]() { emit stateChanged(); });
  connect(service_, &WalletService::networkChanged, this, [this]() { emit stateChanged(); });
  connect(service_, &WalletService::miningChanged, this, [this]() { refreshPoiState(); emit poiChanged(); });
  connect(service_, &WalletService::message, this, &WalletBridge::message);
  refreshPoiState();
}

bool WalletBridge::online() const { return service_ && service_->nodeOnline(); }
bool WalletBridge::usingLocalData() const { return service_ && service_->usingLocalData(); }
bool WalletBridge::syncing() const { return service_ && service_->syncing(); }

QString WalletBridge::syncText() const {
  if (!service_) return QStringLiteral("Not Available");
  if (service_->nodeOnline()) return service_->syncing() ? QStringLiteral("Downloading") : QStringLiteral("Synced");
  if (service_->usingLocalData()) return QStringLiteral("Synced");
  return QStringLiteral("Not Available");
}

double WalletBridge::syncProgress() const { return service_ ? service_->syncProgress() : 0.0; }
int WalletBridge::chainHeight() const { return service_ ? static_cast<int>(service_->chainHeight()) : 0; }
int WalletBridge::headerHeight() const { return service_ ? static_cast<int>(service_->headerHeight()) : 0; }
int WalletBridge::connectionCount() const { return service_ ? static_cast<int>(service_->connectionCount()) : 0; }

QString WalletBridge::balanceDisplay() const {
  if (!service_) return QStringLiteral("—");
  if (service_->nodeOnline() || service_->usingLocalData()) return atomsToPhx(service_->balanceAtoms());
  return QStringLiteral("—");
}

QString WalletBridge::spendableBalanceDisplay() const {
  if (!service_) return QStringLiteral("—");
  if (service_->nodeOnline() || service_->usingLocalData()) return atomsToPhx(service_->spendableBalanceAtoms());
  return QStringLiteral("—");
}

QString WalletBridge::immatureBalanceDisplay() const {
  if (!service_) return QStringLiteral("—");
  if (service_->nodeOnline() || service_->usingLocalData()) return atomsToPhx(service_->immatureBalanceAtoms());
  return QStringLiteral("—");
}

bool WalletBridge::hasImmatureBalance() const {
  return service_ && (service_->nodeOnline() || service_->usingLocalData()) && service_->immatureBalanceAtoms() > 0;
}

QString WalletBridge::address() const {
  if (!service_ || !service_->hasWallet()) return {};
  return service_->address();
}

QString WalletBridge::heightDisplay() const {
  if (!service_) return QStringLiteral("—");
  if (service_->nodeOnline() || service_->usingLocalData()) return QString::number(service_->chainHeight());
  return QStringLiteral("—");
}

QString WalletBridge::peersDisplay() const {
  if (!service_ || !service_->nodeOnline()) return QStringLiteral("—");
  return QString::number(service_->connectionCount());
}

QString WalletBridge::network() const { return service_ ? service_->network() : QString(); }
QString WalletBridge::dataSourceNote() const { return service_ ? service_->dataSourceNote() : QString(); }

bool WalletBridge::poiEnabled() const { return service_ && service_->miningSettings().enabled; }
bool WalletBridge::poiRunning() const { return service_ && service_->poiRunning(); }
int WalletBridge::poiBandwidthValue() const { return service_ ? static_cast<int>(service_->miningSettings().bandwidthValue) : 100; }
QString WalletBridge::poiBandwidthUnit() const { return service_ ? service_->miningSettings().bandwidthUnit.toUpper() : QStringLiteral("MB"); }
QString WalletBridge::poiDomain() const { return service_ ? service_->miningSettings().domain : QString(); }

QString WalletBridge::poiPeersDisplay() const {
  if (!service_ || !service_->nodeOnline()) return QStringLiteral("—");
  return QString::number(service_->connectionCount());
}

bool WalletBridge::poiBackendSupported() const { return poiStatus_.value("backend_supported").toBool(); }

QString WalletBridge::poiBackendNote() const {
  const auto note = poiStatus_.value("backend_note").toString();
  return note.isEmpty() ? QStringLiteral("—") : note;
}

QString WalletBridge::na(const QString& value) const {
  const auto v = value.trimmed();
  if (v.isEmpty() || v == QStringLiteral("Not Available")) return QStringLiteral("—");
  return v;
}

QVariantList WalletBridge::poiStatusRows() const {
  QVariantList rows;
  const bool on = online();
  const auto s = poiStatus_;
  const auto m = service_ ? service_->miningSettings() : MiningSettings{};
  auto add = [&rows](const QString& group, const QString& label, const QString& value, const QString& badge = QString(), const QString& badgeTone = QString()) {
    QVariantMap r;
    r["group"] = group;
    r["label"] = label;
    r["value"] = value;
    r["badge"] = badge;
    r["badgeTone"] = badgeTone; // success/warning/error/info/muted
    rows.push_back(r);
  };

  const bool running = service_ && service_->poiRunning();
  const bool enabled = m.enabled;
  add(QStringLiteral("Connection"), QStringLiteral("Node"), on ? QStringLiteral("Online") : QStringLiteral("Offline"), on ? QStringLiteral("Online") : QStringLiteral("Offline"), on ? QStringLiteral("success") : QStringLiteral("warning"));
  add(QStringLiteral("Connection"), QStringLiteral("Connected Peers"), on ? QString::number(service_->connectionCount()) : QStringLiteral("—"));
  add(QStringLiteral("Connection"), QStringLiteral("PoI RPC"), na(s.value("poi_status").toString()), s.value("poi_available").toBool() ? QStringLiteral("Available") : QStringLiteral("Not Available"), s.value("poi_available").toBool() ? QStringLiteral("success") : QStringLiteral("muted"));

  add(QStringLiteral("Performance"), QStringLiteral("Running"), running ? QStringLiteral("Yes") : QStringLiteral("No"), running ? QStringLiteral("Running") : QStringLiteral("Stopped"), running ? QStringLiteral("success") : QStringLiteral("muted"));
  add(QStringLiteral("Performance"), QStringLiteral("Score"), na(s.value("poi_score").toString()));
  add(QStringLiteral("Performance"), QStringLiteral("Bandwidth Score"), na(s.value("poi_score_bandwidth").toString()) == QStringLiteral("—") ? QStringLiteral("—") : na(s.value("poi_score_bandwidth").toString()) + QStringLiteral("%"));
  add(QStringLiteral("Performance"), QStringLiteral("Latency Score"), na(s.value("poi_score_latency").toString()) == QStringLiteral("—") ? QStringLiteral("—") : na(s.value("poi_score_latency").toString()) + QStringLiteral("%"));
  add(QStringLiteral("Performance"), QStringLiteral("Uptime Score"), na(s.value("poi_score_uptime").toString()) == QStringLiteral("—") ? QStringLiteral("—") : na(s.value("poi_score_uptime").toString()) + QStringLiteral("%"));
  add(QStringLiteral("Performance"), QStringLiteral("Latency"), na(s.value("poi_latency_ms").toString()) == QStringLiteral("—") ? QStringLiteral("—") : na(s.value("poi_latency_ms").toString()) + QStringLiteral(" ms"));
  add(QStringLiteral("Performance"), QStringLiteral("Network Bandwidth"), na(s.value("poi_network_bandwidth_display").toString()));
  add(QStringLiteral("Performance"), QStringLiteral("Epoch"), na(s.value("poi_epoch").toString()));
  add(QStringLiteral("Performance"), QStringLiteral("Active Miners"), na(s.value("poi_active_miners").toString()));
  add(QStringLiteral("Performance"), QStringLiteral("Target Block Interval"), na(s.value("poi_target_block_interval_ms").toString()) == QStringLiteral("—") ? QStringLiteral("—") : na(s.value("poi_target_block_interval_ms").toString()) + QStringLiteral(" ms"));

  add(QStringLiteral("Configuration"), QStringLiteral("Enabled Setting"), enabled ? QStringLiteral("Yes") : QStringLiteral("No"), enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled"), enabled ? QStringLiteral("info") : QStringLiteral("muted"));
  add(QStringLiteral("Configuration"), QStringLiteral("Bandwidth Limit"), m.displayLimit());
  add(QStringLiteral("Configuration"), QStringLiteral("Domain"), m.domain.trimmed().isEmpty() ? QStringLiteral("—") : m.domain.trimmed());
  return rows;
}

void WalletBridge::refreshPoiState() {
  if (!service_) return;
  poiStatus_ = service_->miningStatus();
}

void WalletBridge::refresh() {
  if (!service_) return;
  service_->refreshNodeStatus();
  refreshPoiState();
  emit stateChanged();
  emit poiChanged();
}

void WalletBridge::startPoi() {
  if (!service_) return;
  service_->startPoi();
  refreshPoiState();
  emit stateChanged();
  emit poiChanged();
}

void WalletBridge::stopPoi() {
  if (!service_) return;
  service_->stopPoi();
  refreshPoiState();
  emit stateChanged();
  emit poiChanged();
}

void WalletBridge::applyPoiSettings(bool enabled, int bandwidthValue, const QString& bandwidthUnit, const QString& domain) {
  if (!service_) return;
  MiningSettings s = service_->miningSettings();
  s.enabled = enabled;
  s.bandwidthValue = static_cast<quint64>(qMax(1, bandwidthValue));
  s.bandwidthUnit = bandwidthUnit.toUpper();
  s.domain = domain.trimmed();
  service_->setMiningSettings(s);
  refreshPoiState();
  emit stateChanged();
  emit poiChanged();
}

void WalletBridge::copyAddress() {
  if (!service_ || !service_->hasWallet()) return;
  QGuiApplication::clipboard()->setText(service_->address());
  emit message(QStringLiteral("Address copied."), false);
}

bool WalletBridge::hasWallet() const { return service_ && service_->hasWallet(); }
QString WalletBridge::treasuryAddress() const { return service_ ? service_->treasuryAddress() : QString(); }
QString WalletBridge::keystorePath() const { return service_ ? service_->keystorePath() : QString(); }
QString WalletBridge::datadir() const { return service_ ? service_->datadir() : QString(); }
QString WalletBridge::nodeRpc() const { return service_ ? service_->nodeRpc() : QString(); }
bool WalletBridge::autoSweep() const { return service_ && service_->autoSweepToTreasury(); }

QVariantList WalletBridge::addressList() const {
  QVariantList out;
  if (!service_) return out;
  for (const auto& lbl : service_->accountLabels()) {
    QVariantMap m;
    m["label"] = lbl;
    m["address"] = service_->addressFor(lbl);
    out.push_back(m);
  }
  return out;
}

QVariantList WalletBridge::transactions() const {
  QVariantList out;
  if (!service_) return out;
  const auto rows = service_->transactionHistory();
  for (const auto& v : rows) {
    const auto m = v.toMap();
    QVariantMap r;
    r["txid"] = m.value("txid").toString();
    r["vout"] = m.value("vout").toString();
    r["amount"] = atomsToPhx(m.value("amount_atoms").toULongLong());
    r["height"] = m.value("height").toString();
    r["spent"] = m.value("spent").toBool() ? QStringLiteral("yes") : QStringLiteral("no");
    r["account"] = m.value("account").toString();
    out.push_back(r);
  }
  return out;
}

void WalletBridge::copyText(const QString& text) {
  QGuiApplication::clipboard()->setText(text);
  emit message(QStringLiteral("Copied."), false);
}

QString WalletBridge::sendToAddress(const QString& destination, const QString& amountAtoms, int feePresetIndex) {
  if (!service_) return QStringLiteral("ERR:No service");
  bool okA = false;
  const quint64 amount = amountAtoms.trimmed().toULongLong(&okA);
  if (!okA) return QStringLiteral("ERR:Amount must be a valid number of atoms.");
  const quint64 fee = feePresetIndex == 0 ? 5000ULL : (feePresetIndex == 2 ? 1000ULL : 2000ULL);
  const auto r = service_->sendToAddress(destination.trimmed(), amount, fee);
  if (r.value("ok").toBool()) return QStringLiteral("OK:") + r.value("txid").toString();
  return QStringLiteral("ERR:") + r.value("error").toString();
}

void WalletBridge::generateNewAddress() {
  if (!service_) return;
  QString err;
  const QString a = service_->generateNewAddress(QString(), &err);
  if (a.isEmpty()) { emit message(err, true); return; }
  QGuiApplication::clipboard()->setText(a);
  emit message(QStringLiteral("New address created and copied."), false);
  emit stateChanged();
}

void WalletBridge::generateBatchAddresses(int count) {
  if (!service_) return;
  int n = qMax(1, qMin(count, 20));
  int ok = 0;
  for (int i = 0; i < n; ++i) {
    QString err;
    const QString a = service_->generateNewAddress(QString(), &err);
    if (a.isEmpty()) { emit message(err, true); break; }
    ++ok;
  }
  if (ok > 0) { emit message(QStringLiteral("%1 new addresses created.").arg(ok), false); emit stateChanged(); }
}

void WalletBridge::createWallet() {
  if (!service_) return;
  QString err;
  if (service_->createWallet(&err)) emit message(QStringLiteral("Wallet created."), false);
  else emit message(err, true);
  emit stateChanged();
}

void WalletBridge::loadWallet() {
  if (!service_) return;
  QString err;
  if (service_->loadWallet(&err)) emit message(QStringLiteral("Wallet loaded."), false);
  else emit message(err, true);
  emit stateChanged();
}

void WalletBridge::importWallet(const QString& secretHex) {
  if (!service_) return;
  QString err;
  if (service_->importWallet(secretHex, &err)) emit message(QStringLiteral("Wallet imported."), false);
  else emit message(err, true);
  emit stateChanged();
}

void WalletBridge::importWalletFile() {
  if (!service_) return;
  const QString path = QFileDialog::getOpenFileName(
      nullptr, QStringLiteral("Import PhantomX Keystore"), QString(),
      QStringLiteral("PhantomX Keystore (*.keystore *.json *.bak);;All Files (*)"));
  if (path.isEmpty()) return;
  QString err;
  if (service_->importWalletFile(path, &err)) emit message(QStringLiteral("Keystore file imported."), false);
  else emit message(err, true);
  emit stateChanged();
}

void WalletBridge::backupWalletFile() {
  if (!service_) return;
  const QString path = QFileDialog::getSaveFileName(
      nullptr, QStringLiteral("Backup PhantomX Keystore"),
      QStringLiteral("phantomx-wallet.keystore.bak"),
      QStringLiteral("PhantomX Keystore (*.keystore *.bak);;All Files (*)"));
  if (path.isEmpty()) return;
  QString err;
  if (service_->backupWallet(path, &err)) emit message(QStringLiteral("Backup saved."), false);
  else emit message(err, true);
}

void WalletBridge::backupWallet(const QString& destinationPath) {
  if (!service_) return;
  QString err;
  if (service_->backupWallet(destinationPath, &err)) emit message(QStringLiteral("Backup saved."), false);
  else emit message(err, true);
}

void WalletBridge::applySettings(const QString& network, const QString& datadir, const QString& rpc) {
  if (!service_) return;
  const QString oldNet = service_->network();
  const QString oldDatadir = service_->datadir();
  const QString oldRpc = service_->nodeRpc();
  if (!network.trimmed().isEmpty() && network.trimmed() != oldNet) service_->setNetwork(network.trimmed());
  if (!datadir.trimmed().isEmpty() && datadir.trimmed() != oldDatadir) service_->setDatadir(datadir.trimmed());
  if (!rpc.trimmed().isEmpty() && rpc.trimmed() != oldRpc) service_->setNodeRpc(rpc.trimmed());
  emit message(QStringLiteral("Settings applied."), false);
  refresh();
}

void WalletBridge::setTreasuryAddress(const QString& address) {
  if (!service_) return;
  service_->setTreasuryAddress(address.trimmed());
  emit message(QStringLiteral("Treasury saved."), false);
  emit stateChanged();
}

void WalletBridge::setAutoSweep(bool enabled) {
  if (!service_) return;
  service_->setAutoSweepToTreasury(enabled);
  emit stateChanged();
}
