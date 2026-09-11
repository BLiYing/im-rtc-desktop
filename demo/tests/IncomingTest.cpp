/**
 * IncomingTest.cpp —— 窗内来电横幅与来电浮层（UX_FLOWS §07 v3.7）。
 *
 * 钉的是三件**出错时不报任何错**的事：
 *   - 点横幅**本体**展开，点横幅上的按钮**不**展开。按钮哪天漏 accept 鼠标事件，
 *     事件就冒泡到本体上：点接听的同时横幅也换成了浮层，界面看着只是「闪了一下」；
 *   - 横幅不抢焦点：在拨号页打字时来电，光标不能被抢走；
 *   - 视频来电的浮层上有摄像头这一颗，今天是禁用态（媒体面未接），点了必须出提示。
 */

#include <QLineEdit>
#include <QSignalSpy>
#include <QtTest>

#include "CallOverlay.h"
#include "CallStrings.h"
#include "ControlButton.h"
#include "IncomingBanner.h"

namespace {

/** 按钮靠 objectName 找——那是 Qt 的常规做法，不是给测试开的后门。 */
ControlButton* buttonOf(QWidget* root, const char* name) {
  return root->findChild<ControlButton*>(QString::fromLatin1(name));
}

}  // namespace

class IncomingTest : public QObject {
  Q_OBJECT

private slots:
  void bodyClickExpands();
  void buttonClicksDoNotExpand();
  void cameraOnlyForVideoCalls();
  void doesNotTakeFocus();
  void placedTopCenterAndFollowsResize();
  void inviteTextMatchesWeb();
  void overlayIncomingVideoHasBlockedCamera();
};

void IncomingTest::bodyClickExpands() {
  QWidget host;
  host.resize(900, 560);
  auto* banner = new IncomingBanner(&host);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  banner->showCall(QStringLiteral("carol"), QStringLiteral("video"), false);

  QSignalSpy expand(banner, &IncomingBanner::expandRequested);
  QSignalSpy accept(banner, &IncomingBanner::acceptRequested);
  // 头像是画出来的，点它就是点本体。
  QTest::mouseClick(banner, Qt::LeftButton, {}, QPoint(30, banner->height() / 2));
  QCOMPARE(expand.count(), 1);
  QCOMPARE(accept.count(), 0);
}

void IncomingTest::buttonClicksDoNotExpand() {
  QWidget host;
  host.resize(900, 560);
  auto* banner = new IncomingBanner(&host);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  banner->showCall(QStringLiteral("carol"), QStringLiteral("video"), false);

  QSignalSpy expand(banner, &IncomingBanner::expandRequested);
  QSignalSpy accept(banner, &IncomingBanner::acceptRequested);
  QSignalSpy reject(banner, &IncomingBanner::rejectRequested);
  QSignalSpy notice(banner, &IncomingBanner::notice);

  QTest::mouseClick(buttonOf(banner, "bannerRejectButton"), Qt::LeftButton);
  QTest::mouseClick(buttonOf(banner, "bannerAcceptButton"), Qt::LeftButton);
  // 禁用态的摄像头：吃掉点击、出提示，同样不能冒泡。
  QTest::mouseClick(buttonOf(banner, "bannerCameraButton"), Qt::LeftButton);

  QCOMPARE(reject.count(), 1);
  QCOMPARE(accept.count(), 1);
  QCOMPARE(notice.count(), 1);
  QCOMPARE(expand.count(), 0);
}

void IncomingTest::cameraOnlyForVideoCalls() {
  QWidget host;
  auto* banner = new IncomingBanner(&host);
  ControlButton* camera = buttonOf(banner, "bannerCameraButton");
  QVERIFY(camera != nullptr);

  banner->showCall(QStringLiteral("carol"), QStringLiteral("audio"), false);
  QVERIFY(!camera->isVisibleTo(banner));
  banner->showCall(QStringLiteral("carol"), QStringLiteral("video"), false);
  QVERIFY(camera->isVisibleTo(banner));
  banner->showCall(QStringLiteral("carol"), QStringLiteral("video"), true);
  QVERIFY(camera->isVisibleTo(banner));
}

void IncomingTest::doesNotTakeFocus() {
  QWidget host;
  host.resize(900, 560);
  auto* edit = new QLineEdit(&host);
  auto* banner = new IncomingBanner(&host);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  edit->setFocus();

  banner->showCall(QStringLiteral("carol"), QStringLiteral("video"), false);
  QCOMPARE(host.focusWidget(), edit);
  QCOMPARE(banner->focusPolicy(), Qt::NoFocus);
  const QList<ControlButton*> buttons = banner->findChildren<ControlButton*>();
  QCOMPARE(buttons.size(), 3);
  for (const ControlButton* button : buttons) QCOMPARE(button->focusPolicy(), Qt::NoFocus);
}

void IncomingTest::placedTopCenterAndFollowsResize() {
  QWidget host;
  host.resize(900, 560);
  auto* banner = new IncomingBanner(&host);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  banner->showCall(QStringLiteral("carol"), QStringLiteral("audio"), false);
  QCOMPARE(banner->geometry(), QRect((900 - 420) / 2, 8, 420, 62));

  // 窗口窄到放不下 420：两侧各留 8。
  host.resize(400, 560);
  QTRY_COMPARE(banner->geometry(), QRect(8, 8, 384, 62));
}

/** 与 Web 的 `incomingInviteText` 同一张表。 */
void IncomingTest::inviteTextMatchesWeb() {
  QCOMPARE(callstrings::incomingInviteText(true, false), QStringLiteral("邀请你视频通话"));
  QCOMPARE(callstrings::incomingInviteText(false, false), QStringLiteral("邀请你语音通话"));
  QCOMPARE(callstrings::incomingInviteText(true, true), QStringLiteral("邀请你加入群通话"));
  QCOMPARE(callstrings::incomingInviteText(false, true), QStringLiteral("邀请你加入群通话"));
}

void IncomingTest::overlayIncomingVideoHasBlockedCamera() {
  CallOverlay overlay;
  overlay.setSelfUid(QStringLiteral("alice"));
  overlay.show();
  QVERIFY(QTest::qWaitForWindowExposed(&overlay));
  overlay.beginIncoming(QStringLiteral("carol"), {QStringLiteral("alice")},
                        QStringLiteral("video"), false);
  ControlButton* camera = buttonOf(&overlay, "cameraButton");
  QVERIFY(camera != nullptr);
  QVERIFY(camera->isVisibleTo(&overlay));

  QSignalSpy notice(&overlay, &CallOverlay::notice);
  QSignalSpy accept(&overlay, &CallOverlay::acceptRequested);
  QTest::mouseClick(camera, Qt::LeftButton);
  QCOMPARE(notice.count(), 1);
  QCOMPARE(accept.count(), 0);

  CallOverlay voice;
  voice.beginIncoming(QStringLiteral("carol"), {QStringLiteral("alice")},
                      QStringLiteral("audio"), false);
  QVERIFY(!buttonOf(&voice, "cameraButton")->isVisibleTo(&voice));
}

QTEST_MAIN(IncomingTest)
#include "IncomingTest.moc"
