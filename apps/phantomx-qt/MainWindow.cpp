#include "MainWindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString atomsToPhx(quint64 atoms) {
  const quint64 whole = atoms / 100000000ULL;
  quint64 frac = atoms % 100000000ULL;
  QString fracStr = QString::number(frac).rightJustified(8, '0');
  while (fracStr.endsWith('0')) fracStr.chop(1);
  if (fracStr.isEmpty()) fracStr = "0";
  return QString::number(whole) + "." + fracStr + " PHX";
}

QFrame* makeCard(const QString& title, QLabel** valueOut, const QString& initial = "—") {
  auto* card = new QFrame();
  card->setObjectName("card");
  auto* l = new QVBoxLayout(card);
  l->setContentsMargins(14, 12, 14, 12);
  auto* t = new QLabel(title);
  t->setObjectName("cardTitle");
  auto* v = new QLabel(initial);
  v->setObjectName("cardValue");
  v->setWordWrap(true);
  l->addWidget(t);
  l->addWidget(v);
  if (valueOut) *valueOut = v;
  return card;
}

QPushButton* navButton(const QString& text) {
  auto* b = new QPushButton(text);
  b->setObjectName("navBtn");
  b->setCheckable(true);
  b->setCursor(Qt::PointingHandCursor);
  b->setMinimumHeight(38);
  return b;
}

QLabel* sectionTitle(const QString& text) {
  auto* l = new QLabel(text);
  l->setObjectName("sectionTitle");
  return l;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  service_ = new WalletService(this);
  pollTimer_ = new QTimer(this);
  pollTimer_->setInterval(15000);
  setupUi();
  setupStyle();
  setupConnections();
  refreshAll();
  pollTimer_->start();
}

QWidget* MainWindow::wrapScroll(QWidget* inner) {
  auto* scroll = new QScrollArea();
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(inner);
  return scroll;
}

void MainWindow::setupUi() {
  setWindowTitle("PhantomX Wallet");
  resize(1040, 680);
  setMinimumSize(920, 600);

  auto* central = new QWidget();
  auto* root = new QHBoxLayout(central);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  root->addWidget(buildSidebar(), 0);

  auto* mainArea = new QWidget();
  auto* mainLayout = new QVBoxLayout(mainArea);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  auto* topbar = new QFrame();
  topbar->setObjectName("topbar");
  auto* topLayout = new QHBoxLayout(topbar);
  topLayout->setContentsMargins(20, 14, 20, 14);
  auto* title = new QLabel("PhantomX Desktop Wallet");
  title->setObjectName("topTitle");
  statusLabel_ = new QLabel("Node: checking...");
  statusLabel_->setObjectName("statusPill");
  networkLabel_ = new QLabel();
  networkLabel_->setObjectName("muted");
  topLayout->addWidget(title);
  topLayout->addStretch(1);
  topLayout->addWidget(networkLabel_);
  topLayout->addSpacing(12);
  topLayout->addWidget(statusLabel_);
  mainLayout->addWidget(topbar);

  stack_ = new QStackedWidget();
  stack_->addWidget(wrapScroll(buildOverviewPage()));
  stack_->addWidget(wrapScroll(buildSendPage()));
  stack_->addWidget(wrapScroll(buildReceivePage()));
  stack_->addWidget(wrapScroll(buildTransactionsPage()));
  stack_->addWidget(wrapScroll(buildPoiPage()));
  stack_->addWidget(wrapScroll(buildSettingsPage()));
  mainLayout->addWidget(stack_, 1);
  root->addWidget(mainArea, 1);

  syncStatusBarLabel_ = new QLabel("Sync: Not Available");
  statusBar()->addWidget(syncStatusBarLabel_, 1);
  setCentralWidget(central);
}

QWidget* MainWindow::buildSidebar() {
  auto* side = new QFrame();
  side->setObjectName("sidebar");
  side->setFixedWidth(230);
  auto* l = new QVBoxLayout(side);
  l->setContentsMargins(16, 20, 16, 16);
  l->setSpacing(6);

  auto* logoRow = new QHBoxLayout();
  logoRow->setSpacing(10);
  auto* mark = new QLabel("X");
  mark->setFixedSize(34, 34);
  mark->setAlignment(Qt::AlignCenter);
  mark->setStyleSheet("background:#F26822;color:#ffffff;font-size:18px;font-weight:800;border-radius:8px;");
  auto* logoCol = new QVBoxLayout();
  logoCol->setSpacing(0);
  auto* logo = new QLabel("PHANTOMX");
  logo->setObjectName("logo");
  auto* sub = new QLabel("DESKTOP WALLET");
  sub->setObjectName("logoSub");
  logoCol->addWidget(logo);
  logoCol->addWidget(sub);
  logoRow->addWidget(mark, 0, Qt::AlignTop);
  logoRow->addLayout(logoCol, 1);
  l->addLayout(logoRow);
  l->addSpacing(18);

  auto* balTitle = new QLabel("BALANCE");
  balTitle->setObjectName("cardTitle");
  sidebarBalanceLabel_ = new QLabel("Not Available");
  sidebarBalanceLabel_->setStyleSheet("color:#f2f2f2;font-size:18px;font-weight:700;background:transparent;");
  sidebarBalanceLabel_->setWordWrap(true);
  sidebarSyncLabel_ = new QLabel("Not Available");
  sidebarSyncLabel_->setObjectName("muted");
  sidebarSyncLabel_->setWordWrap(true);
  l->addWidget(balTitle);
  l->addWidget(sidebarBalanceLabel_);
  l->addWidget(sidebarSyncLabel_);
  l->addSpacing(18);

  const QStringList names = {"Overview", "Send", "Receive", "Transactions", "PoI", "Settings"};
  for (int i = 0; i < names.size(); ++i) {
    auto* b = navButton(names[i]);
    b->setChecked(i == 0);
    connect(b, &QPushButton::clicked, this, [this, i]() { showPage(i); });
    l->addWidget(b);
  }
  l->addStretch(1);
  auto* note = new QLabel("PhantomX local wallet\nReal data from node / local chain");
  note->setWordWrap(true);
  note->setObjectName("sideNote");
  l->addWidget(note);
  return side;
}

QWidget* MainWindow::buildOverviewPage() {
  auto* page = new QWidget();
  auto* l = new QVBoxLayout(page);
  l->setContentsMargins(24, 24, 24, 24);
  l->setSpacing(16);

  backupBanner_ = new QLabel();
  backupBanner_->setObjectName("warnBanner");
  backupBanner_->setWordWrap(true);
  backupBanner_->setVisible(false);
  l->addWidget(backupBanner_);

  auto* cards = new QGridLayout();
  cards->setHorizontalSpacing(12);
  cards->setVerticalSpacing(12);
  cards->addWidget(makeCard("Balance", &balanceLabel_, "Not Available"), 0, 0);
  cards->addWidget(makeCard("Block Height", &heightLabel_, "Not Available"), 0, 1);
  cards->addWidget(makeCard("Connected Peers (Node)", &connLabel_, "Not Available"), 0, 2);
  l->addLayout(cards);

  auto* addrCard = new QFrame();
  addrCard->setObjectName("addressCard");
  auto* acl = new QVBoxLayout(addrCard);
  acl->setContentsMargins(18, 16, 18, 16);
  acl->setSpacing(10);
  auto* addrTitleRow = new QHBoxLayout();
  auto* addrTitle = new QLabel("Receive Address");
  addrTitle->setObjectName("cardTitle");
  auto* addrBadge = new QLabel("PHX");
  addrBadge->setObjectName("addrBadge");
  addrTitleRow->addWidget(addrTitle);
  addrTitleRow->addStretch(1);
  addrTitleRow->addWidget(addrBadge);
  acl->addLayout(addrTitleRow);
  addressLabel_ = new QLabel("No wallet loaded");
  addressLabel_->setObjectName("addressBig");
  addressLabel_->setWordWrap(true);
  addressLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  acl->addWidget(addressLabel_);
  auto* addrBtnRow = new QHBoxLayout();
  auto* copyAddrCardBtn = new QPushButton("Copy Address");
  copyAddrCardBtn->setObjectName("primaryBtn");
  copyAddrCardBtn->setCursor(Qt::PointingHandCursor);
  addrBtnRow->addWidget(copyAddrCardBtn);
  addrBtnRow->addStretch(1);
  acl->addLayout(addrBtnRow);
  l->addWidget(addrCard);

  auto* actions = new QFrame();
  actions->setObjectName("card");
  auto* al = new QHBoxLayout(actions);
  al->setContentsMargins(14, 12, 14, 12);
  auto* refreshBtn = new QPushButton("Refresh");
  refreshBtn->setObjectName("ghostBtn");
  auto* openWalletBtn = new QPushButton("Wallet Management");
  openWalletBtn->setObjectName("primaryBtn");
  al->addWidget(openWalletBtn);
  al->addStretch(1);
  al->addWidget(refreshBtn);
  l->addWidget(actions);

  overviewStatus_ = new QLabel("Ready.");
  overviewStatus_->setObjectName("muted");
  overviewStatus_->setWordWrap(true);
  l->addWidget(overviewStatus_);

  auto* info = new QFrame();
  info->setObjectName("card");
  auto* il = new QVBoxLayout(info);
  il->setContentsMargins(14, 12, 14, 12);
  il->addWidget(sectionTitle("Network Info"));
  auto* infoText = new QLabel(
      "Overview shows live data from node RPC. A default wallet is created automatically if no local keystore exists. "
      "Manage create/import/backup in Settings > Wallet Management.");
  infoText->setWordWrap(true);
  infoText->setObjectName("muted");
  il->addWidget(infoText);
  l->addWidget(info);
  l->addStretch(1);

  connect(refreshBtn, &QPushButton::clicked, this, [this]() { refreshAll(); setStatusMessage("Refreshed."); });
  connect(openWalletBtn, &QPushButton::clicked, this, [this]() { showPage(5); });
  connect(copyAddrCardBtn, &QPushButton::clicked, this, [this]() {
    if (!service_->hasWallet()) { setStatusMessage("No wallet loaded.", true); return; }
    QApplication::clipboard()->setText(service_->address());
    setStatusMessage("Address disalin.");
  });
  return page;
}

QWidget* MainWindow::buildSendPage() {
  auto* page = new QWidget();
  auto* l = new QVBoxLayout(page);
  l->setContentsMargins(24, 24, 24, 24);
  l->setSpacing(16);
  auto* box = new QFrame();
  box->setObjectName("card");
  auto* form = new QGridLayout(box);
  form->setContentsMargins(16, 16, 16, 16);
  form->setHorizontalSpacing(12);
  form->setVerticalSpacing(10);

  sendToEdit_ = new QLineEdit();
  sendToEdit_->setPlaceholderText("PhantomX destination address");
  sendAmountEdit_ = new QLineEdit();
  sendAmountEdit_->setPlaceholderText("Amount in atoms (example: 100000000 = 1 PHX)");
  sendPresetCombo_ = new QComboBox();
  sendPresetCombo_->addItems({"Fast (2 conf)", "Normal (6 conf)", "Slow (12 conf)"});
  sendPresetCombo_->setCurrentIndex(1);
  sendFeeLabel_ = new QLabel();
  sendFeeLabel_->setObjectName("muted");
  auto* sendBtn = new QPushButton("Send Transaction");
  sendBtn->setObjectName("primaryBtn");
  sendResultLabel_ = new QLabel("No transaction sent yet.");
  sendResultLabel_->setWordWrap(true);
  sendResultLabel_->setObjectName("muted");

  form->addWidget(new QLabel("To"), 0, 0);
  form->addWidget(sendToEdit_, 0, 1);
  form->addWidget(new QLabel("Amount (atoms)"), 1, 0);
  form->addWidget(sendAmountEdit_, 1, 1);
  form->addWidget(new QLabel("Fee preset"), 2, 0);
  form->addWidget(sendPresetCombo_, 2, 1);
  form->addWidget(new QLabel("Fee"), 3, 0);
  form->addWidget(sendFeeLabel_, 3, 1);
  form->addWidget(sendBtn, 4, 1, Qt::AlignLeft);
  form->addWidget(sendResultLabel_, 5, 0, 1, 2);

  l->addWidget(box);
  l->addStretch(1);

  auto updateFee = [this]() {
    const int idx = sendPresetCombo_->currentIndex();
    const quint64 fee = idx == 0 ? 5000ULL : (idx == 2 ? 1000ULL : 2000ULL);
    const QString conf = idx == 0 ? "2 conf" : (idx == 2 ? "12 conf" : "6 conf");
    sendFeeLabel_->setText(QString("%1 atoms (%2 PHX) • target %3").arg(fee).arg(atomsToPhx(fee), conf));
  };
  connect(sendPresetCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [updateFee](int){ updateFee(); });
  updateFee();
  connect(sendBtn, &QPushButton::clicked, this, [this]() {
    bool okA = false;
    const quint64 amount = sendAmountEdit_->text().trimmed().toULongLong(&okA);
    if (!okA) { setStatusMessage("Amount must be a valid number of atoms.", true); return; }
    const int idx = sendPresetCombo_->currentIndex();
    const quint64 fee = idx == 0 ? 5000ULL : (idx == 2 ? 1000ULL : 2000ULL);
    const auto r = service_->sendToAddress(sendToEdit_->text(), amount, fee);
    if (r.value("ok").toBool()) {
      sendResultLabel_->setText("TXID: " + r.value("txid").toString());
      setStatusMessage("Transaction sent.");
    } else {
      sendResultLabel_->setText("Error: " + r.value("error").toString());
      setStatusMessage(r.value("error").toString(), true);
    }
    refreshAll();
  });
  return page;
}

QWidget* MainWindow::buildReceivePage() {
  auto* page = new QWidget();
  auto* l = new QVBoxLayout(page);
  l->setContentsMargins(24, 24, 24, 24);
  l->setSpacing(16);

  auto* box = new QFrame();
  box->setObjectName("card");
  auto* bl = new QVBoxLayout(box);
  bl->setContentsMargins(16, 16, 16, 16);
  bl->addWidget(sectionTitle("Receive / Treasury Address"));
  auto* addr = new QLabel();
  addr->setObjectName("addressBig");
  addr->setTextInteractionFlags(Qt::TextSelectableByMouse);
  addr->setWordWrap(true);
  auto* btnRow = new QHBoxLayout();
  auto* copyBtn = new QPushButton("Copy Address");
  copyBtn->setObjectName("primaryBtn");
  auto* genBtn = new QPushButton("Generate New Address");
  auto* genBatchBtn = new QPushButton("Generate 5 Addresses");
  btnRow->addWidget(copyBtn);
  btnRow->addWidget(genBtn);
  btnRow->addWidget(genBatchBtn);
  btnRow->addStretch(1);
  auto* note = new QLabel("One keystore can contain multiple addresses. New addresses are created in the same keystore (suitable for exchanges). The treasury is the primary receiving address.");
  note->setWordWrap(true);
  note->setObjectName("muted");
  bl->addWidget(addr);
  bl->addLayout(btnRow);
  bl->addWidget(note);
  l->addWidget(box);

  auto* listTitle = sectionTitle("Addresses in Keystore");
  l->addWidget(listTitle);

  auto* listContainer = new QWidget();
  listContainer->setObjectName("addrListContainer");
  auto* listLayout = new QVBoxLayout(listContainer);
  listLayout->setContentsMargins(0, 0, 0, 0);
  listLayout->setSpacing(10);
  l->addWidget(listContainer);
  l->addStretch(1);

  auto refreshAddrs = [this, addr, listContainer, listLayout]() {
    const QString a = service_->hasWallet() ? service_->treasuryAddress() : "No wallet loaded";
    addr->setText(a);

    while (QLayoutItem* item = listLayout->takeAt(0)) {
      if (QWidget* w = item->widget()) w->deleteLater();
      delete item;
    }

    const auto labels = service_->accountLabels();
    if (labels.isEmpty()) {
      auto* emptyCard = new QFrame();
      emptyCard->setObjectName("card");
      auto* el = new QVBoxLayout(emptyCard);
      el->setContentsMargins(16, 14, 16, 14);
      auto* emptyLbl = new QLabel("No addresses");
      emptyLbl->setObjectName("muted");
      emptyLbl->setWordWrap(true);
      el->addWidget(emptyLbl);
      listLayout->addWidget(emptyCard);
      return;
    }

    for (const auto& lbl : labels) {
      const QString addrText = service_->addressFor(lbl);
      auto* card = new QFrame();
      card->setObjectName("card");
      auto* cl = new QVBoxLayout(card);
      cl->setContentsMargins(16, 14, 16, 14);
      cl->setSpacing(8);

      auto* topRow = new QHBoxLayout();
      auto* nameLbl = new QLabel(lbl);
      nameLbl->setStyleSheet("color:#f2f2f2;font-weight:700;background:transparent;");
      nameLbl->setWordWrap(true);
      auto* copyOne = new QPushButton("Copy");
      copyOne->setObjectName("ghostBtn");
      copyOne->setCursor(Qt::PointingHandCursor);
      copyOne->setFixedHeight(30);
      topRow->addWidget(nameLbl, 1);
      topRow->addWidget(copyOne, 0, Qt::AlignTop);
      cl->addLayout(topRow);

      auto* valueLbl = new QLabel(addrText);
      valueLbl->setObjectName("addressBig");
      valueLbl->setWordWrap(true);
      valueLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
      valueLbl->setStyleSheet("color:#d8d8d8;font-size:13px;background:transparent;");
      cl->addWidget(valueLbl);

      connect(copyOne, &QPushButton::clicked, this, [this, addrText]() {
        QApplication::clipboard()->setText(addrText);
        setStatusMessage("Address copied.");
      });
      listLayout->addWidget(card);
    }
  };

  connect(copyBtn, &QPushButton::clicked, this, [this, addr]() {
    QApplication::clipboard()->setText(addr->text());
    setStatusMessage("Address copied.");
  });
  connect(genBtn, &QPushButton::clicked, this, [this, refreshAddrs]() {
    QString err;
    const QString a = service_->generateNewAddress(QString(), &err);
    if (a.isEmpty()) { setStatusMessage(err, true); return; }
    QApplication::clipboard()->setText(a);
    QMessageBox::information(this, "New Address", "New address created and copied:\n\n" + a);
    refreshAddrs();
    refreshAll();
  });
  connect(genBatchBtn, &QPushButton::clicked, this, [this, refreshAddrs]() {
    QStringList created;
    for (int i = 0; i < 5; ++i) {
      QString err;
      const QString a = service_->generateNewAddress(QString(), &err);
      if (a.isEmpty()) { setStatusMessage(err, true); break; }
      created << a;
    }
    if (!created.isEmpty()) {
      QMessageBox::information(this, "New Addresses", "New addresses created:\n\n" + created.join("\n"));
    }
    refreshAddrs();
    refreshAll();
  });
  connect(service_, &WalletService::walletChanged, this, refreshAddrs);
  refreshAddrs();
  return page;
}

QWidget* MainWindow::buildTransactionsPage() {
  auto* page = new QWidget();
  auto* l = new QVBoxLayout(page);
  l->setContentsMargins(24, 24, 24, 24);
  l->setSpacing(16);
  auto* top = new QHBoxLayout();
  top->addWidget(sectionTitle("Transactions / UTXOs"));
  top->addStretch(1);
  auto* refreshBtn = new QPushButton("Refresh");
  refreshBtn->setObjectName("ghostBtn");
  top->addWidget(refreshBtn);
  l->addLayout(top);

  txTable_ = new QTableWidget(0, 6);
  txTable_->setHorizontalHeaderLabels({"TXID", "Vout", "Amount", "Height", "Spent", "Account"});
  txTable_->horizontalHeader()->setStretchLastSection(true);
  txTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  txTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  txTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  txTable_->setAlternatingRowColors(true);
  txTable_->verticalHeader()->setVisible(false);
  l->addWidget(txTable_, 1);
  connect(refreshBtn, &QPushButton::clicked, this, [this]() { refreshTransactions(); setStatusMessage("Transactions refreshed."); });
  return page;
}

QWidget* MainWindow::buildPoiPage() {
  auto* page = new QWidget();
  auto* l = new QVBoxLayout(page);
  l->setContentsMargins(24, 24, 24, 24);
  l->setSpacing(16);

  auto* box = new QFrame();
  box->setObjectName("card");
  auto* g = new QGridLayout(box);
  g->setContentsMargins(20, 20, 20, 20);
  g->setHorizontalSpacing(16);
  g->setVerticalSpacing(14);
  g->setColumnStretch(1, 1);
  g->addWidget(sectionTitle("PoI"), 0, 0, 1, 4);

  poiEnable_ = new QCheckBox("Enable PoI contribution");
  poiEnable_->setCursor(Qt::PointingHandCursor);
  g->addWidget(poiEnable_, 1, 0, 1, 4);

  auto* bwLbl = new QLabel("Bandwidth limit");
  bwLbl->setWordWrap(true);
  g->addWidget(bwLbl, 2, 0);
  bandwidthSpin_ = new QSpinBox();
  bandwidthSpin_->setRange(1, 1024 * 1024);
  bandwidthSpin_->setValue(100);
  bandwidthUnit_ = new QComboBox();
  bandwidthUnit_->addItems({"MB", "GB", "TB"});
  auto* bwWrap = new QWidget();
  auto* bwl = new QHBoxLayout(bwWrap);
  bwl->setContentsMargins(0, 0, 0, 0);
  bwl->setSpacing(8);
  bwl->addWidget(bandwidthSpin_, 1);
  bwl->addWidget(bandwidthUnit_);
  g->addWidget(bwWrap, 2, 1, 1, 3);

  auto* domLbl = new QLabel("PoI Domain / Verifier endpoint");
  domLbl->setWordWrap(true);
  g->addWidget(domLbl, 3, 0);
  poiDomainEdit_ = new QLineEdit();
  poiDomainEdit_->setPlaceholderText("example: verifier.example.org:9000");
  g->addWidget(poiDomainEdit_, 3, 1, 1, 3);

  poiStartBtn_ = new QPushButton("Start PoI");
  poiStartBtn_->setObjectName("primaryBtn");
  poiStartBtn_->setMinimumHeight(36);
  poiStopBtn_ = new QPushButton("Stop PoI");
  poiStopBtn_->setObjectName("dangerBtn");
  poiStopBtn_->setMinimumHeight(36);
  auto* applyBtn = new QPushButton("Apply Settings");
  applyBtn->setMinimumHeight(36);
  auto* refreshBtn = new QPushButton("Refresh Status");
  refreshBtn->setObjectName("ghostBtn");
  refreshBtn->setMinimumHeight(36);
  auto* btnRow = new QHBoxLayout();
  btnRow->setSpacing(10);
  btnRow->addWidget(poiStartBtn_);
  btnRow->addWidget(poiStopBtn_);
  btnRow->addWidget(applyBtn);
  btnRow->addWidget(refreshBtn);
  btnRow->addStretch(1);
  g->addLayout(btnRow, 4, 0, 1, 4);

  poiPeersLabel_ = new QLabel("Connected Peers (Node): Not Available");
  poiPeersLabel_->setObjectName("muted");
  poiPeersLabel_->setWordWrap(true);
  g->addWidget(poiPeersLabel_, 5, 0, 1, 4);

  l->addWidget(box);

  auto* statusCard = new QFrame();
  statusCard->setObjectName("card");
  auto* scl = new QVBoxLayout(statusCard);
  scl->setContentsMargins(20, 16, 20, 16);
  scl->setSpacing(8);
  scl->addWidget(sectionTitle("Status"));
  poiStatusLabel_ = new QLabel("Status: Not Available");
  poiStatusLabel_->setWordWrap(true);
  poiStatusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  poiStatusLabel_->setStyleSheet("color:#d4d4d4;background:transparent;");
  scl->addWidget(poiStatusLabel_);
  l->addWidget(statusCard);

  auto* backendCard = new QFrame();
  backendCard->setObjectName("card");
  auto* bcl = new QVBoxLayout(backendCard);
  bcl->setContentsMargins(20, 16, 20, 16);
  bcl->setSpacing(8);
  bcl->addWidget(sectionTitle("Backend"));
  poiBackendLabel_ = new QLabel();
  poiBackendLabel_->setWordWrap(true);
  poiBackendLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  poiBackendLabel_->setStyleSheet("color:#d4d4d4;background:transparent;");
  bcl->addWidget(poiBackendLabel_);
  l->addWidget(backendCard);

  auto* noteBox = new QFrame();
  noteBox->setObjectName("card");
  auto* nl = new QVBoxLayout(noteBox);
  nl->setContentsMargins(20, 16, 20, 16);
  nl->setSpacing(8);
  nl->addWidget(sectionTitle("Notes"));
  auto* note = new QLabel(
      "PoI bandwidth settings are stored in local Qt wallet config (qt-wallet.ini in datadir). "
      "Values can be set in MB/GB/TB. Start/Stop PoI sends best-effort RPC to the node; "
      "if the backend does not support control/enforcement yet, the UI shows Not Available honestly.");
  note->setWordWrap(true);
  note->setObjectName("muted");
  nl->addWidget(note);
  l->addWidget(noteBox);
  l->addStretch(1);

  connect(applyBtn, &QPushButton::clicked, this, [this]() {
    MiningSettings s = service_->miningSettings();
    s.enabled = poiEnable_->isChecked();
    s.bandwidthValue = static_cast<quint64>(bandwidthSpin_->value());
    s.bandwidthUnit = bandwidthUnit_->currentText().toUpper();
    s.domain = poiDomainEdit_->text().trimmed();
    service_->setMiningSettings(s);
    setStatusMessage("PoI settings saved: " + s.displayLimit());
    refreshPoi();
  });
  connect(refreshBtn, &QPushButton::clicked, this, [this]() { refreshPoi(); });
  connect(poiStartBtn_, &QPushButton::clicked, this, [this]() {
    MiningSettings s = service_->miningSettings();
    s.enabled = true;
    s.bandwidthValue = static_cast<quint64>(bandwidthSpin_->value());
    s.bandwidthUnit = bandwidthUnit_->currentText().toUpper();
    s.domain = poiDomainEdit_->text().trimmed();
    service_->setMiningSettings(s);
    service_->startPoi();
    refreshPoi();
  });
  connect(poiStopBtn_, &QPushButton::clicked, this, [this]() {
    service_->stopPoi();
    refreshPoi();
  });

  return page;
}

QWidget* MainWindow::buildSettingsPage() {
  auto* page = new QWidget();
  auto* l = new QVBoxLayout(page);
  l->setContentsMargins(24, 24, 24, 24);
  l->setSpacing(16);

  auto* walletBox = new QFrame();
  walletBox->setObjectName("card");
  auto* wl = new QVBoxLayout(walletBox);
  wl->setContentsMargins(16, 16, 16, 16);
  wl->setSpacing(10);
  wl->addWidget(sectionTitle("Wallet Management"));
  keystorePathLabel_ = new QLabel();
  keystorePathLabel_->setObjectName("muted");
  keystorePathLabel_->setWordWrap(true);
  keystorePathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  wl->addWidget(keystorePathLabel_);

  auto* wBtns = new QHBoxLayout();
  auto* createBtn = new QPushButton("Create New Wallet");
  auto* loadBtn = new QPushButton("Open/Load Wallet");
  auto* backupBtn = new QPushButton("Backup Keystore");
  createBtn->setObjectName("primaryBtn");
  wBtns->addWidget(createBtn);
  wBtns->addWidget(loadBtn);
  wBtns->addWidget(backupBtn);
  wBtns->addStretch(1);
  wl->addLayout(wBtns);

  auto* importRow = new QHBoxLayout();
  auto* importEdit = new QLineEdit();
  importEdit->setPlaceholderText("Import secret hex (local only, never sent to server)");
  importEdit->setEchoMode(QLineEdit::Password);
  auto* importBtn = new QPushButton("Import Secret");
  importRow->addWidget(importEdit, 1);
  importRow->addWidget(importBtn);
  wl->addLayout(importRow);

  auto* treasuryRow = new QHBoxLayout();
  auto* treasuryEdit = new QLineEdit();
  treasuryEdit->setPlaceholderText("Treasury address (primary address in keystore)");
  treasuryEdit->setText(service_->treasuryAddress());
  auto* setTreasuryBtn = new QPushButton("Set Treasury");
  auto* sweepCheck = new QCheckBox("Auto-sweep to Treasury");
  sweepCheck->setChecked(service_->autoSweepToTreasury());
  treasuryRow->addWidget(new QLabel("Treasury"));
  treasuryRow->addWidget(treasuryEdit, 1);
  treasuryRow->addWidget(setTreasuryBtn);
  treasuryRow->addWidget(sweepCheck);
  wl->addLayout(treasuryRow);
  connect(setTreasuryBtn, &QPushButton::clicked, this, [this, treasuryEdit, sweepCheck]() {
    service_->setTreasuryAddress(treasuryEdit->text().trimmed());
    service_->setAutoSweepToTreasury(sweepCheck->isChecked());
    setStatusMessage("Treasury settings saved.");
    refreshAll();
  });
  connect(sweepCheck, &QCheckBox::toggled, this, [this, sweepCheck](bool checked){ service_->setAutoSweepToTreasury(checked); });
  connect(service_, &WalletService::walletChanged, this, [this, treasuryEdit, sweepCheck]() {
    if (treasuryEdit->text().isEmpty()) treasuryEdit->setText(service_->treasuryAddress());
    sweepCheck->setChecked(service_->autoSweepToTreasury());
  });
  l->addWidget(walletBox);

  auto* netBox = new QFrame();
  netBox->setObjectName("card");
  auto* g = new QGridLayout(netBox);
  g->setContentsMargins(16, 16, 16, 16);
  g->setHorizontalSpacing(12);
  g->setVerticalSpacing(10);
  g->addWidget(sectionTitle("Node / Network"), 0, 0, 1, 2);

  networkCombo_ = new QComboBox();
  networkCombo_->addItems({"mainnet", "testnet", "regtest"});
  datadirEdit_ = new QLineEdit();
  rpcEdit_ = new QLineEdit();
  auto* applyBtn = new QPushButton("Apply Settings");
  applyBtn->setObjectName("primaryBtn");
  auto* reloadBtn = new QPushButton("Reload Defaults");
  reloadBtn->setObjectName("ghostBtn");

  g->addWidget(new QLabel("Network"), 1, 0);
  g->addWidget(networkCombo_, 1, 1);
  g->addWidget(new QLabel("Datadir"), 2, 0);
  g->addWidget(datadirEdit_, 2, 1);
  g->addWidget(new QLabel("Node RPC"), 3, 0);
  g->addWidget(rpcEdit_, 3, 1);
  g->addWidget(applyBtn, 4, 1, Qt::AlignLeft);
  g->addWidget(reloadBtn, 4, 1, Qt::AlignRight);
  l->addWidget(netBox);

  auto* hint = new QLabel("Config is created automatically in the datadir as phantomx.conf (commented template). "
                          "The Qt wallet stores UI/PoI preferences in qt-wallet.ini in the same datadir.");
  hint->setWordWrap(true);
  hint->setObjectName("muted");
  l->addWidget(hint);
  l->addStretch(1);

  connect(createBtn, &QPushButton::clicked, this, [this]() {
    if (service_->hasWallet()) {
      auto ans = QMessageBox::question(this, "Create Wallet", "A wallet already exists. Creating a new wallet will replace the default key in this keystore. Continue?");
      if (ans != QMessageBox::Yes) return;
    }
    QString err;
    if (service_->createWallet(&err)) setStatusMessage("Wallet created: " + service_->address());
    else setStatusMessage(err, true);
    refreshAll();
  });
  connect(loadBtn, &QPushButton::clicked, this, [this]() {
    QString err;
    if (service_->loadWallet(&err)) setStatusMessage("Wallet loaded: " + service_->address());
    else setStatusMessage(err, true);
    refreshAll();
  });
  connect(importBtn, &QPushButton::clicked, this, [this, importEdit]() {
    QString err;
    if (service_->importWallet(importEdit->text(), &err)) { importEdit->clear(); setStatusMessage("Wallet imported."); }
    else setStatusMessage(err, true);
    refreshAll();
  });
  connect(backupBtn, &QPushButton::clicked, this, [this]() {
    const QString dest = QFileDialog::getSaveFileName(this, "Backup Wallet Keystore", service_->datadir() + "/wallet/wallet.keystore.bak");
    if (dest.isEmpty()) return;
    QString err;
    if (service_->backupWallet(dest, &err)) setStatusMessage("Backup saved: " + dest);
    else setStatusMessage(err, true);
  });
  connect(applyBtn, &QPushButton::clicked, this, [this]() {
    const QString oldDatadir = service_->datadir();
    const QString oldRpc = service_->nodeRpc();
    const QString oldNet = service_->network();
    const QString newNet = networkCombo_->currentText().trimmed();
    const QString newDatadir = datadirEdit_->text().trimmed();
    const QString newRpc = rpcEdit_->text().trimmed();

    if (newNet != oldNet) service_->setNetwork(newNet);
    // Only apply datadir if user explicitly changed it from the previous value.
    if (!newDatadir.isEmpty() && newDatadir != oldDatadir) service_->setDatadir(newDatadir);
    // Only apply RPC if user explicitly changed it from the previous value.
    if (!newRpc.isEmpty() && newRpc != oldRpc) service_->setNodeRpc(newRpc);
    setStatusMessage("Settings applied.");
    refreshAll();
  });
  connect(reloadBtn, &QPushButton::clicked, this, [this]() { refreshAll(); setStatusMessage("Defaults reloaded."); });
  return page;
}

void MainWindow::setupStyle() {
  setStyleSheet(R"QSS(
    QMainWindow, QWidget { background: #2d2d2d; color: #e6e6e6; font-family: 'Inter', 'Segoe UI', 'Ubuntu', sans-serif; font-size: 13px; }
    #sidebar { background: #1f1f1f; min-width: 230px; border-right: 1px solid #333333; }
    #logo { color: #ffffff; font-size: 22px; font-weight: 800; letter-spacing: 2px; background: transparent; }
    #logoSub { color: #a8a8a8; font-size: 11px; letter-spacing: 1px; background: transparent; }
    #sideNote { color: #9a9a9a; font-size: 11px; background: transparent; }
    #topbar { background: #2d2d2d; border-bottom: 1px solid #3a3a3a; }
    #topTitle { font-size: 18px; font-weight: 700; color: #f2f2f2; background: transparent; }
    #statusPill { background: transparent; color: #F26822; border: none; padding: 0; font-weight: 600; }
    #muted { color: #c2c2c2; background: transparent; }
    #card { background: #333333; border: 1px solid #404040; border-radius: 10px; }
    #addressCard { background: #333333; border: 1px solid #404040; border-radius: 10px; }
    #addrBadge { background: #F26822; color: #ffffff; border-radius: 8px; padding: 3px 10px; font-size: 11px; font-weight: 700; }
    #cardTitle { color: #a8a8a8; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px; background: transparent; }
    #cardValue { color: #f2f2f2; font-size: 22px; font-weight: 700; background: transparent; }
    #sectionTitle { font-size: 16px; font-weight: 700; color: #f2f2f2; background: transparent; }
    #addressBig { font-family: 'Consolas', 'DejaVu Sans Mono', monospace; font-size: 14px; color: #f2f2f2; background: transparent; }
    #warnBanner { background: #4a3518; color: #ffcc80; border: 1px solid #6b4a1f; border-radius: 8px; padding: 10px 12px; font-weight: 600; }
    QPushButton { background: #3a3a3a; border: 1px solid #4a4a4a; border-radius: 6px; padding: 8px 14px; color: #f2f2f2; font-weight: 600; }
    QPushButton:hover { background: #454545; }
    QPushButton:pressed { background: #505050; }
    QPushButton:disabled { color: #777777; background: #333333; border-color: #3d3d3d; }
    QPushButton#primaryBtn { background: #F26822; color: #ffffff; border-color: #F26822; }
    QPushButton#primaryBtn:hover { background: #ff7a35; }
    QPushButton#primaryBtn:pressed { background: #d85a1c; }
    QPushButton#dangerBtn { background: transparent; color: #ff6b6b; border-color: #7a3a3a; }
    QPushButton#dangerBtn:hover { background: #4a2a2a; }
    QPushButton#ghostBtn { background: transparent; border-color: #4a4a4a; color: #d0d0d0; }
    QPushButton#ghostBtn:hover { background: #3a3a3a; }
    QPushButton#navBtn { text-align: left; padding: 10px 14px; background: transparent; border: 0; color: #cfcfcf; font-size: 14px; font-weight: 600; }
    QPushButton#navBtn:hover { background: #333333; color: #ffffff; border-radius: 6px; }
    QPushButton#navBtn:checked { background: #3a3a3a; color: #F26822; border-left: 3px solid #F26822; border-radius: 0px 6px 6px 0px; }
    QLineEdit, QComboBox, QSpinBox { background: #363636; border: 1px solid #4a4a4a; border-radius: 6px; padding: 8px 10px; color: #f2f2f2; selection-background-color: #F26822; }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #F26822; }
    QComboBox::drop-down { border: none; width: 24px; }
    QComboBox QAbstractItemView { background: #363636; color: #f2f2f2; selection-background-color: #F26822; border: 1px solid #4a4a4a; }
    QTableWidget { background: #333333; border: 1px solid #444444; border-radius: 8px; gridline-color: #404040; color: #e6e6e6; alternate-background-color: #363636; }
    QHeaderView::section { background: #2a2a2a; color: #b0b0b0; border: 0; border-bottom: 1px solid #444444; padding: 8px; font-weight: 700; }
    QProgressBar { border: 1px solid #4a4a4a; border-radius: 6px; background: #363636; text-align: center; color: #f2f2f2; height: 18px; }
    QProgressBar::chunk { background: #F26822; border-radius: 5px; }
    QCheckBox { spacing: 8px; color: #e6e6e6; background: transparent; }
    QCheckBox::indicator { width: 16px; height: 16px; border-radius: 3px; border: 1px solid #666666; background: #363636; }
    QCheckBox::indicator:checked { background: #F26822; border-color: #F26822; }
    QScrollArea { background: transparent; border: none; }
    QScrollBar:vertical { background: #2d2d2d; width: 10px; margin: 0; }
    QScrollBar::handle:vertical { background: #555555; min-height: 20px; border-radius: 5px; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QStatusBar { background: #252525; color: #b0b0b0; border-top: 1px solid #3a3a3a; }
    QStatusBar QLabel { background: transparent; color: #b0b0b0; }
    QLabel { background: transparent; }
  )QSS");
}

void MainWindow::setupConnections() {
  connect(pollTimer_, &QTimer::timeout, this, [this]() { refreshAll(); });
  connect(service_, &WalletService::message, this, [this](const QString& text, bool err) { setStatusMessage(text, err); });
}

void MainWindow::showPage(int index) {
  stack_->setCurrentIndex(index);
  auto buttons = findChildren<QPushButton*>("navBtn");
  for (int i = 0; i < buttons.size(); ++i) buttons[i]->setChecked(i == index);
  if (index == 2) emit service_->walletChanged();
  if (index == 3) refreshTransactions();
  if (index == 4) refreshPoi();
}

void MainWindow::refreshAll() {
  auto status = service_->refreshNodeStatus();
  const bool online = status.value("online").toBool();
  const bool localData = service_->usingLocalData();
  statusLabel_->setText(online ? "Node: Online" : (localData ? "Node: Offline (Local Data)" : "Node: Offline"));
  statusLabel_->setStyleSheet(online
    ? "background:transparent;color:#1d7a46;border:none;padding:0;font-weight:600;"
    : (localData
        ? "background:transparent;color:#1a5f9a;border:none;padding:0;font-weight:600;"
        : "background:transparent;color:#9a5a00;border:none;padding:0;font-weight:600;"));
  networkLabel_->setText("Network: " + service_->network());
  if (networkCombo_) networkCombo_->setCurrentText(service_->network());
  if (datadirEdit_) datadirEdit_->setText(service_->datadir());
  if (rpcEdit_) rpcEdit_->setText(service_->nodeRpc());
  if (keystorePathLabel_) keystorePathLabel_->setText("Keystore: " + service_->keystorePath());
  // bottom-left sync status
  if (syncStatusBarLabel_) {
    syncStatusBarLabel_->setStyleSheet("background: transparent; padding: 0; margin: 0;");
    syncStatusBarLabel_->setWordWrap(true);
    if (online) {
      if (service_->syncing()) syncStatusBarLabel_->setText("Downloading");
      else syncStatusBarLabel_->setText("Synced");
    } else if (localData) {
      syncStatusBarLabel_->setText("Synced");
    } else {
      syncStatusBarLabel_->setText("Not Available");
    }
  }
  refreshOverview();
  refreshPoi();
  if (sidebarBalanceLabel_) {
    if (service_->nodeOnline() || service_->usingLocalData()) sidebarBalanceLabel_->setText(atomsToPhx(service_->balanceAtoms()));
    else sidebarBalanceLabel_->setText("Not Available");
  }
  if (sidebarSyncLabel_) {
    if (service_->nodeOnline()) sidebarSyncLabel_->setText(service_->syncing() ? "Downloading" : "Synced");
    else if (service_->usingLocalData()) sidebarSyncLabel_->setText("Synced");
    else sidebarSyncLabel_->setText("Not Available");
  }
}

void MainWindow::refreshOverview() {
  const bool online = service_->nodeOnline();
  const bool localData = service_->usingLocalData();

  if (online || localData) balanceLabel_->setText(atomsToPhx(service_->balanceAtoms()));
  else balanceLabel_->setText("Not Available");

  if (online || localData) heightLabel_->setText(QString::number(service_->chainHeight()));
  else heightLabel_->setText("Not Available");

  connLabel_->setText(online ? QString::number(service_->connectionCount()) : "Not Available");
  addressLabel_->setText(service_->hasWallet() ? service_->address() : "No wallet loaded");

  backupBanner_->setVisible(service_->autoCreatedWallet());
  if (service_->autoCreatedWallet()) {
    backupBanner_->setText("A default wallet was created automatically. Back up your keystore in Settings > Wallet Management as soon as possible.");
  }

  // Sync is shown in the bottom-left status bar (short: Downloading / Synced).
  (void)online;
  (void)localData;

  if (localData && !online) overviewStatus_->setText(service_->dataSourceNote());
  else if (!service_->rpcError().isEmpty()) overviewStatus_->setText("RPC: " + service_->rpcError());
}

void MainWindow::refreshTransactions() {
  const auto rows = service_->transactionHistory();
  txTable_->setRowCount(0);
  for (const auto& v : rows) {
    const auto m = v.toMap();
    const int row = txTable_->rowCount();
    txTable_->insertRow(row);
    txTable_->setItem(row, 0, new QTableWidgetItem(m.value("txid").toString()));
    txTable_->setItem(row, 1, new QTableWidgetItem(QString::number(m.value("vout").toUInt())));
    txTable_->setItem(row, 2, new QTableWidgetItem(atomsToPhx(m.value("amount_atoms").toULongLong())));
    txTable_->setItem(row, 3, new QTableWidgetItem(QString::number(m.value("height").toUInt())));
    txTable_->setItem(row, 4, new QTableWidgetItem(m.value("spent").toBool() ? "yes" : "no"));
    txTable_->setItem(row, 5, new QTableWidgetItem(m.value("account").toString()));
  }
  if (rows.isEmpty()) {
    txTable_->setRowCount(1);
    QString msg;
    if (service_->nodeOnline()) msg = "No transactions / UTXOs";
    else if (service_->usingLocalData()) msg = "No local UTXOs found in downloaded blockchain data for this wallet.";
    else msg = "Not Available (node offline and local blockchain data not available)";
    auto* item = new QTableWidgetItem(msg);
    item->setFlags(Qt::ItemIsEnabled);
    txTable_->setItem(0, 0, item);
    txTable_->setSpan(0, 0, 1, 6);
  }
}

void MainWindow::refreshPoi() {
  if (!poiEnable_) return;
  const auto m = service_->miningSettings();
  poiEnable_->setChecked(m.enabled);
  bandwidthSpin_->setValue(static_cast<int>(std::min<quint64>(m.bandwidthValue, 1024ULL * 1024ULL)));
  bandwidthUnit_->setCurrentText(m.bandwidthUnit.toUpper());
  poiPeersLabel_->setText(service_->nodeOnline()
                              ? QString("Connected Peers (Node): %1").arg(service_->connectionCount())
                              : QString("Connected Peers (Node): Not Available"));

  if (poiDomainEdit_ && poiDomainEdit_->text().isEmpty()) poiDomainEdit_->setText(m.domain);
  const auto st = service_->miningStatus();
  const QString poi = st.value("poi_available").toBool() ? st.value("poi_status").toString() : "Not Available";
  const QString run = st.value("running").toBool() ? "running" : "stopped";
  const QString score = st.value("poi_score").isValid() ? st.value("poi_score").toString() : QString("Not Available");
  const QString epoch = st.value("poi_epoch").isValid() ? st.value("poi_epoch").toString() : QString("Not Available");
  const QString miners = st.value("poi_active_miners").isValid() ? st.value("poi_active_miners").toString() : QString("Not Available");
  const QString targetMs = st.value("poi_target_block_interval_ms").isValid() ? st.value("poi_target_block_interval_ms").toString() : QString("Not Available");
  const QString latMs = st.value("poi_latency_ms").isValid() ? st.value("poi_latency_ms").toString() : QString("Not Available");
  const QString relRatio = st.value("poi_relay_ratio").isValid() ? st.value("poi_relay_ratio").toString() : QString("Not Available");
  const QString sBw = st.value("poi_score_bandwidth").isValid() ? st.value("poi_score_bandwidth").toString() : QString("Not Available");
  const QString sLat = st.value("poi_score_latency").isValid() ? st.value("poi_score_latency").toString() : QString("Not Available");
  const QString sRel = st.value("poi_score_relay").isValid() ? st.value("poi_score_relay").toString() : QString("Not Available");
  const QString sNet = st.value("poi_score_network").isValid() ? st.value("poi_score_network").toString() : QString("Not Available");
  const QString scoreNote = st.value("poi_score_partial").toBool() ? " (partial: some components Not Available)" : "";
  poiStatusLabel_->setText(
      QString("Running: %1\nEnabled setting: %2\nBandwidth limit: %3\nDomain: %4\nPoI RPC: %5\nScore: %6%10\nEpoch (dynamic): %7\nActive miners: %8\nTarget block interval: %9 ms\nLatency: %13 ms (comp %14)\nRelay ratio: %15 (comp %16)\nBandwidth comp: %17 | Network comp: %18\nNode: %11\nConnected Peers (Node): %12")
          .arg(run,
               m.enabled ? "true" : "false",
               m.displayLimit(),
               m.domain.isEmpty() ? "Not set" : m.domain,
               poi,
               score,
               epoch,
               miners,
               targetMs,
               scoreNote,
               service_->nodeOnline() ? "online" : "offline",
               service_->nodeOnline() ? QString::number(service_->connectionCount()) : "Not Available",
               latMs, sLat, relRatio, sRel, sBw, sNet));
  poiBackendLabel_->setText(st.value("backend_note").toString() + "\nRumus: epoch=max(1,ceil(active_miners/10)); score=100*(0.35*bw+0.25*lat+0.25*relay+0.15*miners), komponen NA direweight; T_find=clamp(60000/(F_score*F_epoch),15000,300000).");
}

void MainWindow::setStatusMessage(const QString& text, bool isError) {
  if (overviewStatus_) {
    overviewStatus_->setText(text);
    overviewStatus_->setStyleSheet(isError ? "color:#ff8080;background:transparent;" : "color:#c2c2c2;background:transparent;");
  }
  statusBar()->showMessage(text, 5000);
}
