/**
 * SystemAlertAttention_stub.cpp —— 非 Apple 平台的注意请求。
 *
 * 暂用 `QApplication::alert(window, 0)`：Windows 上闪任务栏按钮，直到窗口被激活。
 * **撤不回来**——对方取消后还会闪到用户切回来。要撤得改成 `FlashWindowEx`（开始 `FLASHW_ALL | FLASHW_TIMERNOFG`，
 * 撤时 `FLASHW_STOP`），那要在 Windows 机器上写和验，本机做不了。
 */

#include <QApplication>

#include "SystemAlertSink.h"

void attention::request(QWidget* window) { QApplication::alert(window, 0); }

void attention::cancel() {}
