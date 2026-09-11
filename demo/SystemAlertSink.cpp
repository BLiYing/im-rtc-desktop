#include "SystemAlertSink.h"

#include <QSystemTrayIcon>

#include "Icons.h"
#include "Theme.h"

namespace {

/** 通知在屏幕上停多久（Windows 用；macOS 由系统设置决定）。一次振铃大约 30s。 */
constexpr int kMessageMs = 30000;
constexpr int kTrayIconSize = 32;

}  // namespace

SystemAlertSink::SystemAlertSink(std::function<void()> onClicked)
    : onClicked_(std::move(onClicked)) {}

SystemAlertSink::~SystemAlertSink() = default;

void SystemAlertSink::ensureTray() {
  if (tray_ != nullptr) return;
  tray_ = new QSystemTrayIcon(this);
  tray_->setObjectName(QStringLiteral("incomingAlertTray"));
  tray_->setIcon(icons::icon(icons::Name::Phone, kTrayIconSize, theme::call::accept()));
  connect(tray_, &QSystemTrayIcon::messageClicked, this, [this] { onClicked_(); });
  connect(tray_, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
              onClicked_();
            }
          });
}

void SystemAlertSink::post(const QString& title, const QString& body) {
  // 没有托盘（某些 Linux 桌面）就只剩注意请求；不假装发出去了。
  if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
  ensureTray();
  tray_->setToolTip(tr("im-rtc 来电"));
  tray_->show();
  tray_->showMessage(title, body, QSystemTrayIcon::Information, kMessageMs);
}

void SystemAlertSink::withdraw() {
  if (tray_ != nullptr) tray_->hide();
}

void SystemAlertSink::requestAttention(QWidget* window) { attention::request(window); }

void SystemAlertSink::cancelAttention() { attention::cancel(); }
