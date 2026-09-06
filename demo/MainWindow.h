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
class QStackedWidget;

class MainWindow : public QWidget {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

  /**
   * 联调用：预填服务器与用户名并自动登录，可选地在连上之后立刻拨一个人。
   * 每天要登几十次，手填三个字段是纯浪费。命令行见 main.cpp。
   */
  void autoLogin(const QString& httpBase, const QString& username, const QString& autoCallee,
                 const QString& mediaType);

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

  QString httpBase_;
  QString wsUrl_;
  QString uid_;
  QString nickname_;
  QString sessionId_;
  /** --call 指定的对象：连上之后自动拨一次，只拨这一次。 */
  QString autoCallee_;
  QString autoCallMediaType_;

  /** 手上这通电话。onCallEnd 之后清空。 */
  CallRecord pending_;
  bool hasPending_ = false;
};
