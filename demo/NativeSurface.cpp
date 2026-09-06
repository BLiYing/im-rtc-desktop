#include "NativeSurface.h"

#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>

NativeSurface::NativeSurface(QWidget* parent) : QWidget(parent) {
  /*
    这两个属性缺一不可：

    - WA_NativeWindow            让这个 widget 真的有一个 NSView / HWND，
                                 否则 winId() 拿到的是它某个祖先的句柄，
                                 引擎会画到整块面板上而不是这一格里。
    - WA_DontCreateNativeAncestors  **只让自己变原生，别把祖先一路提升**。
                                 不加它的话 Qt 会把整条父链都变成原生窗口，
                                 那会连累其他控件的绘制与 z 序。
  */
  setAttribute(Qt::WA_NativeWindow, true);
  setAttribute(Qt::WA_DontCreateNativeAncestors, true);
  // 原生层自己负责画满，Qt 不用再刷一遍背景。
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setMinimumSize(16, 16);
}

NativeSurface::~NativeSurface() {
  // 析构前先让宿主摘掉句柄。**顺序反了就是野指针**：
  // 引擎还拿着一个已经销毁的 NSView / HWND。
  emit detachRequested();
  removeTestPattern();
}

void* NativeSurface::nativeHandle() {
  // winId() 会在还没有原生窗口时**创建**一个，所以这一句既是查询也是保证。
  const WId id = winId();
  return reinterpret_cast<void*>(id);
}

qreal NativeSurface::backingScale() const {
  if (QWindow* handle = windowHandle()) return handle->devicePixelRatio();
  if (QScreen* screen = this->screen()) return screen->devicePixelRatio();
  return 1.0;
}

void NativeSurface::showTestPattern(const QString& label) {
  label_ = label;
  patternOn_ = true;
  applyTestPattern();
}

void NativeSurface::clearTestPattern() {
  patternOn_ = false;
  removeTestPattern();
}

void NativeSurface::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  ++resizeCount_;
  // 原生层不跟着 Qt 的布局走，得自己同步——这正是「缩放」那条风险。
  if (patternOn_) syncPatternGeometry();
}

void NativeSurface::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (patternOn_) {
    applyTestPattern();
    syncPatternGeometry();
  }
}
