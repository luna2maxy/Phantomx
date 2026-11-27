#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class WalletService;

class WalletBridge final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool online READ online NOTIFY stateChanged)
  Q_PROPERTY(bool usingLocalData READ usingLocalData NOTIFY stateChanged)
  Q_PROPERTY(bool syncing READ syncing NOTIFY stateChanged)
  Q_PROPERTY(QString syncText READ syncText NOTIFY stateChanged)
  Q_PROPERTY(double syncProgress READ syncProgress NOTIFY stateChanged)
  Q_PROPERTY(int chainHeight READ chainHeight NOTIFY stateChanged)
  Q_PROPERTY(int headerHeight READ headerHeight NOTIFY stateChanged)
  Q_PROPERTY(int connectionCount READ connectionCount NOTIFY stateChanged)

  Q_PROPERTY(bool walletSyncAvailable READ walletSyncAvailable NOTIFY stateChanged)
  Q_PROPERTY(bool walletSyncing READ walletSyncing NOTIFY stateChanged)
  Q_PROPERTY(double walletSyncProgress READ walletSyncProgress NOTIFY stateChanged)
  Q_PROPERTY(int walletScanHeight READ walletScanHeight NOTIFY stateChanged)
  Q_PROPERTY(int walletScanTargetHeight READ walletScanTargetHeight NOTIFY stateChanged)
  Q_PROPERTY(double walletBlocksPerSecond READ walletBlocksPerSecond NOTIFY stateChanged)
  Q_PROPERTY(qint64 walletEtaSeconds READ walletEtaSeconds NOTIFY stateChanged)
  Q_PROPERTY(qint64 walletLastScanMs READ walletLastScanMs NOTIFY stateChanged)

  Q_PROPERTY(bool nodeSyncing READ nodeSyncing NOTIFY stateChanged)
  Q_PROPERTY(double nodeSyncProgress READ nodeSyncProgress NOTIFY stateChanged)
  Q_PROPERTY(int nodeHeight READ nodeHeight NOTIFY stateChanged)
  Q_PROPERTY(int networkHeight READ networkHeight NOTIFY stateChanged)
  Q_PROPERTY(double nodeBlocksPerSecond READ nodeBlocksPerSecond NOTIFY stateChanged)
  Q_PROPERTY(qint64 nodeEtaSeconds READ nodeEtaSeconds NOTIFY stateChanged)
  Q_PROPERTY(qint64 nodeLastBlockMs READ nodeLastBlockMs NOTIFY stateChanged)
  Q_PROPERTY(qint64 nodeLastSyncMs READ nodeLastSyncMs NOTIFY stateChanged)

  Q_PROPERTY(bool hideBalance READ hideBalance WRITE setHideBalance NOTIFY hideBalanceChanged)
  Q_PROPERTY(QString balanceDisplay READ balanceDisplay NOTIFY stateChanged)
  Q_PROPERTY(QString spendableBalanceDisplay READ spendableBalanceDisplay NOTIFY stateChanged)
  Q_PROPERTY(QString immatureBalanceDisplay READ immatureBalanceDisplay NOTIFY stateChanged)
  Q_PROPERTY(bool hasImmatureBalance READ hasImmatureBalance NOTIFY stateChanged)
  Q_PROPERTY(QString address READ address NOTIFY stateChanged)
  Q_PROPERTY(QString heightDisplay READ heightDisplay NOTIFY stateChanged)
  Q_PROPERTY(QString peersDisplay READ peersDisplay NOTIFY stateChanged)
  Q_PROPERTY(QString network READ network NOTIFY stateChanged)
  Q_PROPERTY(QString dataSourceNote READ dataSourceNote NOTIFY stateChanged)

  Q_PROPERTY(bool poiEnabled READ poiEnabled NOTIFY poiChanged)
  Q_PROPERTY(bool poiRunning READ poiRunning NOTIFY poiChanged)
  Q_PROPERTY(int poiBandwidthValue READ poiBandwidthValue NOTIFY poiChanged)
  Q_PROPERTY(QString poiBandwidthUnit READ poiBandwidthUnit NOTIFY poiChanged)
  Q_PROPERTY(QString poiDomain READ poiDomain NOTIFY poiChanged)
  Q_PROPERTY(QString poiPeersDisplay READ poiPeersDisplay NOTIFY poiChanged)
  Q_PROPERTY(QVariantList poiStatusRows READ poiStatusRows NOTIFY poiChanged)
  Q_PROPERTY(QString poiBackendNote READ poiBackendNote NOTIFY poiChanged)
  Q_PROPERTY(bool poiBackendSupported READ poiBackendSupported NOTIFY poiChanged)

  Q_PROPERTY(bool hasWallet READ hasWallet NOTIFY stateChanged)
  Q_PROPERTY(QString treasuryAddress READ treasuryAddress NOTIFY stateChanged)
  Q_PROPERTY(QVariantList addressList READ addressList NOTIFY stateChanged)
  Q_PROPERTY(QVariantList transactions READ transactions NOTIFY stateChanged)
  Q_PROPERTY(QString keystorePath READ keystorePath NOTIFY stateChanged)
  Q_PROPERTY(QString datadir READ datadir NOTIFY stateChanged)
  Q_PROPERTY(QString nodeRpc READ nodeRpc NOTIFY stateChanged)
  Q_PROPERTY(bool autoSweep READ autoSweep NOTIFY stateChanged)

 public:
  explicit WalletBridge(WalletService* service, QObject* parent = nullptr);

  bool online() const;
  bool usingLocalData() const;
  bool syncing() const;
  QString syncText() const;
  double syncProgress() const;
  int chainHeight() const;
  int headerHeight() const;
  int connectionCount() const;

  bool walletSyncAvailable() const;
  bool walletSyncing() const;
  double walletSyncProgress() const;
  int walletScanHeight() const;
  int walletScanTargetHeight() const;
  double walletBlocksPerSecond() const;
  qint64 walletEtaSeconds() const;
  qint64 walletLastScanMs() const;

  bool nodeSyncing() const;
  double nodeSyncProgress() const;
  int nodeHeight() const;
  int networkHeight() const;
  double nodeBlocksPerSecond() const;
  qint64 nodeEtaSeconds() const;
  qint64 nodeLastBlockMs() const;
  qint64 nodeLastSyncMs() const;

  bool hideBalance() const;
  Q_INVOKABLE void setHideBalance(bool hide);
  QString balanceDisplay() const;
  QString spendableBalanceDisplay() const;
  QString immatureBalanceDisplay() const;
  bool hasImmatureBalance() const;
  QString address() const;
  QString heightDisplay() const;
  QString peersDisplay() const;
  QString network() const;
  QString dataSourceNote() const;

  bool poiEnabled() const;
  bool poiRunning() const;
  int poiBandwidthValue() const;
  QString poiBandwidthUnit() const;
  QString poiDomain() const;
  QString poiPeersDisplay() const;
  QVariantList poiStatusRows() const;
  QString poiBackendNote() const;
  bool poiBackendSupported() const;

  bool hasWallet() const;
  QString treasuryAddress() const;
  QVariantList addressList() const;
  QVariantList transactions() const;
  QString keystorePath() const;
  QString datadir() const;
  QString nodeRpc() const;
  bool autoSweep() const;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void startPoi();
  Q_INVOKABLE void stopPoi();
  Q_INVOKABLE void applyPoiSettings(bool enabled, int bandwidthValue, const QString& bandwidthUnit, const QString& domain);
  Q_INVOKABLE void copyAddress();
  Q_INVOKABLE void copyText(const QString& text);
  Q_INVOKABLE QString sendToAddress(const QString& destination, const QString& amountAtoms, int feePresetIndex);
  Q_INVOKABLE void generateNewAddress();
  Q_INVOKABLE void generateBatchAddresses(int count);
  Q_INVOKABLE void createWallet();
  Q_INVOKABLE void loadWallet();
  Q_INVOKABLE void importWallet(const QString& secretHex);
  Q_INVOKABLE void importWalletFile();
  Q_INVOKABLE void backupWallet(const QString& destinationPath);
  Q_INVOKABLE void backupWalletFile();
  Q_INVOKABLE void applySettings(const QString& network, const QString& datadir, const QString& rpc);
  Q_INVOKABLE void setTreasuryAddress(const QString& address);
  Q_INVOKABLE void setAutoSweep(bool enabled);

 signals:
  void stateChanged();
  void hideBalanceChanged();
  void poiChanged();
  void message(const QString& text, bool isError);

 private:
  void refreshPoiState();
  QString na(const QString& value) const;

  WalletService* service_ = nullptr;
  QVariantMap poiStatus_;
  bool hideBalance_ = false;
};
