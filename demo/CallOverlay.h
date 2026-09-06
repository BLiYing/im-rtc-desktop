#pragma once

/**
 * CallOverlay.h —— 通话浮窗（草图 §06-R / §06-S，桌面版式）。
 *
 * 居中深色浮窗 520×360，四态：拨出中 / 来电 / 通话中 / 已结束。
 * 群通话时中间换成九宫格（3×3）。
 *
 * # 三条从设计稿与协议来的硬规矩
 *
 * 1. **通话页固定深色，不跟随系统主题**（草图 §03）。Demo 的其余四屏跟系统，
 *    只有这一层是写死的。
 * 2. **红按钮：动作只按「有没有 call」分叉，文案才按人数分叉。**
 *    - **会议房**（`join_room` 进来的，没有 call）→ `leaveRoom()`
 *    - **1v1 与群通话**（有 call）→ 接通前 `cancel()` / `reject()`，接通后 **`hangup()`**
 *    - 文案：1v1 写「挂断」，群通话与会议房都写「离开」
 *
 *    也就是说**群通话上写着「离开」，调的却是 `hangup()`**——这一格最容易写错，
 *    我们就写错过：设计稿 §05 原话是「群/会议 → leaveRoom()」，照着写之后在真服务端上
 *    发现群通话的离开者**永远收不到 `onCallEnd`**（服务端对 `room.leave` 只广播
 *    `room.participant_left`），他的通话记录落不下来，也违反不变量 I1。
 *    协议 §4 规则 6 说得很清楚：**离开的人自己收 `ended{hangup, ended_by:自己}`**。
 *    设计稿已于 2026-09-06 更正。
 * 3. **计时器只是显示**。通话记录里的时长必须用 `onCallEnd` 给的 `durationSec`
 *    （不变量 I8），不许拿这里的秒表去写记录。
 *
 * # 这个构建里静音与摄像头是禁用的
 *
 * 不是偷懒。媒体适配器没接上时，引擎的 `openMic()` / `closeCamera()` 是
 * **静默空操作**——按钮看着像生效，其实既没开麦也没给对端发 `room.mute`。
 * 与其留一个假开关，不如按 UI_SPEC §06 的「禁用态」画出来，点了给提示。
 * WebRTCAdapter 落地后把 `setBlocked({})` 打开即可。
 */

#include <QHash>
#include <QStringList>
#include <QWidget>

#include "EngineBridge.h"

class AvatarWidget;
class ControlButton;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QTimer;
class VideoTile;

class CallOverlay : public QWidget {
  Q_OBJECT

public:
  enum class Phase { Outgoing, Incoming, Connected, Ended };

  /** 红按钮在当前状态下到底该调哪个方法。 */
  enum class DangerAction { None, Cancel, Reject, Hangup, LeaveRoom };

  /**
   * **红按钮那条规则的唯一真相源**，公开是为了能直接测。
   *
   * 动作只看「有没有 call」，**不看人数**：
   *   - 会议房（`join_room` 进来的，没有 call）→ `LeaveRoom`
   *   - 1v1 与群通话（有 call）→ 接通前 `Cancel` / `Reject`，接通后 `Hangup`
   */
  DangerAction dangerAction() const;

  /**
   * 红按钮的文案。**依据与 `dangerAction()` 不同**：文案只看人数——
   * 1v1 写「挂断」，群通话与会议房都写「离开」。
   * 于是群通话上写着「离开」，调的却是 `Hangup`。这一格最容易写错。
   */
  QString dangerCaption() const;

  explicit CallOverlay(QWidget* parent = nullptr);

  /** 我拨出去的。`members` 是被叫列表（1v1 就一个）。 */
  void beginOutgoing(const QStringList& members, const QString& mediaType, bool isGroup);
  /** 收到来电。 */
  void beginIncoming(const QString& caller, const QStringList& callees, const QString& mediaType,
                     bool isGroup);
  /** 直接进会议房（没有 call，红按钮语义是「离开」）。 */
  void beginRoom(const QString& roomId);

  void markConnected(const QString& role);
  /** 显示结束语，几秒后自动关。 */
  void markEnded(const QString& reason, qint64 durationSec);

  void setSelfUid(const QString& uid) { selfUid_ = uid; }

  /* ---- 成员事件：全部由回调驱动 ---- */
  void onMemberEntered(const QString& uid);
  void onMemberLeft(const QString& uid);
  void onMemberAccepted(const QString& uid);
  void onMemberRejected(const QString& uid);
  void onMemberNoResponse(const QString& uid);
  void onMemberAudio(const QString& uid, bool available);
  void onSpeakers(const QList<SpeakerInfo>& speakers);
  void onQuality(const QList<QualityInfo>& entries);
  void onReconnecting(bool reconnecting);

signals:
  void acceptRequested();
  void rejectRequested();
  void cancelRequested();
  void hangupRequested();
  void leaveRoomRequested();
  void inviteMoreRequested();
  /** 点了禁用按钮：上层弹提示，不能静默。 */
  void notice(const QString& message);
  void closed();

protected:
  void paintEvent(QPaintEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  void retranslateUi();
  void applyPhase();
  void rebuildTiles();
  VideoTile* tileFor(const QString& uid);
  void updateTitle();
  bool isRoomMode() const { return !roomId_.isEmpty(); }

  Phase phase_ = Phase::Outgoing;
  bool isGroup_ = false;
  bool isVideo_ = false;
  bool outgoing_ = true;
  QString peer_;        ///< 1v1 的对方
  QString caller_;
  QString roomId_;
  QString selfUid_;
  QStringList members_;
  QString endReason_;
  qint64 endDuration_ = 0;
  int elapsedSec_ = 0;

  QLabel* title_ = nullptr;
  QLabel* timer_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* peerName_ = nullptr;
  QLabel* endText_ = nullptr;
  QLabel* modeNotice_ = nullptr;
  AvatarWidget* avatar_ = nullptr;
  QWidget* soloPane_ = nullptr;
  QWidget* gridPane_ = nullptr;
  QGridLayout* grid_ = nullptr;
  QWidget* endPane_ = nullptr;
  QHBoxLayout* controls_ = nullptr;

  ControlButton* mic_ = nullptr;
  ControlButton* camera_ = nullptr;
  ControlButton* screen_ = nullptr;
  ControlButton* danger_ = nullptr;
  ControlButton* answer_ = nullptr;

  QHash<QString, VideoTile*> tiles_;
  QTimer* clock_ = nullptr;
  QTimer* autoClose_ = nullptr;
};
