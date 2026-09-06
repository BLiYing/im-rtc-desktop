#pragma once

/**
 * VideoTile.h —— 九宫格里的一格（UI_SPEC §06「格子（VideoTile）」）。
 *
 * 现在**画不出真实画面**：媒体那一刀还没落地（见 current_task 已知坑第一条）。
 * 但格子的全部语义都在这里并且是真的——谁在说话（绿色内描边）、谁静音了
 * （右上角标）、谁还在振铃（「呼叫中…」）、谁网络差。这些都由回调驱动，不是摆设。
 *
 * **渲染路径 A 的口子也已经在这里了**（设计 §8.3）：`setVideoAvailable(true)` 会
 * 在格子里建一个 `NativeSurface`，并把它的原生句柄（macOS `NSView*` /
 * Windows `HWND`）通过 `attachRequested` 递上去，宿主转手交给
 * `imrtc_v1_attach_view`。等 `WebRTCAdapter` 接上，画面直接落在这一层，
 * 这个类一行都不用改。
 *
 * 绿色（#3DDC84）在通话页只有两个含义：接听、正在说话。
 * **不许拿它当「已连接」的状态点**——九宫格里全靠它辨认谁在说话。
 */

#include <QString>
#include <QWidget>

class NativeSurface;

class VideoTile : public QWidget {
  Q_OBJECT

public:
  enum class State {
    Ringing,   ///< 还在振铃：「呼叫中…」
    Present,   ///< 已在通话里
    Left       ///< 已离开（保留一格灰着，比让格子跳掉好读）
  };

  explicit VideoTile(QWidget* parent = nullptr);

  void setIdentity(const QString& uid, const QString& displayName);
  void setState(State state);
  void setSpeaking(bool speaking);
  void setMuted(bool muted);
  /** 0 = unknown，1~2 视为弱网（§05 net-bars 三档）。 */
  void setQualityLevel(int level);
  void setSelf(bool self);

  /**
   * 有没有画面。true 时建原生子窗口并把句柄递上去；false 时先请宿主摘掉
   * （`detachRequested`）**再**销毁——顺序反了就是野指针。
   */
  void setVideoAvailable(bool available);
  bool videoAvailable() const { return surface_ != nullptr; }

  /** 联调用：在原生层上贴一张测试图案，验层级 / 缩放 / DPI。 */
  void setTestPatternEnabled(bool enabled);

  QString uid() const { return uid_; }

signals:
  /** 句柄只在下一次 `detachRequested` 之前有效。 */
  void attachRequested(const QString& uid, void* nativeHandle);
  void detachRequested(const QString& uid);

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  QString uid_;
  QString displayName_;
  State state_ = State::Present;
  bool speaking_ = false;
  bool muted_ = false;
  bool self_ = false;
  int quality_ = 0;
  bool testPattern_ = false;
  NativeSurface* surface_ = nullptr;
  /**
   * 画面上面那层「格子外壳」：名字标签、静音角标、发言描边、弱网。
   *
   * **为什么要单独一层**：原生子窗口（画面）在 macOS 上会盖住同一个 widget 里
   * 所有 Qt 绘制的内容，与 Qt 的 z 序无关——格子直接 paintEvent 画的标签会被吃掉。
   * 所以外壳必须是**在画面之后创建的另一个原生子窗口**，靠 NSView 的兄弟顺序压在上面。
   */
  QWidget* chrome_ = nullptr;

  /** 外壳的绘制。没有画面时由格子自己画，有画面时由 chrome_ 画——同一份代码。 */
  void paintChrome(QPainter* painter, const QRect& box) const;

  /** 重绘。**必须连外壳一起**：有画面时外壳是另一个 widget，格子的 update() 到不了它。 */
  void refresh();

  friend class TileChrome;
};
