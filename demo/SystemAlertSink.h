#pragma once

/**
 * SystemAlertSink.h —— `IncomingAlert` 的系统那一侧：托盘通知 + Dock / 任务栏注意请求。
 *
 * **通知走 `QSystemTrayIcon::showMessage`**，不引第三方库：
 *   - macOS：Qt 的 cocoa 插件走 `NSUserNotificationCenter`，点通知回 `messageClicked`；
 *     发通知要先把托盘图标 `show()` 出来，所以响铃期间菜单栏里会多一个听筒图标，`withdraw()` 时收掉。
 *   - Windows：通知区域气泡 / Toast，点它同样回 `messageClicked`。
 *   - 点托盘图标本身也算「点了提醒」。
 *
 * **撤通知 Qt 没有接口**：`withdraw()` 只能把托盘图标藏起来。Windows 上藏图标会把气泡一起收掉；
 * macOS 上已经进了通知中心的那条**撤不掉**，通话结束后点它只会激活应用（`IncomingAlert` 不再展开）。
 *
 * **注意请求是平台代码**（`SystemAlertAttention_*.mm/.cpp`）：macOS 自己调
 * `requestUserAttention:NSCriticalRequest` 并留着返回的请求号，好在接通 / 取消时 `cancelUserAttentionRequest:`——
 * `QApplication::alert` 发出去就撤不回来，对方取消了 Dock 还会一直跳到用户切回来。
 * 非 Apple 平台暂用 `QApplication::alert`（Windows 闪任务栏到窗口被激活为止，撤不回来）。
 */

#include <QObject>

#include <functional>

#include "IncomingAlert.h"

class QSystemTrayIcon;

class SystemAlertSink : public QObject, public IncomingAlert::Sink {
  Q_OBJECT

public:
  /** `onClicked`：通知或托盘图标被点时调。 */
  explicit SystemAlertSink(std::function<void()> onClicked);
  ~SystemAlertSink() override;

  void post(const QString& title, const QString& body) override;
  void withdraw() override;
  void requestAttention(QWidget* window) override;
  void cancelAttention() override;

private:
  void ensureTray();

  std::function<void()> onClicked_;
  QSystemTrayIcon* tray_ = nullptr;
};

/** Dock / 任务栏注意请求。平台实现在 SystemAlertAttention_mac.mm / _stub.cpp。 */
namespace attention {

void request(QWidget* window);
void cancel();

}  // namespace attention
