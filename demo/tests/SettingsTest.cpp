/**
 * SettingsTest.cpp —— 设置页「详细日志」开关。
 *
 * 钉三件事：
 *   - 什么都没存时默认**关**（= info，与 C 头的默认级别一致）；
 *   - 勾上 / 取消会写进 QSettings `log/verbose`，**重建设置页读回一致**——
 *     设置页每次打开都是新建的（MainWindow 里的 QDialog），读不回来就是「点了没存」；
 *   - 勾上真的经 C ABI 改了级别：debug 那一条过没过闸，是开关生效的唯一证据。
 *
 * QSettings 指到临时目录的 ini 上，不碰本机真实的 Demo 设置。
 */

#include <QCheckBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "EngineBridge.h"
#include "SettingsPage.h"
#include "imrtc/imrtc_c.h"

namespace {

constexpr char kKey[] = "log/verbose";

QCheckBox* verboseBox(SettingsPage& page) {
  return page.findChild<QCheckBox*>(QStringLiteral("verboseLog"));
}

/** 装一个 sink，数 debug 级别、带指定 message 的条数。 */
struct DebugCounter {
  int hits = 0;
};

void countDebug(void* user_data, imrtc_v1_log_level level, const char* message,
                const imrtc_v1_log_field*, uint32_t) {
  if (level == IMRTC_V1_LOG_DEBUG && QByteArray(message) == "settings-test-probe") {
    ++static_cast<DebugCounter*>(user_data)->hits;
  }
}

/** 往引擎日志流里打一条 debug，返回它有没有过闸。 */
bool debugPassesGate() {
  DebugCounter counter;
  imrtc_v1_set_log_sink(&countDebug, &counter);
  imrtc_v1_log(IMRTC_V1_LOG_DEBUG, "settings-test-probe", nullptr, 0);
  imrtc_v1_set_log_sink(nullptr, nullptr);
  return counter.hits == 1;
}

}  // namespace

class SettingsTest : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanupTestCase();

  void defaultsToOffWhenNothingStored();
  void toggleIsStoredAndReadBackByNewPage();
  void toggleChangesEngineLogLevel();

private:
  QTemporaryDir settingsDir_;
};

void SettingsTest::initTestCase() {
  QVERIFY(settingsDir_.isValid());
  QCoreApplication::setOrganizationName(QStringLiteral("im-rtc-test"));
  QCoreApplication::setApplicationName(QStringLiteral("SettingsTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
}

void SettingsTest::init() {
  QSettings().clear();
  EngineBridge::applyLogLevel(false);
}

void SettingsTest::cleanupTestCase() {
  EngineBridge::applyLogLevel(false);  // 级别是进程级的，别留给后面的用例
}

void SettingsTest::defaultsToOffWhenNothingStored() {
  SettingsPage page;
  QCheckBox* box = verboseBox(page);
  QVERIFY(box != nullptr);
  QVERIFY(!box->isChecked());
  QVERIFY(!EngineBridge::verboseLog());
  QVERIFY(!QSettings().contains(QLatin1String(kKey)));  // 只是打开页面，不该写
}

void SettingsTest::toggleIsStoredAndReadBackByNewPage() {
  {
    SettingsPage page;
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    QCheckBox* box = verboseBox(page);
    QVERIFY(box != nullptr);
    QTest::mouseClick(box, Qt::LeftButton, {}, QPoint(8, box->height() / 2));
    QVERIFY(box->isChecked());
  }
  QCOMPARE(QSettings().value(QLatin1String(kKey)).toBool(), true);
  {
    SettingsPage reopened;
    QCheckBox* box = verboseBox(reopened);
    QVERIFY(box != nullptr);
    QVERIFY(box->isChecked());
    box->click();  // 再关掉
  }
  QCOMPARE(QSettings().value(QLatin1String(kKey)).toBool(), false);
  SettingsPage again;
  QVERIFY(!verboseBox(again)->isChecked());
}

void SettingsTest::toggleChangesEngineLogLevel() {
  SettingsPage page;
  QCheckBox* box = verboseBox(page);
  QVERIFY(box != nullptr);
  QVERIFY(!debugPassesGate());  // 默认 info，debug 被挡

  box->click();
  QVERIFY(debugPassesGate());

  box->click();
  QVERIFY(!debugPassesGate());
}

QTEST_MAIN(SettingsTest)
#include "SettingsTest.moc"
