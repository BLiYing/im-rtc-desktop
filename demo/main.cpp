/**
 * main.cpp —— Qt Demo 的入口。
 *
 * 这个 Demo 存在的意义只有一个：**证明只用公开的 C ABI 就能做出完整体验**。
 * 所以它链的是 `libim_rtc_engine_capi.dylib` / `.dll`——与集成方拿到的
 * 是同一个动态库、同一个 C 头。它没有任何内部捷径。
 *
 * 联调参数（都可省）：
 *   --server http://127.0.0.1:8787   预填服务器
 *   --user   alice                   预填用户 ID，并**自动登录**
 *   --call   bob[,carol,…]            连上之后自动拨一次，多于一个即群通话
 *   --video                          --call 用视频（默认语音）
 *   --profile bob                    独立的设置与通话记录
 *   --auto-accept                    收到来电就接（联调用）
 *   --hangup-after 5                 接通 / 进房 5 秒后自动退出（联调用）
 *   --invite dave                    接通后立刻 invite_more 一个人（只有主叫能发）
 *   --room   new | 8827-1190         走会议房那条路：new 表示先建一个
 *   --fake-video                     每个格子都当成有画面并贴测试图案。
 *                                    用来在没有媒体的情况下验渲染路径 A 的宿主侧。
 *
 * `--profile` 是给「同一台机器上开两个实例互打」用的。macOS 的
 * QStandardPaths **不理会 $HOME**（它走的是密码库里的真实家目录），
 * 所以靠环境变量隔离不了——两个实例会写同一份 call-history.json 互相覆盖。
 * 换 applicationName 才是有效的隔离方式。
 */

#include <QApplication>
#include <QCommandLineParser>

#include "Language.h"
#include "MainWindow.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  // QSettings 要靠这三项定位存储路径（设备 id、上次填的服务器、语言都存在那）。
  QCoreApplication::setOrganizationName(QStringLiteral("im-rtc"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("imrtc.dev"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QCoreApplication::translate("main", "im-rtc 桌面端参考实现（经 C ABI 调引擎）"));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption serverOption(QStringLiteral("server"),
                                        QCoreApplication::translate("main", "预填服务器地址。"),
                                        QStringLiteral("url"));
  const QCommandLineOption userOption(
      QStringLiteral("user"),
      QCoreApplication::translate("main", "预填用户 ID，并自动登录。"),
      QStringLiteral("uid"));
  const QCommandLineOption callOption(
      QStringLiteral("call"),
      QCoreApplication::translate("main", "连上之后自动拨这些人，逗号分隔；多于一个即群通话。"),
      QStringLiteral("uid[,uid…]"));
  const QCommandLineOption videoOption(
      QStringLiteral("video"), QCoreApplication::translate("main", "--call 用视频，默认语音。"));
  const QCommandLineOption profileOption(
      QStringLiteral("profile"),
      QCoreApplication::translate("main", "独立的设置与通话记录，用于同机开两个实例互打。"),
      QStringLiteral("name"));
  parser.addOption(serverOption);
  parser.addOption(userOption);
  parser.addOption(callOption);
  parser.addOption(videoOption);
  const QCommandLineOption autoAcceptOption(
      QStringLiteral("auto-accept"),
      QCoreApplication::translate("main", "收到来电就接，联调用。"));
  const QCommandLineOption hangupAfterOption(
      QStringLiteral("hangup-after"),
      QCoreApplication::translate("main", "接通 / 进房 N 秒后自动退出，联调用。"),
      QStringLiteral("sec"));
  const QCommandLineOption inviteOption(
      QStringLiteral("invite"),
      QCoreApplication::translate("main", "接通后立刻加一个人进来，联调用。"),
      QStringLiteral("uid"));
  const QCommandLineOption roomOption(
      QStringLiteral("room"),
      QCoreApplication::translate("main", "进会议房；new 表示先建一个。"),
      QStringLiteral("id|new"));
  parser.addOption(profileOption);
  parser.addOption(autoAcceptOption);
  parser.addOption(hangupAfterOption);
  parser.addOption(inviteOption);
  const QCommandLineOption fakeVideoOption(
      QStringLiteral("fake-video"),
      QCoreApplication::translate(
          "main", "每个格子都当成有画面并贴测试图案，验渲染路径 A 的宿主侧。"));
  parser.addOption(roomOption);
  parser.addOption(fakeVideoOption);
  parser.process(app);

  // applicationName 决定 QSettings 与 AppDataLocation 的位置，所以要在
  // 建任何界面（会读 QSettings）**之前**定下来。
  const QString profile = parser.value(profileOption);
  QCoreApplication::setApplicationName(
      profile.isEmpty() ? QStringLiteral("Desktop Demo")
                        : QStringLiteral("Desktop Demo [%1]").arg(profile));

  // 语言要在建界面**之前**装好，否则第一屏是源语言（中文）、切换后才对。
  language::apply(language::current());

  MainWindow window;
  window.setAutomation(parser.isSet(autoAcceptOption),
                       parser.value(hangupAfterOption).toInt(),
                       parser.value(inviteOption));
  window.setFakeVideo(parser.isSet(fakeVideoOption));
  window.show();

  if (parser.isSet(userOption)) {
    window.autoLogin(parser.value(serverOption), parser.value(userOption),
                     parser.value(callOption).split(QLatin1Char(','), Qt::SkipEmptyParts),
                     parser.isSet(videoOption) ? QStringLiteral("video")
                                               : QStringLiteral("audio"),
                     parser.value(roomOption));
  }
  return app.exec();
}
