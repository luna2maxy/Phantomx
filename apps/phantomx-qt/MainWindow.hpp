#pragma once

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;

#include "WalletService.hpp"

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private:
  void setupUi();
  void setupStyle();
  void setupConnections();
  void refreshAll();
  void refreshOverview();
  void refreshTransactions();
  void refreshPoi();
  void showPage(int index);

  QWidget* buildSidebar();
  QWidget* buildOverviewPage();
  QWidget* buildSendPage();
  QWidget* buildReceivePage();
  QWidget* buildTransactionsPage();
  QWidget* buildPoiPage();
  QWidget* buildSettingsPage();
  QWidget* wrapScroll(QWidget* inner);
  void setStatusMessage(const QString& text, bool isError = false);

  WalletService* service_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* networkLabel_ = nullptr;
  QLabel* syncStatusBarLabel_ = nullptr;
  QLabel* sidebarBalanceLabel_ = nullptr;
  QLabel* sidebarSyncLabel_ = nullptr;

  // Overview
  QLabel* balanceLabel_ = nullptr;
  QLabel* addressLabel_ = nullptr;
  QLabel* heightLabel_ = nullptr;
  QLabel* connLabel_ = nullptr;
  QLabel* overviewStatus_ = nullptr;
  QLabel* backupBanner_ = nullptr;

  // Send
  QLineEdit* sendToEdit_ = nullptr;
  QLineEdit* sendAmountEdit_ = nullptr;
  QComboBox* sendPresetCombo_ = nullptr;
  QLabel* sendFeeLabel_ = nullptr;
  QLabel* sendResultLabel_ = nullptr;

  // Transactions
  QTableWidget* txTable_ = nullptr;

  // Settings
  QLineEdit* rpcEdit_ = nullptr;
  QComboBox* networkCombo_ = nullptr;
  QLineEdit* datadirEdit_ = nullptr;
  QLabel* keystorePathLabel_ = nullptr;

  // PoI
  QCheckBox* poiEnable_ = nullptr;
  QSpinBox* bandwidthSpin_ = nullptr;
  QComboBox* bandwidthUnit_ = nullptr;
  QLineEdit* poiDomainEdit_ = nullptr;
  QLabel* poiStatusLabel_ = nullptr;
  QLabel* poiBackendLabel_ = nullptr;
  QLabel* poiPeersLabel_ = nullptr;
  QPushButton* poiStartBtn_ = nullptr;
  QPushButton* poiStopBtn_ = nullptr;

  QTimer* pollTimer_ = nullptr;
};
