/**
 * SystemAlertAttention_stub.cpp —— 非 Apple、非 Windows 平台（Linux 等）的注意请求。
 *
 * 暂用 `QApplication::alert(window, 0)`：X11 / Wayland 上设紧急提示，直到窗口被激活。
 * **撤不回来**——对方取消后还会提示到用户切回来。macOS / Windows 各有自己的实现（`_mac.mm` / `_win.cpp`），能撤。
 */

#include <QApplication>

#include "SystemAlertSink.h"

void attention::request(QWidget* window) { QApplication::alert(window, 0); }

void attention::cancel() {}
