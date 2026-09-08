#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "imrtc/imrtc_c.h"

class QNetworkAccessManager;
class QTimer;

/**
 * RemoteLogSink 把 engine 与 Demo 的日志攒批送回服务端（`POST /v1/dev/logs`）。
 *
 * # 为什么它在 Demo 里而不是 engine 里
 *
 * engine 不认识 HTTP，也不该认识：它对外只有一个 `setLogSink`，
 * 怎么落地是**宿主的选择**（LOGGING.md §7）。iOS 的 `RemoteLogSink`、
 * Android 的 `DemoLogSink` 都在各自的 Demo 里，本仓照这个形状。
 *
 * # 为什么值得做
 *
 * 四端联调时客户端日志只活在各自的机器上，要分析一次问题就得人工复制粘贴——
 * 复制不全、顺序错乱、时间对不上服务端。送回服务端之后，一次通话的两端加服务端
 * 就能用 `im-rtc-server/scripts/timeline.py` 按时间轴放在一起读。
 *
 * # 三个坑（照 iOS / Android 的教训避开，见 CLIENT_PARITY 的 androidlogsink 脚注）
 *
 * - **超时显式设短**：iOS 踩过默认 60 秒 + 一个发送闩，一个卡住的请求让后面所有
 *   日志静默丢掉。这里 5 秒。
 * - **队列满了丢最旧的**：留住最近的现场比留住开头更有用。
 * - **发失败不重试、不回队**：日志回传是尽力而为的；重试只会在网络本来就不好的时候
 *   雪上加霜，而那正是我们最需要它别添乱的时刻。
 */
class RemoteLogSink : public QObject {
  Q_OBJECT

public:
  /**
   * client 是自报身份，服务端据此分文件：`client-<client>.log`。
   *
   * 自己持一个 `QNetworkAccessManager`，不借 `EngineBridge` 的：回传是**尽力而为**的
   * 旁路，不该和取票、建会议房那些真正会影响通话的请求挤在同一个连接池里。
   */
  RemoteLogSink(QString httpBase, QString client, QObject* parent = nullptr);
  ~RemoteLogSink() override;

  /** install 把自己装进 engine 的日志出口（进程级）。**只该装一个**。 */
  void install();
  /** uninstall 卸掉并把攒着的最后一批送走。 */
  void uninstall();

  /** enqueue 收一条。**可能在任意线程被调到**，内部会切回本对象的线程。 */
  void enqueue(imrtc_v1_log_level level, const QString& message, const QStringList& fieldPairs);

private slots:
  void flush();

private:
  /** Entry 是攒着的一条。 */
  struct Entry {
    qint64 atMs = 0;
    QString level;
    QString message;
    QStringList fieldPairs;  // [k1, v1, k2, v2, …]
  };

  QNetworkAccessManager* http_ = nullptr;
  QString httpBase_;
  QString client_;
  QTimer* timer_ = nullptr;
  QList<Entry> pending_;
  bool installed_ = false;
};
