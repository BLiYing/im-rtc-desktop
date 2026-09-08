#include "RemoteLogSink.h"

#include <QDateTime>
#include <QThread>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>

#include "imrtc/CallEngine.hpp"

namespace {

/** kFlushIntervalMs 是攒批的节奏。够把一次状态跃迁的前后文凑齐，又不至于一条一个请求。 */
constexpr int kFlushIntervalMs = 2000;
/**
 * kMaxPending 是队列上限。**满了丢最旧的**——留住最近的现场比留住开头更有用，
 * 而报障永远是「刚才那通电话」。
 */
constexpr int kMaxPending = 500;
/** kMaxPerBatch 与服务端 `devLogMaxEntries` 对齐；超了服务端会自己截断。 */
constexpr int kMaxPerBatch = 200;
/** kTimeoutMs 显式设短。iOS 踩过默认 60 秒，一个卡住的请求让后面所有日志静默丢掉。 */
constexpr int kTimeoutMs = 5000;

}  // namespace

RemoteLogSink::RemoteLogSink(QString httpBase, QString client, QObject* parent)
    : QObject(parent), httpBase_(std::move(httpBase)), client_(std::move(client)) {
  http_ = new QNetworkAccessManager(this);
  timer_ = new QTimer(this);
  timer_->setInterval(kFlushIntervalMs);
  connect(timer_, &QTimer::timeout, this, &RemoteLogSink::flush);
}

RemoteLogSink::~RemoteLogSink() { uninstall(); }

void RemoteLogSink::install() {
  if (installed_) return;
  installed_ = true;
  timer_->start();

  imrtc::capi::setLogSink([this](imrtc_v1_log_level level, const std::string& message,
                                 const std::vector<std::pair<std::string, std::string>>& fields) {
    QStringList pairs;
    pairs.reserve(static_cast<qsizetype>(fields.size()) * 2);
    for (const auto& field : fields) {
      pairs << QString::fromStdString(field.first) << QString::fromStdString(field.second);
    }
    enqueue(level, QString::fromStdString(message), pairs);
  });
}

void RemoteLogSink::uninstall() {
  if (!installed_) return;
  installed_ = false;
  imrtc::capi::setLogSink(nullptr);
  timer_->stop();
  // 最后一批也要送走：宿主退出前那几条往往正是要看的。
  flush();
}

void RemoteLogSink::enqueue(imrtc_v1_log_level level, const QString& message,
                            const QStringList& fieldPairs) {
  /*
    **可能在任意线程被调到**（头文件里就是这么承诺的），而 QNetworkAccessManager
    与 QTimer 只能在本对象自己的线程上用。所以先切回来再动队列。

    这里**绝不能打日志**——sink 里打日志就是无限递归，而且是那种「一跑就栈溢出」
    的递归，不是慢慢发现的。
  */
  if (QThread::currentThread() != thread()) {
    QMetaObject::invokeMethod(
        this, [this, level, message, fieldPairs] { enqueue(level, message, fieldPairs); },
        Qt::QueuedConnection);
    return;
  }

  Entry entry;
  entry.atMs = QDateTime::currentMSecsSinceEpoch();
  entry.level = QString::fromLatin1(
      level == IMRTC_V1_LOG_DEBUG   ? "debug"
      : level == IMRTC_V1_LOG_WARN  ? "warn"
      : level == IMRTC_V1_LOG_ERROR ? "error"
                                    : "info");
  entry.message = message;
  entry.fieldPairs = fieldPairs;

  pending_.append(entry);
  while (pending_.size() > kMaxPending) pending_.removeFirst();
}

void RemoteLogSink::flush() {
  if (pending_.isEmpty() || http_ == nullptr) return;

  QJsonArray entries;
  const int take = static_cast<int>(std::min<qsizetype>(pending_.size(), kMaxPerBatch));
  for (int i = 0; i < take; ++i) {
    const Entry& entry = pending_.at(i);
    QJsonObject fields;
    for (int f = 0; f + 1 < entry.fieldPairs.size(); f += 2) {
      fields[entry.fieldPairs.at(f)] = entry.fieldPairs.at(f + 1);
    }
    QJsonObject item;
    // 用**客户端自己的**时间戳：要跟服务端日志对时间轴，用服务端收到的时刻就对不上了。
    item[QStringLiteral("at_ms")] = entry.atMs;
    item[QStringLiteral("level")] = entry.level;
    item[QStringLiteral("msg")] = entry.message;
    item[QStringLiteral("fields")] = fields;
    entries.append(item);
  }
  // **先摘再发**：发失败不回队（见头文件的第三个坑），所以这里就把它们放掉。
  pending_.erase(pending_.begin(), pending_.begin() + take);

  QUrl url(httpBase_.trimmed());
  if (url.scheme().isEmpty()) url.setScheme(QStringLiteral("http"));
  url.setPath(QStringLiteral("/v1/dev/logs"));

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setTransferTimeout(kTimeoutMs);

  QJsonObject body;
  body[QStringLiteral("client")] = client_;
  body[QStringLiteral("entries")] = entries;

  QNetworkReply* reply = http_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
  /*
    失败什么都不做：**不重试、不回队、也不报错给界面**。

    日志回传是尽力而为的。重试只会在网络本来就不好的时候雪上加霜，而那正是我们最
    需要它别添乱的时刻；报错给界面则更糟——用户会以为通话出了问题。
    这条路由只在服务端开着 `-demo-login` 时才存在，连不上是**常态**而不是异常。
  */
}
