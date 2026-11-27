#include "SplashScreen.hpp"

#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>

SplashScreen::SplashScreen(QWidget* parent) : QWidget(parent) {
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::SplashScreen);
  setAttribute(Qt::WA_TranslucentBackground, false);
  setFixedSize(520, 320);
  if (auto* screen = QGuiApplication::primaryScreen()) {
    const auto geo = screen->availableGeometry();
    move(geo.center() - rect().center());
  }
  const QStringList candidates = {
      QStringLiteral(":/PhantomXWallet/assets/logo.png"),
      QStringLiteral(":/qt/qml/PhantomXWallet/assets/logo.png"),
      QStringLiteral("apps/phantomx-qt/assets/logo.png"),
  };
  for (const auto& c : candidates) {
    QPixmap px(c);
    if (!px.isNull()) { logo_ = px; break; }
  }
  timer_ = new QTimer(this);
  timer_->setInterval(350);
  connect(timer_, &QTimer::timeout, this, [this]() { dotCount_ = (dotCount_ + 1) % 4; update(); });
  timer_->start();
}

void SplashScreen::finish(QWidget* mainWindow) {
  if (timer_) timer_->stop();
  if (mainWindow) mainWindow->show();
  close();
}

void SplashScreen::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);

  // Background
  p.fillRect(rect(), QColor("#1b1b1b"));
  p.setPen(QPen(QColor("#2a2a2a"), 1));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  // Logo: preserve the source aspect ratio and fill the dedicated splash area.
  if (!logo_.isNull()) {
    const int logoWidth = width() - 120;
    const int logoHeight = qRound(static_cast<double>(logoWidth) * logo_.height() / logo_.width());
    const QSize target(logoWidth, logoHeight);
    const QPixmap scaled = logo_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int x = (width() - scaled.width()) / 2;
    const int y = 14;
    p.drawPixmap(x, y, scaled);
  } else {
    // Fallback mark
    const QRectF markRect(width() / 2.0 - 34, 72, 68, 68);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#F26822"));
    p.drawRoundedRect(markRect, 14, 14);
    p.setPen(QPen(QColor("#ffffff"), 6, Qt::SolidLine, Qt::RoundCap));
    const QRectF xr = markRect.adjusted(18, 18, -18, -18);
    p.drawLine(xr.topLeft(), xr.bottomRight());
    p.drawLine(xr.topRight(), xr.bottomLeft());
  }

  // Title
  QFont titleFont(font());
  titleFont.setPixelSize(30);
  titleFont.setBold(true);
  titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 2);
  p.setFont(titleFont);
  p.setPen(QColor("#f2f2f2"));
  p.drawText(QRect(0, 196, width(), 40), Qt::AlignCenter, "PHANTOMX");

  QFont subFont(font());
  subFont.setPixelSize(13);
  subFont.setLetterSpacing(QFont::AbsoluteSpacing, 1);
  p.setFont(subFont);
  p.setPen(QColor("#b0b0b0"));
  p.drawText(QRect(0, 232, width(), 24), Qt::AlignCenter, "DESKTOP WALLET");

  // Loading
  QString dots = QString(".").repeated(dotCount_);
  QFont loadFont(font());
  loadFont.setPixelSize(12);
  p.setFont(loadFont);
  p.setPen(QColor("#F26822"));
  p.drawText(QRect(0, 264, width(), 24), Qt::AlignCenter, QString("Loading%1").arg(dots));

  // Footer
  p.setPen(QColor("#666666"));
  QFont footFont(font());
  footFont.setPixelSize(11);
  p.setFont(footFont);
  p.drawText(QRect(0, height() - 34, width(), 20), Qt::AlignCenter, "PhantomX  \xE2\x80\xA2  Secure Local Wallet");
}
