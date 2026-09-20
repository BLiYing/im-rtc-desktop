#include "EngineBridge.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {

Q_LOGGING_CATEGORY(lcBridge, "imrtc.demo.bridge")

/** 宿主按 ~200ms~1s 的粒度 tick 即可（C 头里写的）。取下限，状态跳转跟手。 */
constexpr int kTickIntervalMs = 200;

/** 设置页「详细日志」。键名与 Language.cpp 的 "ui/language" 同一风格：分组/项。 */
constexpr char kVerboseLogKey[] = "log/verbose";

QString qs(const std::string& value) { return QString::fromStdString(value); }

std::vector<std::string> stdStrings(const QStringList& values) {
  std::vector<std::string> out;
  out.reserve(static_cast<std::size_t>(values.size()));
  for (const QString& value : values) out.push_back(value.toStdString());
  return out;
}

}  // namespace

/**
 * reportFailure / logFailure 是发起类动作的结果回调（2.0.0：失败只从结果回来，不再经 onError）。
 *
 * 结果在 tick 所在线程（就是 GUI 线程）回调。非退出类的失败照旧弹 toast（走 engineError 那条信号，
 * forType 位置放动作名）；退出类（拒接 / 取消 / 挂断 / 离房）失败时引擎已经本地收场，只记日志。
 */
std::function<void(imrtc::capi::Result<>)> EngineBridge::reportFailure(const char* action) {
  return [this, action](imrtc::capi::Result<> result) {
    if (result.ok()) return;
    qCWarning(lcBridge, "%s 失败 %d %s", action, result.code, result.name.c_str());
    emit engineError(result.code, qs(result.name), QString::fromLatin1(action));
  };
}

std::function<void(imrtc::capi::Result<>)> EngineBridge::logFailure(const char* action) {
  return [action](imrtc::capi::Result<> result) {
    if (result.ok()) return;
    qCInfo(lcBridge, "%s 失败 %d %s（引擎已本地收场）", action, result.code, result.name.c_str());
  };
}

EngineBridge::EngineBridge(QObject* parent) : QObject(parent) {
  http_ = new QNetworkAccessManager(this);

  ticker_ = new QTimer(this);
  ticker_->setInterval(kTickIntervalMs);
  connect(ticker_, &QTimer::timeout, this, [this] {
    if (engine_) engine_->tick();
  });
  watchSystemSignals();
}

/*
  回前台 / 网络变了就告诉引擎——**宿主该写的就是这几行**（引擎不碰 OS，见 imrtc_c.h）。
  断线后引擎就不再按退避白等：正等着重连的立刻连，连着的先探 3 秒（2026-09-18，五端同形）。
  桌面上「回前台」= App 重新被激活（含睡眠唤醒后用户点回来）；网络走 Qt 的 QNetworkInformation。
*/
void EngineBridge::watchSystemSignals() {
  if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
      if (engine_) engine_->setAppForeground(state == Qt::ApplicationActive);
    });
  }
  if (!QNetworkInformation::loadDefaultBackend()) {
    qCInfo(lcBridge, "这台机器没有 QNetworkInformation 后端，网络变化靠心跳兜底");
    return;
  }
  QNetworkInformation* info = QNetworkInformation::instance();
  connect(info, &QNetworkInformation::reachabilityChanged, this,
          [this](QNetworkInformation::Reachability reachability) {
            if (engine_ && reachability == QNetworkInformation::Reachability::Online) {
              engine_->notifyNetworkChanged();
            }
          });
  connect(info, &QNetworkInformation::transportMediumChanged, this,
          [this](QNetworkInformation::TransportMedium) {
            if (engine_) engine_->notifyNetworkChanged();
          });
}

EngineBridge::~EngineBridge() {
  // 顺序要紧：先摘观察者，再销毁引擎。destroy 会阻塞到回调静默，
  // 但那之前若还挂着观察者，正在飞的那一条回调会打到一个正在析构的 QObject 上。
  if (engine_) engine_->setObserver(nullptr);
}

QString EngineBridge::deviceId() {
  // 协议要求：≤64、同一 uid 下唯一、**跨重启稳定**。所以生成一次就存下来，
  // 每次启动都换一个的话，服务端会把同一台机器当成无数台新设备。
  QSettings settings;
  QString id = settings.value(QStringLiteral("deviceId")).toString();
  if (id.isEmpty()) {
    id = QStringLiteral("desktop-") +
         QUuid::createUuid().toString(QUuid::Id128).left(12);
    settings.setValue(QStringLiteral("deviceId"), id);
  }
  return id;
}

QString EngineBridge::wsUrlFromHttpBase(const QString& httpBase) {
  QUrl url(httpBase.trimmed());
  if (url.scheme().isEmpty()) url.setScheme(QStringLiteral("http"));
  url.setScheme(url.scheme() == QLatin1String("https") ? QStringLiteral("wss")
                                                       : QStringLiteral("ws"));
  url.setPath(QStringLiteral("/v1/ws"));
  url.setQuery(QString());
  return url.toString();
}

QString EngineBridge::versionString() {
  return QString::fromLatin1(imrtc_v1_version());
}

bool EngineBridge::verboseLog() {
  return QSettings().value(QLatin1String(kVerboseLogKey), false).toBool();
}

void EngineBridge::setVerboseLog(bool on) {
  QSettings().setValue(QLatin1String(kVerboseLogKey), on);
  applyLogLevel(on);
}

void EngineBridge::applyLogLevel(bool verbose) {
  imrtc_v1_set_log_level(verbose ? IMRTC_V1_LOG_DEBUG : IMRTC_V1_LOG_INFO);
}

void EngineBridge::fetchDemoToken(const QString& httpBase, const QString& username) {
  QUrl url(httpBase.trimmed());
  if (url.scheme().isEmpty()) url.setScheme(QStringLiteral("http"));
  url.setPath(QStringLiteral("/v1/demo/login"));

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

  QJsonObject body;
  body[QStringLiteral("username")] = username;

  QNetworkReply* reply = http_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      emit tokenFailed(reply->errorString());
      return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    const QString token = document.object().value(QStringLiteral("token")).toString();
    if (token.isEmpty()) {
      emit tokenFailed(tr("服务端没有返回 token（免密登录只在开发构建可用）"));
      return;
    }
    emit tokenReady(token);
  });
}

void EngineBridge::connectToServer(const QString& wsUrl, const QString& token) {
  disconnectFromServer();
  token_ = token;

  engine_ = std::make_unique<imrtc::capi::Engine>(
      wsUrl.toStdString(), deviceId().toStdString(),
      "desktop-qt-demo/" + std::string(imrtc_v1_version()));
  if (!engine_->valid()) {
    const imrtc::capi::Error error = engine_->lastError();
    engine_.reset();
    emit engineError(error.code(), QString::fromLatin1(error.name()),
                     QStringLiteral("engine_create"));
    return;
  }
  engine_->setObserver(this);
  ticker_->start();
  engine_->login(token.toStdString());
}

void EngineBridge::disconnectFromServer() {
  token_.clear();
  if (!engine_) return;
  ticker_->stop();
  engine_->logout();
  // 让 logout 那一帧发出去、并把本地合成的 onCallEnd 抛完，再拆。
  engine_->tick();
  engine_->setObserver(nullptr);
  engine_.reset();  // ~Engine → imrtc_v1_engine_destroy，阻塞到回调静默
}

void EngineBridge::createMeetingRoom(const QString& httpBase) {
  QUrl url(httpBase.trimmed());
  if (url.scheme().isEmpty()) url.setScheme(QStringLiteral("http"));
  url.setPath(QStringLiteral("/v1/rooms"));

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setRawHeader("Authorization", ("Bearer " + token_).toUtf8());

  QNetworkReply* reply =
      http_->post(request, QByteArrayLiteral(R"({"kind":"meeting"})"));
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      emit restFailed(tr("建会议房失败：%1").arg(reply->errorString()));
      return;
    }
    const QString roomId = QJsonDocument::fromJson(reply->readAll())
                               .object()
                               .value(QStringLiteral("room_id"))
                               .toString();
    if (roomId.isEmpty()) {
      emit restFailed(tr("建会议房失败：服务端没有返回 room_id"));
      return;
    }
    qCInfo(lcBridge, "meetingRoomCreated %s", qUtf8Printable(roomId));
    emit meetingRoomCreated(roomId);
  });
}

void EngineBridge::fetchRoomToken(const QString& httpBase, const QString& roomId) {
  QUrl url(httpBase.trimmed());
  if (url.scheme().isEmpty()) url.setScheme(QStringLiteral("http"));
  url.setPath(QStringLiteral("/v1/rooms/%1/tokens").arg(roomId));

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setRawHeader("Authorization", ("Bearer " + token_).toUtf8());

  QJsonObject body;
  body[QStringLiteral("device_id")] = deviceId();

  QNetworkReply* reply =
      http_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  connect(reply, &QNetworkReply::finished, this, [this, reply, roomId] {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      emit restFailed(tr("换进房票失败：%1").arg(reply->errorString()));
      return;
    }
    const QString roomToken = QJsonDocument::fromJson(reply->readAll())
                                  .object()
                                  .value(QStringLiteral("room_token"))
                                  .toString();
    if (roomToken.isEmpty()) {
      emit restFailed(tr("换进房票失败：服务端没有返回 room_token"));
      return;
    }
    // 票是凭据，只打前 6 位 + 长度（CONVENTIONS §8）。
    qCInfo(lcBridge, "roomTokenReady room=%s token=%s… len=%d", qUtf8Printable(roomId),
           qUtf8Printable(roomToken.left(6)), static_cast<int>(roomToken.size()));
    emit roomTokenReady(roomId, roomToken);
  });
}

void EngineBridge::assertOnGuiThread(const char* where) const {
  // 这不是防御性编程，是**在核一条设计前提**：本 Demo 在 GUI 线程上 tick，
  // 所以回调也落在 GUI 线程上，界面才敢直接改。哪天有人把 tick 挪走，
  // 应该在这里当场炸，而不是让 QWidget 在别的线程上被改出随机崩溃。
  Q_ASSERT_X(QThread::currentThread() == QCoreApplication::instance()->thread(), where,
             "回调没有落在 GUI 线程上——tick() 被挪到别的线程了？");
  Q_UNUSED(where);
}

/* ---- Observer：只做翻译，不做判断 ---- */

void EngineBridge::onConnected(const std::string& sessionId, bool resumed) {
  assertOnGuiThread("onConnected");
  qCInfo(lcBridge, "connected, resumed = %d", resumed ? 1 : 0);
  emit connected(qs(sessionId), resumed);
}

void EngineBridge::onDisconnected(std::int32_t code, bool willReconnect) {
  assertOnGuiThread("onDisconnected");
  // 关闭码与 willReconnect 先落日志：demo/docs/ops/silent-failure/desktop.md 记的
  // P1「onDisconnected 无参把关闭码/willReconnect 全抹平」还没排到界面这一半，
  // 这里先把数据留住，MainWindow 仍然按老样子统一显示「正在重连…」。
  qCInfo(lcBridge, "onDisconnected code=%d willReconnect=%d", code, willReconnect ? 1 : 0);
  emit disconnected();
}

void EngineBridge::onKickedOut(imrtc_v1_kicked_reason reason) {
  assertOnGuiThread("onKickedOut");
  emit kickedOut(reason);
}

void EngineBridge::onError(std::int32_t code, const std::string& name,
                           const std::string& forType) {
  assertOnGuiThread("onError");
  qCWarning(lcBridge, "error %d %s for %s", code, name.c_str(), forType.c_str());
  emit engineError(code, qs(name), qs(forType));
}

void EngineBridge::onCallReceived(const std::string& callId, const std::string& caller,
                                  const std::vector<std::string>& calleeIds,
                                  const std::string& mediaType, bool isGroup,
                                  const std::string& chatGroupId, const std::string& userData,
                                  const std::string& inviter,
                                  const std::vector<std::string>& joinedIds) {
  assertOnGuiThread("onCallReceived");
  // 通话生命周期的三条日志：联调时「到底谁没收到」全靠它们定位。
  qCInfo(lcBridge, "onCallReceived call=%s caller=%s inviter=%s media=%s group=%d chat_group=%s",
         callId.c_str(), caller.c_str(), inviter.c_str(), mediaType.c_str(), isGroup ? 1 : 0,
         chatGroupId.c_str());
  QStringList ids;
  ids.reserve(static_cast<qsizetype>(calleeIds.size()));
  for (const std::string& id : calleeIds) ids << qs(id);
  emit callReceived(qs(callId), qs(caller), ids, qs(mediaType), isGroup, qs(chatGroupId),
                    qs(userData), qs(inviter));
}

void EngineBridge::onCallBegin(const std::string& callId, const std::string& roomId,
                               const std::string& role, const std::string& caller,
                               const std::string& chatGroupId, const std::string& userData) {
  assertOnGuiThread("onCallBegin");
  qCInfo(lcBridge, "onCallBegin call=%s room=%s role=%s caller=%s chat_group=%s", callId.c_str(),
         roomId.c_str(), role.c_str(), caller.c_str(), chatGroupId.c_str());
  emit callBegan(qs(callId), qs(roomId), qs(role), qs(caller), qs(chatGroupId), qs(userData));
}

void EngineBridge::onCallEnd(const std::string& callId, const std::string& reason,
                             std::int64_t durationSec, const std::string& endedBy,
                             imrtc_v1_end_reason reasonCode) {
  assertOnGuiThread("onCallEnd");
  qCInfo(lcBridge, "onCallEnd call=%s reason=%s reason_code=%d duration=%lld", callId.c_str(),
         reason.c_str(), static_cast<int>(reasonCode), static_cast<long long>(durationSec));
  emit callEnded(qs(callId), qs(reason), durationSec, qs(endedBy));
}

void EngineBridge::onCallMissed(const std::string& callId, const std::string& caller,
                                const std::string& reason) {
  assertOnGuiThread("onCallMissed");
  emit callMissed(qs(callId), qs(caller), qs(reason));
}

void EngineBridge::onCallCancelled(const std::string& uid) {
  assertOnGuiThread("onCallCancelled");
  emit callCancelled(qs(uid));
}

void EngineBridge::onCallRejected(const std::string& uid) {
  assertOnGuiThread("onCallRejected");
  emit callRejected(qs(uid));
}

void EngineBridge::onCallBusy(const std::string& uid) {
  assertOnGuiThread("onCallBusy");
  emit callBusy(qs(uid));
}

void EngineBridge::onCallNoAnswer(const std::string& uid) {
  assertOnGuiThread("onCallNoAnswer");
  emit callNoAnswer(qs(uid));
}

void EngineBridge::onHandledOnOtherDevice(const std::string& callId, const std::string& action) {
  assertOnGuiThread("onHandledOnOtherDevice");
  emit handledOnOtherDevice(qs(callId), qs(action));
}

void EngineBridge::onUserEnter(const std::string& uid) {
  assertOnGuiThread("onUserEnter");
  qCInfo(lcBridge, "onUserEnter %s", uid.c_str());
  emit userEntered(qs(uid));
}

void EngineBridge::onUserLeave(const std::string& uid) {
  assertOnGuiThread("onUserLeave");
  qCInfo(lcBridge, "onUserLeave %s", uid.c_str());
  emit userLeft(qs(uid));
}

void EngineBridge::onUserAccept(const std::string& uid) {
  assertOnGuiThread("onUserAccept");
  qCInfo(lcBridge, "onUserAccept %s", uid.c_str());
  emit userAccepted(qs(uid));
}

void EngineBridge::onUserReject(const std::string& uid) {
  assertOnGuiThread("onUserReject");
  qCInfo(lcBridge, "onUserReject %s", uid.c_str());
  emit userRejected(qs(uid));
}

void EngineBridge::onUserNoResponse(const std::string& uid) {
  assertOnGuiThread("onUserNoResponse");
  qCInfo(lcBridge, "onUserNoResponse %s", uid.c_str());
  emit userNoResponse(qs(uid));
}

void EngineBridge::onUserAudioAvailable(const std::string& uid, bool available) {
  assertOnGuiThread("onUserAudioAvailable");
  emit userAudioAvailable(qs(uid), available);
}

void EngineBridge::onUserVideoAvailable(const std::string& uid, bool available) {
  assertOnGuiThread("onUserVideoAvailable");
  emit userVideoAvailable(qs(uid), available);
}

void EngineBridge::onActiveSpeakers(const std::vector<imrtc::capi::Speaker>& speakers) {
  assertOnGuiThread("onActiveSpeakers");
  QList<SpeakerInfo> out;
  out.reserve(static_cast<qsizetype>(speakers.size()));
  for (const imrtc::capi::Speaker& speaker : speakers) {
    out.append(SpeakerInfo{qs(speaker.uid), static_cast<int>(speaker.volume)});
  }
  emit activeSpeakers(out);
}

void EngineBridge::onNetworkQuality(const std::vector<imrtc::capi::Quality>& entries) {
  assertOnGuiThread("onNetworkQuality");
  QList<QualityInfo> out;
  out.reserve(static_cast<qsizetype>(entries.size()));
  for (const imrtc::capi::Quality& entry : entries) {
    out.append(QualityInfo{qs(entry.uid), static_cast<int>(entry.level)});
  }
  emit networkQuality(out);
}

void EngineBridge::onRoomJoined(const std::string& roomId) {
  assertOnGuiThread("onRoomJoined");
  qCInfo(lcBridge, "onRoomJoined %s", roomId.c_str());
  emit roomJoined(qs(roomId));
}

void EngineBridge::onRoomLeft(const std::string& roomId) {
  assertOnGuiThread("onRoomLeft");
  qCInfo(lcBridge, "onRoomLeft %s", roomId.c_str());
  emit roomLeft(qs(roomId));
}

void EngineBridge::onRoomClosed(const std::string& roomId, const std::string& reason) {
  assertOnGuiThread("onRoomClosed");
  emit roomClosed(qs(roomId), qs(reason));
}

/* ---- 转发动作 ---- */

qint32 EngineBridge::fetchCallHistory(
    int limit, qint64 cursor,
    std::function<void(imrtc::capi::Result<imrtc::capi::CallHistoryPage>)> done) {
  if (!engine_) return IMRTC_V1_ERR_INVALID_STATE;
  return engine_->fetchCallHistory(limit, cursor, std::move(done)).code();
}

qint32 EngineBridge::startCall(const QStringList& calleeIds, const QString& mediaType,
                               bool isGroup) {
  if (!engine_) return IMRTC_V1_ERR_INVALID_STATE;
  return engine_
      ->call(stdStrings(calleeIds), mediaType.toStdString(), isGroup,
             [this](imrtc::capi::Result<std::string> result) {
               if (result.ok()) {
                 qCInfo(lcBridge, "call.invite 受理 call=%s", result.value.c_str());
                 return;
               }
               // 界面收起靠随后已经抛过的 onCallEnd(error)，这里只出提示。
               qCWarning(lcBridge, "拨号失败 %d %s", result.code, result.name.c_str());
               emit engineError(result.code, qs(result.name), QStringLiteral("call"));
             })
      .code();
}

qint32 EngineBridge::accept() {
  return engine_ ? engine_->accept(reportFailure("accept")).code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::reject() {
  return engine_ ? engine_->reject(logFailure("reject")).code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::cancel() {
  return engine_ ? engine_->cancel(logFailure("cancel")).code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::hangup() {
  return engine_ ? engine_->hangup(logFailure("hangup")).code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::inviteMore(const QStringList& calleeIds) {
  return engine_ ? engine_->inviteMore(stdStrings(calleeIds), reportFailure("inviteMore")).code()
                 : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::joinRoom(const QString& roomId, const QString& roomToken) {
  return engine_ ? engine_->joinRoom(roomId.toStdString(), roomToken.toStdString(),
                                     reportFailure("joinRoom")).code()
                 : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::leaveRoom() {
  return engine_ ? engine_->leaveRoom(logFailure("leaveRoom")).code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::attachView(const QString& uid, void* nativeHandle) {
  if (!engine_) return IMRTC_V1_ERR_INVALID_STATE;
  qCInfo(lcBridge, "attachView uid=%s handle=%p", qUtf8Printable(uid), nativeHandle);
  return engine_->attachView(uid.toStdString(), nativeHandle).code();
}

qint32 EngineBridge::attachLocalView(void* nativeHandle) {
  if (!engine_) return IMRTC_V1_ERR_INVALID_STATE;
  qCInfo(lcBridge, "attachLocalView handle=%p", nativeHandle);
  return engine_->attachLocalView(nativeHandle).code();
}

qint32 EngineBridge::setRemoteLayer(const QString& uid, const QString& layer) {
  if (!engine_) return IMRTC_V1_ERR_INVALID_STATE;
  qCInfo(lcBridge, "setRemoteLayer uid=%s layer=%s", qUtf8Printable(uid), qUtf8Printable(layer));
  return engine_->setRemoteLayer(uid.toStdString(), layer.toStdString()).code();
}

qint32 EngineBridge::openMic() {
  return engine_ ? engine_->openMicrophone().code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::closeMic() {
  return engine_ ? engine_->closeMicrophone().code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::openCamera() {
  return engine_ ? engine_->openCamera().code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::closeCamera() {
  return engine_ ? engine_->closeCamera().code() : IMRTC_V1_ERR_INVALID_STATE;
}

imrtc_v1_call_state EngineBridge::callState() {
  return engine_ ? engine_->callState() : IMRTC_V1_CALL_IDLE;
}

imrtc_v1_room_state EngineBridge::roomState() {
  return engine_ ? engine_->roomState() : IMRTC_V1_ROOM_IDLE;
}
