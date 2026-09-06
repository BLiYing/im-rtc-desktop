/**
 * Shots.cpp —— 离线界面截图工具。
 *
 *   ./imrtc_demo_shots <输出目录> [zh|en]
 *
 * **不连服务端、不需要引擎活着**：直接把界面控件摆到各个状态上截图。
 * 用途有两个，都不是「给人看着玩」：
 *
 *   1. 媒体那一刀还没落地，真通话跑不出画面，但**界面状态机是真的**——
 *      这个工具能把「拨出中 / 来电 / 群通话九宫格 / 已结束」逐个渲染出来验收。
 *   2. 以后改主题令牌或版式，重跑一次就能逐图对比，不必手工复现每个态。
 *
 * 注意它喂的是**假数据**：这些图证明的是版式与令牌对不对，
 * **不证明**任何一条协议行为——那些由 `tests/` 与 `scripts/smoke.sh` 证明。
 */

#include <QApplication>
#include <QDir>
#include <QScreen>
#include <QSplitter>
#include <QStandardPaths>
#include <QThread>
#include <QPixmap>
#include <QTextStream>

#include "CallHistory.h"
#include "CallOverlay.h"
#include "DialPage.h"
#include "HistoryPage.h"
#include "Language.h"
#include "LoginPage.h"
#include "SettingsPage.h"

namespace {

/** 截一张。widget 必须已经 show 过，否则布局还没算。 */
void shoot(QWidget* widget, const QString& dir, const QString& name) {
  widget->show();
  QApplication::processEvents();
  widget->grab().save(QStringLiteral("%1/%2.png").arg(dir, name));
  QTextStream(stdout) << "  " << name << ".png\n";
  widget->hide();
}

CallRecord record(const QString& peer, bool outgoing, bool connected, const QString& reason,
                  qint64 duration, const QString& mediaType, int minutesAgo) {
  CallRecord r;
  r.callId = QStringLiteral("call-%1").arg(peer);
  r.peer = peer;
  r.members = QStringList{peer};
  r.mediaType = mediaType;
  r.outgoing = outgoing;
  r.connected = connected;
  r.reason = reason;
  r.durationSec = duration;
  r.endedAt = QDateTime::currentDateTime().addSecs(-60 * minutesAgo);
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("im-rtc"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("imrtc.dev"));
  QCoreApplication::setApplicationName(QStringLiteral("Desktop Demo Shots"));
  // **别把假数据写进用户的真目录。**这一行把 QSettings 与 AppDataLocation
  // 都重定向到测试位置，截图工具喂的假通话记录才不会混进 Demo 的真记录里。
  QStandardPaths::setTestModeEnabled(true);

  const QStringList args = QCoreApplication::arguments();
  if (args.size() < 2) {
    QTextStream(stderr) << "用法：imrtc_demo_shots <输出目录> [zh|en]\n";
    return 2;
  }
  const QString dir = args.at(1);
  if (!QDir().mkpath(dir)) {
    QTextStream(stderr) << "建不了目录：" << dir << "\n";
    return 1;
  }
  const bool english = args.size() > 2 && args.at(2) == QLatin1String("en");
  language::apply(english ? language::Choice::English : language::Choice::ZhCN);
  const QString suffix = english ? QStringLiteral("-en") : QString();

  QTextStream(stdout) << "输出到 " << dir << "：\n";

  // ---- 登录屏 ----
  LoginPage login;
  login.resize(560, 520);
  shoot(&login, dir, QStringLiteral("01-login%1").arg(suffix));

  // ---- 拨号屏 ----
  DialPage dial;
  dial.resize(420, 560);
  dial.setIdentity(QStringLiteral("alice"), QStringLiteral("Alice"));
  dial.setConnectionText(QStringLiteral("http://127.0.0.1:8787 · 已连接"), true);
  shoot(&dial, dir, QStringLiteral("02-dial%1").arg(suffix));

  // ---- 通话记录屏（喂几条假记录，覆盖各种 reason）----
  CallHistory history;
  // **从旧到新喂**：`add()` 是 prepend，真实使用中通话按时间先后结束，
  // 所以喂进来的顺序也必须是旧→新，否则列表会倒过来。
  for (const CallRecord& r : {
           record(QStringLiteral("frank"), true, false, QStringLiteral("offline"), 0,
                  QStringLiteral("audio"), 130),
           record(QStringLiteral("erin"), true, false, QStringLiteral("busy"), 0,
                  QStringLiteral("audio"), 95),
           record(QStringLiteral("dave"), true, false, QStringLiteral("cancel"), 0,
                  QStringLiteral("audio"), 80),
           record(QStringLiteral("carol"), false, false, QStringLiteral("no_answer"), 0,
                  QStringLiteral("video"), 40),
           record(QStringLiteral("bob"), true, true, QStringLiteral("hangup"), 201,
                  QStringLiteral("audio"), 5),
       }) {
    history.add(r);
  }
  HistoryPage historyPage(&history);
  historyPage.resize(380, 560);
  shoot(&historyPage, dir, QStringLiteral("03-history%1").arg(suffix));

  // ---- 设置屏 ----
  SettingsPage settings;
  settings.resize(520, 560);
  settings.setSession(QStringLiteral("alice"), QStringLiteral("ws://127.0.0.1:8787/v1/ws"),
                      QStringLiteral("sess-4f7a91c2"));
  shoot(&settings, dir, QStringLiteral("04-settings%1").arg(suffix));

  // ---- 主界面：左拨号 / 右记录（就是 MainWindow 用的那两个控件）----
  auto* split = new QSplitter(Qt::Horizontal);
  auto* homeDial = new DialPage;
  homeDial->setIdentity(QStringLiteral("alice"), QStringLiteral("Alice"));
  homeDial->setConnectionText(QStringLiteral("http://127.0.0.1:8787 · 已连接"), true);
  auto* homeHistory = new HistoryPage(&history);
  split->addWidget(homeDial);
  split->addWidget(homeHistory);
  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 2);
  split->resize(900, 560);
  shoot(split, dir, QStringLiteral("10-home%1").arg(suffix));
  delete split;

  // ---- 通话浮窗的四个态 ----
  CallOverlay outgoing;
  outgoing.setSelfUid(QStringLiteral("alice"));
  outgoing.beginOutgoing({QStringLiteral("bob")}, QStringLiteral("audio"), false);
  shoot(&outgoing, dir, QStringLiteral("05-call-outgoing%1").arg(suffix));

  CallOverlay incoming;
  incoming.setSelfUid(QStringLiteral("alice"));
  incoming.beginIncoming(QStringLiteral("carol"), {QStringLiteral("alice")},
                         QStringLiteral("video"), false);
  shoot(&incoming, dir, QStringLiteral("06-call-incoming%1").arg(suffix));

  CallOverlay connected;
  connected.setSelfUid(QStringLiteral("alice"));
  connected.beginOutgoing({QStringLiteral("bob")}, QStringLiteral("video"), false);
  connected.markConnected(QStringLiteral("caller"));
  shoot(&connected, dir, QStringLiteral("07-call-connected%1").arg(suffix));

  CallOverlay ended;
  ended.setSelfUid(QStringLiteral("alice"));
  ended.beginOutgoing({QStringLiteral("bob")}, QStringLiteral("audio"), false);
  ended.markEnded(QStringLiteral("busy"), 0);
  shoot(&ended, dir, QStringLiteral("08-call-ended%1").arg(suffix));

  // ---- 渲染路径 A：格子里真的塞一个原生子窗口（设计 §8.3）----
  //
  // 实测过：Qt 6.8 / macOS 上 `QWidget::grab()` **能**抓到原生子窗口，
  // 与系统合成（QScreen::grabWindow）逐像素比过，平均每通道只差 0.54/255。
  // 所以这一张和别的图走同一条截图路径，不需要特殊处理。
  {
    CallOverlay surfaces;
    surfaces.setSelfUid(QStringLiteral("alice"));
    surfaces.setFakeVideo(true);
    surfaces.beginOutgoing({QStringLiteral("bob"), QStringLiteral("carol"),
                            QStringLiteral("dave"), QStringLiteral("erin")},
                           QStringLiteral("video"), true);
    surfaces.markConnected(QStringLiteral("caller"));
    for (const QString& who : {QStringLiteral("bob"), QStringLiteral("carol"),
                               QStringLiteral("dave"), QStringLiteral("erin")}) {
      surfaces.onMemberAccepted(who);
    }
    // 外壳的几种状态：它们由压在画面之上的那层画，被盖住的话会一起消失。
    surfaces.onMemberAudio(QStringLiteral("carol"), false);
    surfaces.onSpeakers({SpeakerInfo{QStringLiteral("bob"), 80}});
    surfaces.onQuality({QualityInfo{QStringLiteral("dave"), 2}});
    surfaces.show();
    // 原生层的几何在 show 之后才落定，多转几圈事件循环再抓。
    for (int i = 0; i < 20; ++i) {
      QApplication::processEvents();
      QThread::msleep(10);
    }
    /*
      **这一张必须走 QScreen::grabWindow，不能用 QWidget::grab()。**

      实测（Qt 6.8.3 / macOS，逐像素比平均每通道差值）：
        grab() 单独跑          vs 系统合成 → 差 5.11/255（原生层只画了一半）
        先跑一次系统合成再 grab() vs 系统合成 → 差 0.54/255
      也就是说 grab() **能**看到原生子窗口，但拿到的可能是上一帧；
      要等窗口服务器真的合成过一次才对得上。试过 `[CATransaction flush]`，没用。

      别的场景没有原生子窗口，照旧用 grab()。
    */
    QPixmap shot;
    if (QScreen* screen = surfaces.screen()) shot = screen->grabWindow(surfaces.winId());
    if (shot.isNull()) {
      // grabWindow 在某些环境要「屏幕录制」授权。退回 grab()，但要说清楚。
      QTextStream(stderr) << "  系统合成不可用，退回 QWidget::grab()（原生层可能滞后一帧）\n";
      shot = surfaces.grab();
    }
    shot.save(QStringLiteral("%1/11-call-group-video%2.png").arg(dir, suffix));
    QTextStream(stdout) << "  11-call-group-video" << suffix << ".png\n";
    surfaces.hide();
  }

  // ---- 群通话九宫格：把格子的各种状态都摆出来 ----
  CallOverlay group;
  group.setSelfUid(QStringLiteral("alice"));
  group.beginOutgoing({QStringLiteral("bob"), QStringLiteral("carol"), QStringLiteral("dave"),
                       QStringLiteral("erin"), QStringLiteral("grace")},
                      QStringLiteral("video"), true);
  group.markConnected(QStringLiteral("caller"));
  group.onMemberAccepted(QStringLiteral("bob"));
  group.onMemberAccepted(QStringLiteral("carol"));
  group.onMemberAccepted(QStringLiteral("dave"));
  group.onMemberAudio(QStringLiteral("carol"), false);              // 静音角标
  group.onSpeakers({SpeakerInfo{QStringLiteral("bob"), 72}});       // 绿色内描边
  group.onQuality({QualityInfo{QStringLiteral("dave"), 2}});        // 弱网
  group.onMemberRejected(QStringLiteral("erin"));                   // 已离开
  shoot(&group, dir, QStringLiteral("09-call-group%1").arg(suffix));

  return 0;
}
