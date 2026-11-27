#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <deque>
#include <utility>

#include "phantomx/wallet/wallet.hpp"

struct MiningSettings {
  bool enabled = false;
  quint64 bandwidthValue = 100;
  QString bandwidthUnit = QStringLiteral("MB"); // MB, GB, TB
  QString domain;
  [[nodiscard]] quint64 bandwidthBytes() const;
  [[nodiscard]] QString displayLimit() const;
};

class WalletService final : public QObject {
  Q_OBJECT

 public:
  explicit WalletService(QObject* parent = nullptr);

  [[nodiscard]] bool hasWallet() const { return wallet_.has_wallet(); }
  [[nodiscard]] QString address() const { return QString::fromStdString(wallet_.address()); }
  [[nodiscard]] QString publicKeyHex() const { return QString::fromStdString(wallet_.public_key_hex()); }
  [[nodiscard]] QString network() const { return network_; }
  [[nodiscard]] QString nodeRpc() const { return node_rpc_; }
  [[nodiscard]] QString datadir() const { return datadir_; }
  [[nodiscard]] QString configPath() const { return config_path_; }
  [[nodiscard]] QString keystorePath() const;
  [[nodiscard]] QString rpcError() const { return rpc_error_; }
  [[nodiscard]] bool nodeOnline() const { return node_online_; }
  [[nodiscard]] uint32_t chainHeight() const { return chain_height_; }
  [[nodiscard]] uint32_t headerHeight() const { return header_height_; }
  [[nodiscard]] uint32_t connectionCount() const { return connection_count_; }
  [[nodiscard]] uint64_t balanceAtoms() const { return balance_atoms_; }
  [[nodiscard]] uint64_t spendableBalanceAtoms() const { return spendable_balance_atoms_; }
  [[nodiscard]] uint64_t immatureBalanceAtoms() const { return immature_balance_atoms_; }
  [[nodiscard]] double syncProgress() const { return sync_progress_; }
  [[nodiscard]] int etaMinutes() const { return eta_minutes_; }
  [[nodiscard]] bool syncing() const { return syncing_; }
  [[nodiscard]] uint32_t nodeHeight() const { return node_height_; }
  [[nodiscard]] uint32_t networkHeight() const { return network_height_; }
  [[nodiscard]] double nodeBlocksPerSecond() const { return node_blocks_per_second_; }
  [[nodiscard]] qint64 nodeEtaSeconds() const { return node_eta_seconds_; }
  [[nodiscard]] qint64 lastNodeBlockMs() const { return last_node_block_ms_; }
  [[nodiscard]] qint64 lastSuccessfulSyncMs() const { return last_successful_sync_ms_; }
  [[nodiscard]] uint32_t walletScanHeight() const { return wallet_scan_height_; }
  [[nodiscard]] uint32_t walletScanTargetHeight() const { return wallet_scan_target_height_; }
  [[nodiscard]] double walletScanBlocksPerSecond() const { return wallet_scan_blocks_per_second_; }
  [[nodiscard]] qint64 walletScanEtaSeconds() const { return wallet_scan_eta_seconds_; }
  [[nodiscard]] qint64 lastWalletScanMs() const { return last_wallet_scan_ms_; }
  [[nodiscard]] bool walletScanAvailable() const { return wallet_scan_available_; }
  [[nodiscard]] bool walletScanSyncing() const { return wallet_scan_syncing_; }
  [[nodiscard]] bool autoCreatedWallet() const { return auto_created_wallet_; }
  [[nodiscard]] bool poiRunning() const { return poi_running_; }
  [[nodiscard]] bool usingLocalData() const { return using_local_data_; }
  [[nodiscard]] QString dataSourceNote() const { return data_source_note_; }

  void setNetwork(const QString& network);
  void setNodeRpc(const QString& rpc);
  void setDatadir(const QString& datadir);

  [[nodiscard]] bool ensureWallet(QString* error = nullptr);
  [[nodiscard]] bool createWallet(QString* error = nullptr);
  [[nodiscard]] bool loadWallet(QString* error = nullptr);
  [[nodiscard]] bool importWallet(const QString& secret_hex, QString* error = nullptr);
  [[nodiscard]] bool importWalletFile(const QString& sourcePath, QString* error = nullptr);
  [[nodiscard]] bool backupWallet(const QString& destinationPath, QString* error = nullptr);
  [[nodiscard]] QString generateNewAddress(const QString& label = QString(), QString* error = nullptr);
  [[nodiscard]] QStringList accountLabels() const;
  [[nodiscard]] QString addressFor(const QString& label) const;
  [[nodiscard]] QString auditPath() const;
  void audit(const QString& action, const QString& details = QString()) const;
  [[nodiscard]] QString treasuryAddress() const;
  void setTreasuryAddress(const QString& address);
  [[nodiscard]] bool autoSweepToTreasury() const { return auto_sweep_treasury_; }
  void setAutoSweepToTreasury(bool enabled);

  QVariantMap refreshNodeStatus();
  QVariantMap sendToAddress(const QString& destination, quint64 amountAtoms, quint64 feeAtoms);
  QVariantList transactionHistory();

  [[nodiscard]] MiningSettings miningSettings() const { return mining_; }
  void setMiningSettings(const MiningSettings& settings);
  QVariantMap miningStatus();
  QVariantMap startPoi();
  QVariantMap stopPoi();

 signals:
  void walletChanged();
  void networkChanged();
  void rpcStatusChanged();
  void miningChanged();
  void message(const QString& text, bool is_error);

 private:
  void initFromDefaults();
  void loadConfig(bool preserve_network = false);
  void applyNetworkDefaults();
  void rebuildWallet();
  void updateSyncEstimate();
  void updateWalletScanEstimate();
  void maybeAutoSweep();
  [[nodiscard]] phantomx::Result<phantomx::rpc::RpcResponse> nodeCall(
      const std::string& method,
      std::unordered_map<std::string, std::string> params = {}) const;
  void saveMiningConfig() const;
  void loadLocalChainFallback();

  phantomx::wallet::Wallet wallet_;
  QString node_rpc_ = QStringLiteral("http://127.0.0.1:8741");
  QString rpc_token_;
  QString network_ = QStringLiteral("testnet");
  QString datadir_;
  QString config_path_;
  QString rpc_error_;
  bool node_online_ = false;
  uint32_t chain_height_ = 0;
  uint32_t header_height_ = 0;
  uint32_t connection_count_ = 0;
  uint64_t balance_atoms_ = 0;
  uint64_t spendable_balance_atoms_ = 0;
  uint64_t immature_balance_atoms_ = 0;
  double sync_progress_ = 0.0;
  int eta_minutes_ = -1;
  bool syncing_ = false;
  bool auto_created_wallet_ = false;
  bool poi_running_ = false;
  bool auto_sweep_treasury_ = false;
  bool using_local_data_ = false;
  QString data_source_note_;
  QString treasury_address_;
  qint64 last_sweep_ms_ = 0;
  std::deque<std::pair<qint64, uint32_t>> sync_samples_;
  uint32_t node_height_ = 0;
  uint32_t network_height_ = 0;
  double node_blocks_per_second_ = -1.0;
  qint64 node_eta_seconds_ = -1;
  qint64 last_node_block_ms_ = 0;
  qint64 last_successful_sync_ms_ = 0;
  uint32_t wallet_scan_height_ = 0;
  uint32_t wallet_scan_target_height_ = 0;
  double wallet_scan_blocks_per_second_ = -1.0;
  qint64 wallet_scan_eta_seconds_ = -1;
  qint64 last_wallet_scan_ms_ = 0;
  bool wallet_scan_available_ = false;
  bool wallet_scan_syncing_ = false;
  std::deque<std::pair<qint64, uint32_t>> wallet_scan_samples_;
  QVariantList local_tx_history_;
  MiningSettings mining_;
};
