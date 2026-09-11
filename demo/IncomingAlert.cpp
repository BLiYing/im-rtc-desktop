#include "IncomingAlert.h"

#include <QWidget>

#include "CallStrings.h"
#include "SystemAlertSink.h"

namespace incomingalert {

bool needsSystemAlert(const WindowPresence& presence) {
  return !presence.visible || presence.minimized || !presence.active;
}

WindowPresence presenceOf(const QWidget* window) {
  WindowPresence presence;
  presence.visible = window->isVisible();
  presence.minimized = window->isMinimized();
  presence.active = window->isActiveWindow();
  return presence;
}

}  // namespace incomingalert

IncomingAlert::Sink::~Sink() = default;

IncomingAlert::IncomingAlert(QWidget* window, std::unique_ptr<Sink> sink)
    : QObject(window), window_(window), sink_(std::move(sink)) {
  if (!sink_) sink_ = std::make_unique<SystemAlertSink>([this] { notificationClicked(); });
}

IncomingAlert::~IncomingAlert() = default;

bool IncomingAlert::ring(const incomingalert::WindowPresence& presence, const QString& caller,
                         const QString& mediaType, bool isGroup) {
  if (!incomingalert::needsSystemAlert(presence)) return false;
  alerting_ = true;
  sink_->requestAttention(window_);
  sink_->post(caller,
              callstrings::incomingInviteText(mediaType == QLatin1String("video"), isGroup));
  return true;
}

void IncomingAlert::clear() {
  if (!alerting_) return;
  alerting_ = false;
  sink_->cancelAttention();
  sink_->withdraw();
}

bool IncomingAlert::isAlerting() const { return alerting_; }

void IncomingAlert::notificationClicked() {
  // 通话已经结束才点到旧通知：系统会自己激活应用，但不该再展开一个不存在的来电。
  if (!alerting_) return;
  clear();
  emit openRequested();
}
