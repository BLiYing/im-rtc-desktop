/**
 * SystemAlertAttention_mac.mm —— 来电时跳 Dock（UX_FLOWS §07 第 3 条：`NSApp.requestUserAttention`）。
 *
 * **不用 `QApplication::alert`**：它把请求号藏在 Qt 里，外面撤不回来——
 * 对方取消之后 Dock 还会一直跳，直到用户自己切回来。这里留着请求号，接通 / 结束时撤掉。
 * 应用本来就在前台时 AppKit 返回 0、不跳；用户切回应用时 AppKit 自己停跳。
 */

#import <AppKit/AppKit.h>

#include "SystemAlertSink.h"

namespace {

NSInteger gRequest = 0;

}  // namespace

void attention::request(QWidget* window) {
  (void)window;
  if (gRequest != 0) return;
  // Critical：一直跳到应用被激活或被撤；Informational 只跳一下，来电不够。
  gRequest = [NSApp requestUserAttention:NSCriticalRequest];
}

void attention::cancel() {
  if (gRequest == 0) return;
  [NSApp cancelUserAttentionRequest:gRequest];
  gRequest = 0;
}
