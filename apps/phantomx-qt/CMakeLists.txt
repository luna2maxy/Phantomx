cmake_minimum_required(VERSION 3.20)

find_package(Qt6 COMPONENTS Core Gui Widgets Network Qml Quick QuickControls2 QUIET)
if(NOT Qt6_FOUND)
  message(FATAL_ERROR "PHANTOMX_BUILD_QT=ON but Qt6 (Core Gui Widgets Network Qml Quick QuickControls2) was not found")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

qt_add_executable(phantomx-qt
  main.cpp
  MainWindow.cpp
  MainWindow.hpp
  SplashScreen.cpp
  SplashScreen.hpp
  WalletService.cpp
  WalletService.hpp
  WalletBridge.cpp
  WalletBridge.hpp
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(phantomx-qt
  URI PhantomXWallet
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/shell/Sidebar.qml
    qml/shell/TopBar.qml
    qml/shell/StatusBar.qml
    qml/components/AppButton.qml
    qml/components/AppCard.qml
    qml/components/AppBadge.qml
    qml/components/AppToggleSwitch.qml
    qml/components/AppInput.qml
    qml/components/AppComboBox.qml
    qml/components/AppSpinBox.qml
    qml/components/StatusRow.qml
    qml/components/StatusBanner.qml
    qml/components/LineEdit.qml
    qml/components/SegmentedControl.qml
    qml/components/SectionHeader.qml
    qml/components/AppTooltip.qml
    qml/components/HelpButton.qml
    qml/components/ConfirmDialog.qml
    qml/components/Toast.qml
    qml/components/EmptyState.qml
    qml/components/StyledScrollBar.qml
    qml/components/DateField.qml
    qml/components/SearchField.qml
    qml/components/ListContainer.qml
    qml/components/ListRow.qml
    qml/components/IconButton.qml
    qml/components/BalanceCard.qml
    qml/components/SyncSection.qml
    qml/components/SyncDashboard.qml
    qml/pages/PoiPage.qml
    qml/pages/OverviewPage.qml
    qml/pages/SendPage.qml
    qml/pages/ReceivePage.qml
    qml/pages/TransactionsPage.qml
    qml/pages/SettingsPage.qml
  RESOURCES
    assets/logo.png
)

target_link_libraries(phantomx-qt PRIVATE PhantomX::common Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2)
target_compile_features(phantomx-qt PRIVATE cxx_std_20)
phantomx_apply_warnings(phantomx-qt)
set_target_properties(phantomx-qt PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
