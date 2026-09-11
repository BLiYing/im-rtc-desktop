/**
 * SystemAlertAttention_win.cpp —— 来电时闪任务栏按钮（UX_FLOWS §07 第 3 条：`FlashWindowEx`）。
 *
 * **不用 `QApplication::alert`**：它发出去就撤不回来——对方取消之后任务栏还会一直闪，
 * 直到用户自己切回来。这里自己调 `FlashWindowEx`，记住闪的是哪个窗口，接通 / 结束时 `FLASHW_STOP`。
 * `FLASHW_TIMERNOFG`：一直闪到窗口来到前台；用户切回来时系统自己停，与 macOS 上 AppKit 停跳 Dock 对称。
 *
 * **2026-09-11 在 Intel Mac 上写的，没在 Windows 上编译 / 验证过。** 要验的几条见 current_task.md。
 */

// windows.h 默认带 min / max 宏，会弄坏 std::min 之类；其余部分也用不着。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QWidget>

#include "SystemAlertSink.h"

namespace {

/** 正在闪的顶层窗口，没在闪是 nullptr。只在 UI 线程上动。 */
HWND gFlashing = nullptr;

void flash(HWND hwnd, DWORD flags) {
  FLASHWINFO info{};
  info.cbSize = sizeof(info);
  info.hwnd = hwnd;
  info.dwFlags = flags;
  info.uCount = 0;     // 持续闪，不按次数停
  info.dwTimeout = 0;  // 0 = 系统默认的光标闪烁间隔
  // 返回值是「调用之前窗口是否处于高亮态」，不是成败，没有可处理的。
  (void)FlashWindowEx(&info);
}

}  // namespace

void attention::request(QWidget* window) {
  if (window == nullptr) return;
  // internalWinId 不会替还没有原生句柄的窗口新建一个；主窗没显示过就没有任务栏按钮可闪。
  const WId id = window->window()->internalWinId();
  if (id == 0) return;
  gFlashing = reinterpret_cast<HWND>(id);
  flash(gFlashing, FLASHW_ALL | FLASHW_TIMERNOFG);
}

void attention::cancel() {
  if (gFlashing == nullptr) return;
  flash(gFlashing, FLASHW_STOP);
  gFlashing = nullptr;
}
