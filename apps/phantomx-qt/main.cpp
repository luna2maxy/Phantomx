#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>

#include "SplashScreen.hpp"
#include "WalletBridge.hpp"
#include "WalletService.hpp"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("PhantomX Wallet");
  app.setOrganizationName("PhantomX");

  SplashScreen splash;
  splash.show();
  app.processEvents();

  WalletService service;
  WalletBridge bridge(&service);

  QQmlApplicationEngine engine;
  engine.addImportPath(QStringLiteral("qrc:/"));
  engine.rootContext()->setContextProperty("walletBridge", &bridge);
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.loadFromModule("PhantomXWallet", "Main");

  QTimer::singleShot(900, &splash, [&splash, &engine]() {
    const auto roots = engine.rootObjects();
    QObject* root = roots.isEmpty() ? nullptr : roots.first();
    QWidget* w = nullptr;
    splash.finish(w);
    if (root) root->setProperty("visible", true);
  });

  QTimer* poll = new QTimer(&app);
  poll->setInterval(15000);
  QObject::connect(poll, &QTimer::timeout, &bridge, &WalletBridge::refresh);
  poll->start();
  QTimer::singleShot(100, &bridge, &WalletBridge::refresh);

  return app.exec();
}
