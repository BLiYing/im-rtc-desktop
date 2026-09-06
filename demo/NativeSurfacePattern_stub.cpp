/**
 * NativeSurfacePattern_stub.cpp —— 假渲染器在非 Apple 平台上的占位。
 *
 * **不假装成功**：`testPatternSupported()` 返回 false，界面据此说明
 * "这个平台还没有假渲染器"，而不是显示一块什么都没有的黑格子让人以为坏了。
 *
 * Windows 侧要做的话是一个 `CreateWindowEx` 的子窗口 + GDI/D3D 填色，
 * 或者干脆等 `WebRTCAdapter` 直接接管——句柄本身（`HWND`）不需要这一层就能验。
 */

#include "NativeSurface.h"

bool NativeSurface::testPatternSupported() { return false; }

void NativeSurface::applyTestPattern() {}
void NativeSurface::removeTestPattern() {}
void NativeSurface::syncPatternGeometry() {}

QRectF NativeSurface::patternGeometry() const { return {}; }
qreal NativeSurface::patternScale() const { return 0.0; }
