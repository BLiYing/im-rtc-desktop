#pragma once

/**
 * IncomingBanner.h —— 窗内来电横幅（UI_SPEC §06「来电横幅」，UX_FLOWS §07 v3.7）。
 *
 * 来电先出这一条，**不直接盖住整个窗口**：用户可能正在拨号页里打字。
 * 贴主窗顶部居中（离顶与两侧 8、最宽 420、高 62、圆角 16），规格同 Web 的 `IncomingCall`。
 *
 * # 三条规矩
 *
 * 1. **不抢焦点**：横幅和它的按钮都是 `Qt::NoFocus`，出现时不 `activateWindow`、不 `setFocus`——
 *    输入框里的光标原地不动。窗口在后台时的系统通知 / 跳 Dock / 闪任务栏在 `IncomingAlert`（§07 第 3 条）。
 * 2. **点本体展开，点按钮不展开**：本体点击发 `expandRequested`，由 MainWindow 换成来电浮层；
 *    按钮自己 accept 鼠标事件，不会冒泡到本体上顺带展开一下。
 *    头像与两行字是**画**出来的而不是子控件，所以点它们就是点本体。
 * 3. **摄像头按钮今天是禁用态**：桌面媒体面没接，与浮层上那颗一致，点了出提示。
 */

#include <QString>
#include <QWidget>

class ControlButton;

class IncomingBanner : public QWidget {
  Q_OBJECT

public:
  /** `parent` 是要贴上去的窗口：横幅跟着它的 resize 自己重新摆位。 */
  explicit IncomingBanner(QWidget* parent = nullptr);

  /** 摆好内容并显示在最上层。不碰焦点。 */
  void showCall(const QString& caller, const QString& mediaType, bool isGroup);

signals:
  void expandRequested();
  void acceptRequested();
  void rejectRequested();
  /** 点了禁用按钮：上层弹提示，不能静默。 */
  void notice(const QString& message);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  void place();
  void retranslateUi();
  /** 文字能排到哪：第一颗可见按钮的左边。 */
  int textRight() const;

  QString caller_;
  bool isVideo_ = false;
  bool isGroup_ = false;
  ControlButton* camera_ = nullptr;
  ControlButton* reject_ = nullptr;
  ControlButton* accept_ = nullptr;
};
