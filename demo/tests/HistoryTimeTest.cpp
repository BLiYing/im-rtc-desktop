#include <QtTest>

#include "CallHistory.h"
#include "CallStrings.h"

/**
 * 通话记录的时间与标题文案。时间规则四端统一（用例表与 Android `HistoryTimeTest` 逐条对应）：
 * 今天 HH:mm / 昨天 昨天 HH:mm / 今年更早 M月d日 HH:mm / 往年 yyyy年M月d日 HH:mm。
 * 时区固定 Asia/Shanghai、时分格式固定 HH:mm，测试不依赖机器时区与 locale。
 */
class HistoryTimeTest : public QObject {
  Q_OBJECT

private slots:
  void todayShowsOnlyTime();
  void midnightSharpIsStillToday();
  void yesterday();
  void crossMidnightUsesCalendarDayNot24Hours();
  void earlierThisYearShowsMonthDay();
  void earlierYearsShowYear();
  void newYearsDayLooksBackToYesterday();
  void futureRecordCountsAsToday();
  void groupTitleCountsMembersAtLeastOne();
  void oneToOneTitleFallsBackToUnknown();
  void groupSummaryUsesDirectionAndOutcome();
};

namespace {

const QTimeZone kZone(QByteArrayLiteral("Asia/Shanghai"));

qint64 at(int y, int mo, int d, int h, int mi) {
  return QDateTime(QDate(y, mo, d), QTime(h, mi), kZone).toMSecsSinceEpoch();
}

QString fmt(qint64 started, qint64 now) {
  return callstrings::callTime(started, now, kZone, [](const QDateTime& when) {
    return when.time().toString(QStringLiteral("HH:mm"));
  });
}

const qint64 kNow = at(2026, 9, 19, 11, 40);

}  // namespace

void HistoryTimeTest::todayShowsOnlyTime() {
  QCOMPARE(fmt(at(2026, 9, 19, 11, 35), kNow), QStringLiteral("11:35"));
}
void HistoryTimeTest::midnightSharpIsStillToday() {
  QCOMPARE(fmt(at(2026, 9, 19, 0, 0), kNow), QStringLiteral("00:00"));
}
void HistoryTimeTest::yesterday() {
  QCOMPARE(fmt(at(2026, 9, 18, 23, 11), kNow), QStringLiteral("昨天 23:11"));
}
void HistoryTimeTest::crossMidnightUsesCalendarDayNot24Hours() {
  QCOMPARE(fmt(at(2026, 9, 18, 23, 50), at(2026, 9, 19, 0, 10)), QStringLiteral("昨天 23:50"));
}
void HistoryTimeTest::earlierThisYearShowsMonthDay() {
  QCOMPARE(fmt(at(2026, 9, 15, 18, 17), kNow), QStringLiteral("9月15日 18:17"));
}
void HistoryTimeTest::earlierYearsShowYear() {
  QCOMPARE(fmt(at(2025, 12, 31, 9, 5), kNow), QStringLiteral("2025年12月31日 09:05"));
}
void HistoryTimeTest::newYearsDayLooksBackToYesterday() {
  QCOMPARE(fmt(at(2025, 12, 31, 23, 59), at(2026, 1, 1, 8, 0)), QStringLiteral("昨天 23:59"));
}
void HistoryTimeTest::futureRecordCountsAsToday() {
  QCOMPARE(fmt(at(2026, 9, 19, 11, 50), kNow), QStringLiteral("11:50"));
}

void HistoryTimeTest::groupTitleCountsMembersAtLeastOne() {
  CallRecord record;
  record.isGroup = true;
  QCOMPARE(callstrings::recordTitle(record), QStringLiteral("群通话 · 1 人"));
  record.members = QStringList{QStringLiteral("alice"), QStringLiteral("bob"), QStringLiteral("carol")};
  QCOMPARE(callstrings::recordTitle(record), QStringLiteral("群通话 · 3 人"));
}

void HistoryTimeTest::oneToOneTitleFallsBackToUnknown() {
  CallRecord record;
  record.peer = QStringLiteral("bob");
  QCOMPARE(callstrings::recordTitle(record), QStringLiteral("bob"));
  record.peer.clear();
  QCOMPARE(callstrings::recordTitle(record), QStringLiteral("（未知）"));
}

/** 群通话的第二行与 1v1 同一套（方向 · 结果），人数只在第一行。 */
void HistoryTimeTest::groupSummaryUsesDirectionAndOutcome() {
  CallRecord record;
  record.isGroup = true;
  record.outgoing = true;
  record.connected = true;
  record.durationSec = 175;
  QCOMPARE(callstrings::recordSummary(record), QStringLiteral("呼出 · 02:55"));
  record.connected = false;
  record.durationSec = 0;
  record.reason = QStringLiteral("cancel");
  QCOMPARE(callstrings::recordSummary(record), QStringLiteral("呼出 · 已取消"));
}

QTEST_MAIN(HistoryTimeTest)
#include "HistoryTimeTest.moc"
