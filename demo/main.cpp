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
 *   --call   bob                     连上之后自动拨一次
 *   --video                          --call 用视频（默认语音）
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
  QCoreApplication::setApplicationName(QStringLiteral("Desktop Demo"));
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
      QStringLiteral("call"), QCoreApplication::translate("main", "连上之后自动拨这个人。"),
      QStringLiteral("uid"));
  const QCommandLineOption videoOption(
      QStringLiteral("video"), QCoreApplication::translate("main", "--call 用视频，默认语音。"));
  parser.addOption(serverOption);
  parser.addOption(userOption);
  parser.addOption(callOption);
  parser.addOption(videoOption);
  parser.process(app);

  // 语言要在建界面**之前**装好，否则第一屏是源语言（中文）、切换后才对。
  language::apply(language::current());

  MainWindow window;
  window.show();

  if (parser.isSet(userOption)) {
    window.autoLogin(parser.value(serverOption), parser.value(userOption),
                     parser.value(callOption),
                     parser.isSet(videoOption) ? QStringLiteral("video")
                                               : QStringLiteral("audio"));
  }
  return app.exec();
}
