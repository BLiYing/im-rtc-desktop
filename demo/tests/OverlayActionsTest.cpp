/**
 * OverlayActionsTest.cpp —— 红按钮与文案的回归测试。
 *
 * 为什么单独给这一件事写测试：它已经错过两次，而且**两次都不会报错**。
 *   - 会议房用了 hangup() → 被本地拒成 2005，按钮点了没反应（Web 端炸过）。
 *   - 群通话用了 leaveRoom() → 界面看着正常退出了，但离开者**永远收不到
 *     onCallEnd**，通话记录落不下来（桌面端炸过，见 CallOverlay.h 头注释）。
 * 后一种只有对着真服务端跑群通话才会暴露，靠看代码看不出来。
 *
 * 规则本身是两条**不同依据**的分叉，这也是它容易写错的原因：
 *   动作按「有没有 call」  —— 会议房 leaveRoom；1v1 与群通话 cancel/reject/hangup
 *   文案按「几个人」      —— 1v1「挂断」；群通话与会议房「离开」
 */

#include <QAbstractButton>
#include <QSignalSpy>
#include <QtTest>

#include "CallHistory.h"
#include "CallOverlay.h"
#include "CallStrings.h"
#include "Theme.h"

namespace {

/** 红按钮靠 objectName 找——那是 Qt 的常规做法，不是给测试开的后门。 */
QAbstractButton* dangerButtonOf(CallOverlay* overlay) {
  return overlay->findChild<QAbstractButton*>(QStringLiteral("dangerButton"));
}

}  // namespace

class OverlayActionsTest : public QObject {
  Q_OBJECT

private slots:
  void meetingRoomLeaves();
  void groupCallHangsUp();
  void oneToOneHangsUp();
  void outgoingCancels();
  void incomingRejects();
  void captionsFollowHeadcount();
  void clickIsWiredToTheRule();
  void recordSummaryMatchesSpec();
};

/** 会议房没有 call：必须 leaveRoom，发 hangup 会被本地拒成 2005。 */
void OverlayActionsTest::meetingRoomLeaves() {
  CallOverlay overlay;
  overlay.setSelfUid(QStringLiteral("alice"));
  overlay.beginRoom(QStringLiteral("r-1"));
  QCOMPARE(overlay.dangerAction(), CallOverlay::DangerAction::LeaveRoom);
}

/**
 * 群通话**有 call**：必须 hangup。
 * 发 room.leave 的话服务端只广播 participant_left，callctl 不知道有人走了，
 * 离开者收不到 onCallEnd——协议 §4 规则 6 要求「离开的人自己收 ended{hangup}」。
 */
void OverlayActionsTest::groupCallHangsUp() {
  CallOverlay overlay;
  overlay.setSelfUid(QStringLiteral("alice"));
  overlay.beginOutgoing({QStringLiteral("bob"), QStringLiteral("carol")},
                        QStringLiteral("audio"), true);
  overlay.markConnected(QStringLiteral("caller"));
  // 这一行就是踩过的坑：群通话**不是** LeaveRoom。
  QCOMPARE(overlay.dangerAction(), CallOverlay::DangerAction::Hangup);
}

void OverlayActionsTest::oneToOneHangsUp() {
  CallOverlay overlay;
  overlay.setSelfUid(QStringLiteral("alice"));
  overlay.beginOutgoing({QStringLiteral("bob")}, QStringLiteral("audio"), false);
  overlay.markConnected(QStringLiteral("caller"));
  QCOMPARE(overlay.dangerAction(), CallOverlay::DangerAction::Hangup);
}

/** 接通**之前**是取消，不是挂断——协议上是两条不同的帧。 */
void OverlayActionsTest::outgoingCancels() {
  CallOverlay overlay;
  overlay.beginOutgoing({QStringLiteral("bob")}, QStringLiteral("audio"), false);
  QCOMPARE(overlay.dangerAction(), CallOverlay::DangerAction::Cancel);
}

void OverlayActionsTest::incomingRejects() {
  CallOverlay overlay;
  overlay.beginIncoming(QStringLiteral("carol"), {QStringLiteral("alice")},
                        QStringLiteral("video"), false);
  QCOMPARE(overlay.dangerAction(), CallOverlay::DangerAction::Reject);
}

/** 文案按人数分叉——与上面按「有没有 call」分叉的动作是两回事。 */
void OverlayActionsTest::captionsFollowHeadcount() {
  CallOverlay solo;
  solo.beginOutgoing({QStringLiteral("bob")}, QStringLiteral("audio"), false);
  solo.markConnected(QStringLiteral("caller"));
  QCOMPARE(solo.dangerCaption(), QStringLiteral("挂断"));

  CallOverlay group;
  group.beginOutgoing({QStringLiteral("bob"), QStringLiteral("carol")},
                      QStringLiteral("audio"), true);
  group.markConnected(QStringLiteral("caller"));
  QCOMPARE(group.dangerCaption(), QStringLiteral("离开"));

  CallOverlay meeting;
  meeting.beginRoom(QStringLiteral("r-1"));
  QCOMPARE(meeting.dangerCaption(), QStringLiteral("离开"));
}

/**
 * 光测 dangerAction() 不够——按钮点下去有没有真走那条规则，也得测一次，
 * 否则「规则对了但按钮没接上」照样是个静默的坑。
 */
void OverlayActionsTest::clickIsWiredToTheRule() {
  CallOverlay overlay;
  overlay.setSelfUid(QStringLiteral("alice"));
  overlay.beginOutgoing({QStringLiteral("bob"), QStringLiteral("carol")},
                        QStringLiteral("audio"), true);
  overlay.markConnected(QStringLiteral("caller"));

  QAbstractButton* danger = dangerButtonOf(&overlay);
  QVERIFY(danger != nullptr);

  QSignalSpy hangup(&overlay, &CallOverlay::hangupRequested);
  QSignalSpy leave(&overlay, &CallOverlay::leaveRoomRequested);
  danger->click();

  QCOMPARE(hangup.count(), 1);
  QCOMPARE(leave.count(), 0);
}

/** 记录摘要要与设计稿 §09「Kit 文案定稿」那张表逐行对上。 */
void OverlayActionsTest::recordSummaryMatchesSpec() {
  CallRecord record;
  record.peer = QStringLiteral("bob");
  record.mediaType = QStringLiteral("audio");

  record.outgoing = true;
  record.connected = true;
  record.reason = QStringLiteral("hangup");
  record.durationSec = 201;
  QCOMPARE(callstrings::recordSummary(record), QStringLiteral("呼出 · 03:21"));

  record.connected = false;
  record.durationSec = 0;
  record.reason = QStringLiteral("busy");
  QCOMPARE(callstrings::recordSummary(record), QStringLiteral("呼出 · 对方忙线"));
  record.reason = QStringLiteral("cancel");
  QCOMPARE(callstrings::recordSummary(record), QStringLiteral("呼出 · 已取消"));

  // 被叫侧没接通一律「未接来电」，不报原因。
  record.outgoing = false;
  record.reason = QStringLiteral("no_answer");
  record.mediaType = QStringLiteral("video");
  QCOMPARE(callstrings::recordSummary(record), QStringLiteral("未接来电 · 视频"));

  // 时长超一小时改 h:mm:ss。
  QCOMPARE(callstrings::duration(3721), QStringLiteral("1:02:01"));
}

QTEST_MAIN(OverlayActionsTest)
#include "OverlayActionsTest.moc"
