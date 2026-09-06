#pragma once

/**
 * NativeSurface.h —— 渲染路径 A 的宿主侧（设计 §8.3）。
 *
 * 引擎要的是一个**原生窗口句柄**：Windows 上是 `HWND`，macOS 上是 `NSView*`。
 * 这个控件负责把 Qt 的一个 widget 变成"有真实原生窗口"的那种，并交出句柄。
 *
 * # 为什么现在就做，尽管还没有画面
 *
 * 真正的视频帧要等 `WebRTCAdapter`（已决定推迟）。但**把原生子窗口塞进 Qt 布局里**
 * 这件事的坑与视频无关：层级（原生子窗口会不会盖住 Qt 画的东西）、缩放（窗口拉大
 * 时句柄跟不跟）、多屏 DPI（backing scale 变了谁负责重建）、生命周期（widget 没了
 * 之后引擎还拿着那个句柄会怎样）。这些现在就能验，等媒体到位那天再撞就晚了。
 *
 * 所以这里带一个**假渲染器**：它刻意**不走 Qt 的绘制**，而是直接往原生层上贴内容，
 * 复现引擎将来的行为。它只用于验证，不是产品功能。
 *
 * # 交出去的句柄什么时候失效
 *
 * `nativeHandle()` 的返回值在这个控件析构后立刻失效。宿主必须在析构前
 * `attachView(uid, nullptr)` 把它摘掉——`detachRequested` 信号就是为这件事准备的。
 */

#include <QRectF>
#include <QWidget>

class NativeSurface : public QWidget {
  Q_OBJECT

public:
  explicit NativeSurface(QWidget* parent = nullptr);
  ~NativeSurface() override;

  /**
   * 交给 `imrtc_v1_attach_view` 的句柄。macOS 是 `NSView*`，Windows 是 `HWND`。
   * 控件还没有原生窗口时返回 nullptr（没 show 过就可能是这样）。
   */
  void* nativeHandle();

  /** 这个平台有没有实现假渲染器。没有的话只能验句柄本身。 */
  static bool testPatternSupported();

  /** 贴一层**非 Qt 绘制**的测试图案，用来验层级 / 缩放 / DPI。 */
  void showTestPattern(const QString& label);
  void clearTestPattern();

  /** 当前后备缩放（Retina 是 2.0）。多屏拖动时会变。 */
  qreal backingScale() const;

  /** 到目前为止收到过几次尺寸变化——用来核"拉窗口时原生层跟没跟上"。 */
  int resizeCount() const { return resizeCount_; }

  /**
   * 假渲染器那一层当前的 frame 与 contentsScale。没有那一层时返回空 / 0。
   *
   * 这不是测试后门，是**自检口**：原生层不参与 Qt 的布局，位置或缩放错了，
   * 在界面上只表现为"画面偏了一点"或"糊了"，没人说得清是谁的错。
   * 能读回来才能断言。
   */
  QRectF patternGeometry() const;
  qreal patternScale() const;

signals:
  /** 控件即将消失：宿主收到后应立刻 `attachView(uid, nullptr)`。 */
  void detachRequested();

protected:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  /** 平台实现，在 NativeSurfacePattern_*.cpp/.mm 里。 */
  void applyTestPattern();
  void removeTestPattern();
  void syncPatternGeometry();

  QString label_;
  bool patternOn_ = false;
  int resizeCount_ = 0;
};
