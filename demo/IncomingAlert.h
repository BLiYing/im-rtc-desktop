#pragma once

/**
 * IncomingAlert.h —— 窗口在后台时的来电提醒（UX_FLOWS §07 第 3 条）。
 *
 * 窗内横幅（`IncomingBanner`）只在窗口在前台时看得见。窗口最小化 / 不是活动窗口 / 被藏起来时，
 * 来电还要**跳 Dock（macOS）/ 闪任务栏（Windows）+ 系统通知**；**用户点了通知才前置**，
 * 不抢焦点——与横幅同一条规矩。
 *
 * # 三条规矩
 *
 * 1. **前台不重复提醒**：窗口在前台时只有横幅，不弹系统通知、不跳 Dock（`needsSystemAlert`）。
 * 2. **点通知 = 前置并展开来电浮层**：只在还在响铃时算数。通话已经结束后才点到那条旧通知，
 *    不发 `openRequested`（系统自己会把应用激活，但不该再展开一个不存在的来电）。
 * 3. **接通 / 结束 / 取消都要 `clear()`**：停掉跳 Dock，撤掉通知。撤不撤得掉看平台，见 `SystemAlertSink.h`。
 *
 * 系统那一侧（托盘通知、Dock / 任务栏注意请求）经 `Sink` 注入：
 * Demo 的界面测试跑在真窗口上（不能 offscreen），不注入的话跑一遍 test.sh 就会真弹一条通知。
 */

#include <QObject>
#include <QString>

#include <memory>

class QWidget;

namespace incomingalert {

/** 判定要不要系统提醒时看的三件事。拆成值，判据才能脱离真窗口测。 */
struct WindowPresence {
  bool visible = false;
  bool minimized = false;
  bool active = false;
};

/** 窗口看不见、最小化了、或不是活动窗口（另一个应用在前台 / 被挡住）→ 要系统提醒。 */
bool needsSystemAlert(const WindowPresence& presence);

/** 从真窗口读出 `WindowPresence`。 */
WindowPresence presenceOf(const QWidget* window);

}  // namespace incomingalert

class IncomingAlert : public QObject {
  Q_OBJECT

public:
  /** 系统那一侧。默认实现是 `SystemAlertSink`；测试注入记录用的假件。 */
  class Sink {
  public:
    virtual ~Sink();
    /** 发一条系统通知。点它要回调 `IncomingAlert::notificationClicked()`。 */
    virtual void post(const QString& title, const QString& body) = 0;
    /** 撤掉通知（平台做不到就尽量收起）。 */
    virtual void withdraw() = 0;
    /** 跳 Dock / 闪任务栏，直到 `cancelAttention()` 或窗口被激活。 */
    virtual void requestAttention(QWidget* window) = 0;
    virtual void cancelAttention() = 0;
  };

  /** `window` 是主窗，也是本对象的 parent。`sink` 为空则用 `SystemAlertSink`。 */
  explicit IncomingAlert(QWidget* window, std::unique_ptr<Sink> sink = nullptr);
  ~IncomingAlert() override;

  /**
   * 来电时调。窗口在前台什么都不做、返回 false（横幅已经够了）；
   * 否则跳 Dock / 闪任务栏 + 发通知（标题是来电人，正文是邀请语），返回 true。
   */
  bool ring(const incomingalert::WindowPresence& presence, const QString& caller,
            const QString& mediaType, bool isGroup);

  /** 接通 / 结束 / 取消 / 登出时调。没在提醒就什么都不做，可以重复调。 */
  void clear();

  bool isAlerting() const;

  /** 系统通知（或托盘图标）被点。由 sink 调；不在响铃时忽略。 */
  void notificationClicked();

signals:
  /** 用户点了提醒：主窗前置并展开来电浮层。 */
  void openRequested();

private:
  QWidget* window_ = nullptr;
  std::unique_ptr<Sink> sink_;
  bool alerting_ = false;
};
