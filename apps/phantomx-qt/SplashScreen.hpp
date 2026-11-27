#pragma once

#include <QPixmap>
#include <QWidget>

class QTimer;

class SplashScreen final : public QWidget {
  Q_OBJECT
 public:
  explicit SplashScreen(QWidget* parent = nullptr);
  void finish(QWidget* mainWindow);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  int dotCount_ = 0;
  QTimer* timer_ = nullptr;
  QPixmap logo_;
};
