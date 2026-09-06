#include "MainWindow.h"

#include <QDateTime>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "CallOverlay.h"
#include "CallStrings.h"
#include "DialPage.h"
#include "EngineBridge.h"
#include "HistoryPage.h"
#include "LoginPage.h"
#include "SettingsPage.h"
#include "Theme.h"

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
  bridge_ = new EngineBridge(this);
  history_ = new CallHistory(this);
  history_->load();

  buildUi();
  wireLogin();
  wireConnection();
  wireCall();
  wireRoom();
  retranslateUi();
  resize(900, 560);
}

void MainWindow::autoLogin(const QString& httpBase, const QString& username,
                           const QString& autoCallee, const QString& mediaType) {
  autoCallee_ = autoCallee;
  autoCallMediaType_ = mediaType;
  login_->prefill(httpBase, username);
  login_->submit();
}

void MainWindow::setAutomation(bool autoAccept, int hangupAfterSec) {
  autoAccept_ = autoAccept;
  hangupAfterSec_ = hangupAfterSec;
}

void MainWindow::buildUi() {
  login_ = new LoginPage(this);

  dial_ = new DialPage(this);
  historyPage_ = new HistoryPage(history_, this);

  auto* split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(dial_);
  split->addWidget(historyPage_);
  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 2);

  home_ = new QWidget(this);
  auto* homeLayout = new QVBoxLayout(home_);
  homeLayout->setContentsMargins(0, 0, 0, 0);
  homeLayout->addWidget(split);

  stack_ = new QStackedWidget(this);
  stack_->addWidget(login_);
  stack_->addWidget(home_);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack_);

  // 浮窗是主窗的子控件，盖在最上层——不是独立窗口。
  // 独立窗口那一套（跨屏、置顶、托盘）属于 UX_FLOWS §07 的「桌面独有九件事」，
  // 那一批还没做，做的时候要连同关闭语义、Dock 角标一起做，不适合零敲碎打。
  overlay_ = new CallOverlay(this);
  overlay_->hide();

  toast_ = new QLabel(this);
  toast_->setWordWrap(true);
  toast_->setAlignment(Qt::AlignCenter);
  toast_->setMargin(10);
  toast_->setFont(theme::type::b2());
  toast_->setStyleSheet(QStringLiteral("background: rgba(18,20,24,235);"
                                       "color: #FFFFFF; border-radius: 10px;"));
  toast_->hide();

  toastTimer_ = new QTimer(this);
  toastTimer_->setSingleShot(true);
  toastTimer_->setInterval(4000);
  connect(toastTimer_, &QTimer::timeout, toast_, &QLabel::hide);
}

void MainWindow::wireLogin() {
  connect(login_, &LoginPage::loginRequested, this, [this] {
    if (login_->username().isEmpty()) {
      login_->showError(tr("请填用户 ID。"));
      return;
    }
    httpBase_ = login_->httpBase();
    uid_ = login_->username();
    nickname_ = login_->nickname();
    wsUrl_ = EngineBridge::wsUrlFromHttpBase(httpBase_);
    login_->showError(QString());
    login_->setBusy(true);
    bridge_->fetchDemoToken(httpBase_, uid_);
  });

  connect(bridge_, &EngineBridge::tokenReady, this, [this](const QString& token) {
    bridge_->connectToServer(wsUrl_, token);
  });
  connect(bridge_, &EngineBridge::tokenFailed, this, [this](const QString& message) {
    login_->setBusy(false);
    login_->showError(tr("取票失败：%1\n服务端起来了吗？（scripts/dev.sh）").arg(message));
  });

  connect(dial_, &DialPage::logoutRequested, this, [this] {
    bridge_->disconnectFromServer();
    hideOverlay();
    stack_->setCurrentWidget(login_);
    login_->setBusy(false);
  });

  connect(dial_, &DialPage::settingsRequested, this, [this] {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("设置"));
    auto* page = new SettingsPage(&dialog);
    page->setSession(uid_, wsUrl_, sessionId_);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(page);
    dialog.resize(520, 520);
    dialog.exec();
  });
}

void MainWindow::wireConnection() {
  connect(bridge_, &EngineBridge::connected, this,
          [this](const QString& sessionId, bool resumed) {
            sessionId_ = sessionId;
            login_->setBusy(false);
            stack_->setCurrentWidget(home_);
            dial_->setIdentity(uid_, nickname_);
            dial_->setConnectionText(
                resumed ? tr("%1 · 已恢复连接").arg(httpBase_) : tr("%1 · 已连接").arg(httpBase_),
                true);
            dial_->setDialingEnabled(true);
            overlay_->setSelfUid(uid_);
            overlay_->onReconnecting(false);

            if (!autoCallee_.isEmpty()) {
              // 只自动拨一次：重连也会走到这里，不清空的话会变成自动重拨。
              const QString callee = autoCallee_;
              autoCallee_.clear();
              if (autoCallMediaType_ == QLatin1String("video")) {
                emit dial_->videoCallRequested(callee);
              } else {
                emit dial_->audioCallRequested(callee);
              }
            }
          });

  connect(bridge_, &EngineBridge::disconnected, this, [this] {
    dial_->setConnectionText(tr("正在重连…"), false);
    overlay_->onReconnecting(true);
  });

  connect(bridge_, &EngineBridge::kickedOut, this, [this] {
    // 4401 连续失败或别处登录：回登录页换票，别在原地空转。
    bridge_->disconnectFromServer();
    hideOverlay();
    stack_->setCurrentWidget(login_);
    login_->setBusy(false);
    login_->showError(tr("已在别处登录，或票据已失效。请重新登录。"));
  });

  connect(bridge_, &EngineBridge::engineError, this,
          [this](qint32 code, const QString& name, const QString& forType) {
            toast(tr("出错了：%1（%2），来自 %3").arg(code).arg(name, forType));
          });
  connect(bridge_, &EngineBridge::restFailed, this,
          [this](const QString& message) { toast(message); });
}

void MainWindow::wireCall() {
  const auto startCall = [this](const QStringList& ids, const QString& mediaType, bool isGroup) {
    if (ids.isEmpty()) {
      toast(tr("先填一个对方 ID。"));
      return;
    }
    const qint32 code = bridge_->startCall(ids, mediaType, isGroup);
    if (code != IMRTC_V1_OK) {
      toast(tr("拨不出去：%1（%2）").arg(code).arg(QString::fromLatin1(imrtc_v1_error_name(code))));
      return;
    }
    pending_ = CallRecord{};
    pending_.peer = isGroup ? QString() : ids.first();
    pending_.members = ids;
    pending_.isGroup = isGroup;
    pending_.mediaType = mediaType;
    pending_.outgoing = true;
    hasPending_ = true;

    overlay_->beginOutgoing(ids, mediaType, isGroup);
    showOverlay();
    dial_->setDialingEnabled(false);
  };

  connect(dial_, &DialPage::audioCallRequested, this, [startCall](const QString& id) {
    startCall(id.isEmpty() ? QStringList() : QStringList{id}, QStringLiteral("audio"), false);
  });
  connect(dial_, &DialPage::videoCallRequested, this, [startCall](const QString& id) {
    startCall(id.isEmpty() ? QStringList() : QStringList{id}, QStringLiteral("video"), false);
  });
  connect(dial_, &DialPage::groupCallRequested, this,
          [startCall](const QStringList& ids, const QString& mediaType) {
            startCall(ids, mediaType, true);
          });

  connect(bridge_, &EngineBridge::callReceived, this,
          [this](const QString& callId, const QString& caller, const QStringList& callees,
                 const QString& mediaType, bool isGroup) {
            pending_ = CallRecord{};
            pending_.callId = callId;
            pending_.peer = caller;
            pending_.members = callees;
            pending_.isGroup = isGroup;
            pending_.mediaType = mediaType;
            pending_.outgoing = false;
            hasPending_ = true;

            overlay_->beginIncoming(caller, callees, mediaType, isGroup);
            showOverlay();
            dial_->setDialingEnabled(false);
            if (autoAccept_) bridge_->accept();
          });

  connect(bridge_, &EngineBridge::callBegan, this,
          [this](const QString& callId, const QString& roomId, const QString& role) {
            Q_UNUSED(roomId);
            pending_.callId = callId;
            pending_.connected = true;
            overlay_->markConnected(role);
            if (hangupAfterSec_ > 0) {
              QTimer::singleShot(hangupAfterSec_ * 1000, this, [this] {
                // 群 / 会议房是 leaveRoom，1v1 才是 hangup——与红按钮同一条分叉。
                if (pending_.isGroup) {
                  bridge_->leaveRoom();
                } else {
                  bridge_->hangup();
                }
              });
            }
          });

  connect(bridge_, &EngineBridge::callEnded, this,
          [this](const QString& callId, const QString& reason, qint64 durationSec,
                 const QString& endedBy) {
            Q_UNUSED(endedBy);
            commitRecord(callId, reason, durationSec);
            overlay_->markEnded(reason, durationSec);
            dial_->setDialingEnabled(true);
          });

  // 便利回调**只在 1v1 抛**（不变量 I7），群通话看成员事件。
  // 它们**不是终局**——终局永远是 onCallEnd。所以这里既不写记录，也不弹模态：
  // 浮窗的结束态已经由 onCallEnd 显示了「对方忙线中 / 无人接听 / 对方已拒绝」，
  // 再弹一个模态就是同一件事说两遍，还挡住浮窗。宿主要单独用它们当然可以，
  // 那是宿主的选择——这里只把它们记进日志，证明确实收到了。
  connect(bridge_, &EngineBridge::callBusy, this,
          [](const QString& uid) { qInfo("onCallBusy %s", qUtf8Printable(uid)); });
  connect(bridge_, &EngineBridge::callNoAnswer, this,
          [](const QString& uid) { qInfo("onCallNoAnswer %s", qUtf8Printable(uid)); });
  connect(bridge_, &EngineBridge::callRejected, this,
          [](const QString& uid) { qInfo("onCallRejected %s", qUtf8Printable(uid)); });
  connect(bridge_, &EngineBridge::callCancelled, this,
          [](const QString& by) { qInfo("onCallCancelled %s", qUtf8Printable(by)); });
  connect(bridge_, &EngineBridge::callMissed, this,
          [this](const QString& callId, const QString& caller, const QString& reason) {
            // 通话中被第三个人呼叫，服务端已经替我们回了忙线——**不要弹来电页**。
            CallRecord missed;
            missed.callId = callId;
            missed.peer = caller;
            missed.mediaType = QStringLiteral("audio");
            missed.outgoing = false;
            missed.connected = false;
            missed.reason = reason;
            missed.endedAt = QDateTime::currentDateTime();
            history_->add(missed);
            toast(tr("通话中，已自动回复 %1 忙线").arg(caller));
          });
  connect(bridge_, &EngineBridge::handledOnOtherDevice, this,
          [this](const QString& callId, const QString& action) {
            Q_UNUSED(callId);
            toast(action == QLatin1String("accepted") ? tr("已在其他设备接听")
                                                      : tr("已在其他设备拒绝"));
            hideOverlay();
          });

  connect(bridge_, &EngineBridge::userEntered, overlay_, &CallOverlay::onMemberEntered);
  connect(bridge_, &EngineBridge::userLeft, overlay_, &CallOverlay::onMemberLeft);
  connect(bridge_, &EngineBridge::userAccepted, overlay_, &CallOverlay::onMemberAccepted);
  connect(bridge_, &EngineBridge::userRejected, overlay_, &CallOverlay::onMemberRejected);
  connect(bridge_, &EngineBridge::userNoResponse, overlay_, &CallOverlay::onMemberNoResponse);
  connect(bridge_, &EngineBridge::userAudioAvailable, overlay_, &CallOverlay::onMemberAudio);
  connect(bridge_, &EngineBridge::activeSpeakers, overlay_, &CallOverlay::onSpeakers);
  connect(bridge_, &EngineBridge::networkQuality, overlay_, &CallOverlay::onQuality);

  connect(overlay_, &CallOverlay::acceptRequested, this, [this] { bridge_->accept(); });
  connect(overlay_, &CallOverlay::rejectRequested, this, [this] { bridge_->reject(); });
  connect(overlay_, &CallOverlay::cancelRequested, this, [this] { bridge_->cancel(); });
  connect(overlay_, &CallOverlay::hangupRequested, this, [this] { bridge_->hangup(); });
  connect(overlay_, &CallOverlay::leaveRoomRequested, this, [this] { bridge_->leaveRoom(); });
  connect(overlay_, &CallOverlay::notice, this, &MainWindow::toast);
  connect(overlay_, &CallOverlay::closed, this, &MainWindow::hideOverlay);
}

void MainWindow::wireRoom() {
  connect(dial_, &DialPage::createRoomRequested, this,
          [this] { bridge_->createMeetingRoom(httpBase_); });
  connect(bridge_, &EngineBridge::meetingRoomCreated, this, [this](const QString& roomId) {
    dial_->setRoomId(roomId);
    toast(tr("会议房已创建：%1").arg(roomId));
  });

  connect(dial_, &DialPage::joinRoomRequested, this, [this](const QString& roomId) {
    if (roomId.isEmpty()) {
      toast(tr("先填房间号，或点「新建会议房」。"));
      return;
    }
    // 会议房要先换一枚进房票——通话那条路的票由服务端在 call.connected 里下发。
    bridge_->fetchRoomToken(httpBase_, roomId);
  });
  connect(bridge_, &EngineBridge::roomTokenReady, this,
          [this](const QString& roomId, const QString& roomToken) {
            const qint32 code = bridge_->joinRoom(roomId, roomToken);
            if (code != IMRTC_V1_OK) {
              toast(tr("进房失败：%1（%2）")
                        .arg(code)
                        .arg(QString::fromLatin1(imrtc_v1_error_name(code))));
            }
          });

  connect(bridge_, &EngineBridge::roomJoined, this, [this](const QString& roomId) {
    // 通话那条路的进房不该再弹一次浮窗——那时候浮窗已经开着了。
    if (overlay_->isVisible()) return;
    overlay_->beginRoom(roomId);
    showOverlay();
    dial_->setDialingEnabled(false);
  });
  connect(bridge_, &EngineBridge::roomLeft, this, [this](const QString& roomId) {
    Q_UNUSED(roomId);
    hideOverlay();
    dial_->setDialingEnabled(true);
  });
  connect(bridge_, &EngineBridge::roomClosed, this,
          [this](const QString& roomId, const QString& reason) {
            Q_UNUSED(roomId);
            toast(callstrings::callerEndText(reason));
            hideOverlay();
            dial_->setDialingEnabled(true);
          });
}

void MainWindow::commitRecord(const QString& callId, const QString& reason, qint64 durationSec) {
  if (!hasPending_) return;
  pending_.callId = callId.isEmpty() ? pending_.callId : callId;
  pending_.reason = reason;
  // **时长用回调给的值**，不是自己拿时间戳减（不变量 I8）。
  pending_.durationSec = durationSec;
  pending_.endedAt = QDateTime::currentDateTime();
  history_->add(pending_);
  hasPending_ = false;
}

void MainWindow::showOverlay() {
  centerOverlay();
  overlay_->show();
  overlay_->raise();
}

void MainWindow::hideOverlay() {
  overlay_->hide();
  dial_->setDialingEnabled(bridge_->isEngineAlive());
}

void MainWindow::centerOverlay() {
  overlay_->move((width() - overlay_->width()) / 2, (height() - overlay_->height()) / 2);
}

void MainWindow::toast(const QString& message) {
  // 后来的提示直接替换前一条：连着弹三条模态是最容易惹人烦的做法之一。
  toast_->setText(message);
  toast_->adjustSize();
  const int maxWidth = qMin(420, width() - 40);
  toast_->setFixedWidth(qMin(toast_->width(), maxWidth));
  toast_->adjustSize();
  toast_->move((width() - toast_->width()) / 2, 16);
  toast_->show();
  toast_->raise();
  toastTimer_->start();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  centerOverlay();
  if (toast_->isVisible()) toast_->move((width() - toast_->width()) / 2, 16);
}

void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslateUi();
  QWidget::changeEvent(event);
}

void MainWindow::retranslateUi() {
  setWindowTitle(tr("im-rtc Demo —— 桌面端参考实现（经 C ABI）"));
}
