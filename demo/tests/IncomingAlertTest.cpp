/**
 * IncomingAlertTest.cpp —— 窗口在后台时的来电提醒（UX_FLOWS §07 第 3 条）。
 *
 * 钉的是三件**出错时不报任何错**的事：
 *   - 窗口在前台时**不**弹系统通知（只有横幅）；否则每通来电都是横幅 + 通知两遍；
 *   - 接通 / 结束之后要撤：漏了 `clear()`，对方早挂了 Dock 还在跳；
 *   - 通话结束后才点到旧通知，**不能**再展开来电浮层。
 *
 * 系统那一侧注入假件：这组测试跑在真窗口上（test.sh 不开 offscreen），用真件会真弹通知。
 */

#include <QSignalSpy>
#include <QWidget>
#include <QtTest>

#include "IncomingAlert.h"

namespace {

struct Record {
  QStringList posts;
  int withdraws = 0;
  int attentions = 0;
  int cancels = 0;
};

class FakeSink : public IncomingAlert::Sink {
public:
  explicit FakeSink(Record* record) : record_(record) {}
  void post(const QString& title, const QString& body) override {
    record_->posts << title + QLatin1Char('|') + body;
  }
  void withdraw() override { ++record_->withdraws; }
  void requestAttention(QWidget*) override { ++record_->attentions; }
  void cancelAttention() override { ++record_->cancels; }

private:
  Record* record_;
};

incomingalert::WindowPresence presence(bool visible, bool minimized, bool active) {
  incomingalert::WindowPresence p;
  p.visible = visible;
  p.minimized = minimized;
  p.active = active;
  return p;
}

const incomingalert::WindowPresence kForeground = presence(true, false, true);
const incomingalert::WindowPresence kBehindOtherApp = presence(true, false, false);

}  // namespace

class IncomingAlertTest : public QObject {
  Q_OBJECT

private slots:
  void needsSystemAlertTruthTable();
  void foregroundKeepsBannerOnly();
  void backgroundPostsNotificationAndRequestsAttention();
  void clearWithdrawsOnceAndIsIdempotent();
  void clickOpensOnlyWhileRinging();
  void presenceOfHiddenWindowNeedsAlert();
};

void IncomingAlertTest::needsSystemAlertTruthTable() {
  QVERIFY(!incomingalert::needsSystemAlert(kForeground));
  QVERIFY(incomingalert::needsSystemAlert(kBehindOtherApp));
  QVERIFY(incomingalert::needsSystemAlert(presence(true, true, false)));
  // 最小化了哪怕还报 active，也看不见横幅。
  QVERIFY(incomingalert::needsSystemAlert(presence(true, true, true)));
  QVERIFY(incomingalert::needsSystemAlert(presence(false, false, false)));
}

void IncomingAlertTest::foregroundKeepsBannerOnly() {
  QWidget window;
  Record record;
  IncomingAlert alert(&window, std::make_unique<FakeSink>(&record));
  QVERIFY(!alert.ring(kForeground, QStringLiteral("carol"), QStringLiteral("video"), false));
  QVERIFY(!alert.isAlerting());
  QVERIFY(record.posts.isEmpty());
  QCOMPARE(record.attentions, 0);
}

void IncomingAlertTest::backgroundPostsNotificationAndRequestsAttention() {
  QWidget window;
  Record record;
  IncomingAlert alert(&window, std::make_unique<FakeSink>(&record));
  QVERIFY(alert.ring(kBehindOtherApp, QStringLiteral("carol"), QStringLiteral("video"), false));
  QVERIFY(alert.isAlerting());
  QCOMPARE(record.attentions, 1);
  // 标题是来电人，正文是与横幅同一句邀请语。
  QCOMPARE(record.posts, QStringList{QStringLiteral("carol|邀请你视频通话")});

  alert.clear();
  QVERIFY(alert.ring(kBehindOtherApp, QStringLiteral("dave"), QStringLiteral("audio"), true));
  QCOMPARE(record.posts.last(), QStringLiteral("dave|邀请你加入群通话"));
}

void IncomingAlertTest::clearWithdrawsOnceAndIsIdempotent() {
  QWidget window;
  Record record;
  IncomingAlert alert(&window, std::make_unique<FakeSink>(&record));
  alert.clear();  // 没在提醒：什么都不做
  QCOMPARE(record.withdraws, 0);

  alert.ring(kBehindOtherApp, QStringLiteral("carol"), QStringLiteral("audio"), false);
  alert.clear();
  alert.clear();  // 接通与结束先后到达：第二次无害
  QCOMPARE(record.withdraws, 1);
  QCOMPARE(record.cancels, 1);
  QVERIFY(!alert.isAlerting());
}

void IncomingAlertTest::clickOpensOnlyWhileRinging() {
  QWidget window;
  Record record;
  IncomingAlert alert(&window, std::make_unique<FakeSink>(&record));
  QSignalSpy open(&alert, &IncomingAlert::openRequested);

  alert.notificationClicked();
  QCOMPARE(open.count(), 0);

  alert.ring(kBehindOtherApp, QStringLiteral("carol"), QStringLiteral("video"), false);
  alert.notificationClicked();
  QCOMPARE(open.count(), 1);
  // 点了就收：停跳 Dock、收托盘。
  QVERIFY(!alert.isAlerting());
  QCOMPARE(record.cancels, 1);
  QCOMPARE(record.withdraws, 1);

  // 通话结束后才点到那条旧通知：不再展开。
  alert.notificationClicked();
  QCOMPARE(open.count(), 1);
}

void IncomingAlertTest::presenceOfHiddenWindowNeedsAlert() {
  QWidget window;  // 从没 show 过
  const incomingalert::WindowPresence p = incomingalert::presenceOf(&window);
  QVERIFY(!p.visible);
  QVERIFY(incomingalert::needsSystemAlert(p));
}

QTEST_MAIN(IncomingAlertTest)
#include "IncomingAlertTest.moc"
