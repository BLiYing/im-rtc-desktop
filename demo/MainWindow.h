#pragma once

/**
 * MainWindow.h —— 壳：登录屏 ↔ 主界面（左拨号 / 右记录），通话浮窗盖在上面。
 *
 * 两栏版式照搬 Web（草图 §06-Q）：桌面与 Web 共用一套视觉，只有窗口与系统集成
 * 那一层是各自的（UI_SPEC §01 结论 B）。
 *
 * **通话记录在这里拼**：`onCallEnd` 是所有结束分支的唯一出口（不变量 I1），
 * 所以一通电话的记录只在那一个回调里落一条，别处都不写。
 */

#include <QWidget>

#include "CallHistory.h"

class CallOverlay;
class DialPage;
class EngineBridge;
class HistoryPage;
class LoginPage;
class SettingsPage;
class QLabel;
class QStackedWidget;
class QTimer;

class MainWindow : public QWidget {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

  /**
   * 联调用：预填服务器与用户名并自动登录，可选地在连上之后立刻拨一个人。
   * 每天要登几十次，手填三个字段是纯浪费。命令行见 main.cpp。
   */
  /**
   * 联调用：预填服务器与用户名并自动登录，可选地在连上之后立刻做一件事。
   * `autoCallees` 多于一个就是群通话；`autoRoom` 非空则走会议房那条路
   * （`"new"` 表示先建一个）。两者互斥，同时给时以通话优先。
   */
  void autoLogin(const QString& httpBase, const QString& username,
                 const QStringList& autoCallees, const QString& mediaType,
                 const QString& autoRoom);

  /**
   * 联调开关，**只由命令行打开**：
   *   autoAccept    —— 收到来电就接。省掉「找第二个人点接听」这件事。
   *   hangupAfterSec —— 接通 N 秒后自动挂断（0 = 不自动挂）。
   * 生产宿主当然不该这么干，这两个开关的存在是为了让「接通 → 计时 → 挂断 →
   * 落记录」这条完整回路能被一条命令跑完。
   */
  void setAutomation(bool autoAccept, int hangupAfterSec, const QString& autoInvite);

protected:
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  void buildUi();
  void wireLogin();
  void wireConnection();
  void wireCall();
  void wireRoom();
  void retranslateUi();

  void showOverlay();
  void hideOverlay();
  void centerOverlay();
  /**
   * 非模态提示条。**刻意不用 QMessageBox**：静态的 QMessageBox 会开一个
   * 嵌套事件循环并等人点确定，在没人看着的场合（自动化联调、连续来了两条提示）
   * 会把后面的动作全挡住——实测中它挡掉过一次自动挂断。
   * 提示本来就是「说一声」，不该要求用户响应。
   */
  void toast(const QString& message);
  /** 把手上这通电话的信息落成一条记录。只在 onCallEnd 里调。 */
  void commitRecord(const QString& callId, const QString& reason, qint64 durationSec);

  EngineBridge* bridge_ = nullptr;
  CallHistory* history_ = nullptr;

  QStackedWidget* stack_ = nullptr;
  LoginPage* login_ = nullptr;
  QWidget* home_ = nullptr;
  DialPage* dial_ = nullptr;
  HistoryPage* historyPage_ = nullptr;
  CallOverlay* overlay_ = nullptr;
  QLabel* toast_ = nullptr;
  QTimer* toastTimer_ = nullptr;

  QString httpBase_;
  QString wsUrl_;
  QString uid_;
  QString nickname_;
  QString sessionId_;
  /** --call 指定的对象：连上之后自动拨一次，只拨这一次。 */
  QStringList autoCallees_;
  QString autoCallMediaType_;
  /** --room：`new` 表示先建一个会议房再进。同样只做一次。 */
  QString autoRoom_;
  /** `--room new` 建完之后要不要立刻进房（手点「新建会议房」时不进）。 */
  bool autoRoomJoinPending_ = false;

  /** 手上这通电话。onCallEnd 之后清空。 */
  CallRecord pending_;
  bool hasPending_ = false;

  bool autoAccept_ = false;
  int hangupAfterSec_ = 0;
  QString autoInvite_;

  /** 接通 / 进房之后 N 秒自动退出。1v1 是 hangup，群与会议房是 leaveRoom。 */
  void armAutoLeave(bool useLeaveRoom);
};
