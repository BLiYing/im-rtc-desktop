#include "EngineBridge.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
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

QString qs(const std::string& value) { return QString::fromStdString(value); }

std::vector<std::string> stdStrings(const QStringList& values) {
  std::vector<std::string> out;
  out.reserve(static_cast<std::size_t>(values.size()));
  for (const QString& value : values) out.push_back(value.toStdString());
  return out;
}

}  // namespace

EngineBridge::EngineBridge(QObject* parent) : QObject(parent) {
  http_ = new QNetworkAccessManager(this);

  ticker_ = new QTimer(this);
  ticker_->setInterval(kTickIntervalMs);
  connect(ticker_, &QTimer::timeout, this, [this] {
    if (engine_) engine_->tick();
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

  engine_ = std::make_unique<imrtc::capi::Engine>(wsUrl.toStdString(),
                                                  deviceId().toStdString(),
                                                  "desktop-qt-demo/0.1.0");
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

void EngineBridge::onDisconnected() {
  assertOnGuiThread("onDisconnected");
  emit disconnected();
}

void EngineBridge::onKickedOut() {
  assertOnGuiThread("onKickedOut");
  emit kickedOut();
}

void EngineBridge::onError(std::int32_t code, const std::string& name,
                           const std::string& forType) {
  assertOnGuiThread("onError");
  qCWarning(lcBridge, "error %d %s for %s", code, name.c_str(), forType.c_str());
  emit engineError(code, qs(name), qs(forType));
}

void EngineBridge::onCallReceived(const std::string& callId, const std::string& caller,
                                  const std::vector<std::string>& calleeIds,
                                  const std::string& mediaType, bool isGroup) {
  assertOnGuiThread("onCallReceived");
  // 通话生命周期的三条日志：联调时「到底谁没收到」全靠它们定位。
  qCInfo(lcBridge, "onCallReceived call=%s caller=%s media=%s group=%d", callId.c_str(),
         caller.c_str(), mediaType.c_str(), isGroup ? 1 : 0);
  QStringList ids;
  ids.reserve(static_cast<qsizetype>(calleeIds.size()));
  for (const std::string& id : calleeIds) ids << qs(id);
  emit callReceived(qs(callId), qs(caller), ids, qs(mediaType), isGroup);
}

void EngineBridge::onCallBegin(const std::string& callId, const std::string& roomId,
                               const std::string& role) {
  assertOnGuiThread("onCallBegin");
  qCInfo(lcBridge, "onCallBegin call=%s room=%s role=%s", callId.c_str(), roomId.c_str(),
         role.c_str());
  emit callBegan(qs(callId), qs(roomId), qs(role));
}

void EngineBridge::onCallEnd(const std::string& callId, const std::string& reason,
                             std::int64_t durationSec, const std::string& endedBy) {
  assertOnGuiThread("onCallEnd");
  qCInfo(lcBridge, "onCallEnd call=%s reason=%s duration=%lld", callId.c_str(), reason.c_str(),
         static_cast<long long>(durationSec));
  emit callEnded(qs(callId), qs(reason), durationSec, qs(endedBy));
}

void EngineBridge::onCallMissed(const std::string& callId, const std::string& caller,
                                const std::string& reason) {
  assertOnGuiThread("onCallMissed");
  emit callMissed(qs(callId), qs(caller), qs(reason));
}

void EngineBridge::onCallCancelled(const std::string& by) {
  assertOnGuiThread("onCallCancelled");
  emit callCancelled(qs(by));
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

qint32 EngineBridge::startCall(const QStringList& calleeIds, const QString& mediaType,
                               bool isGroup) {
  if (!engine_) return IMRTC_V1_ERR_INVALID_STATE;
  return engine_->call(stdStrings(calleeIds), mediaType.toStdString(), isGroup).code();
}

qint32 EngineBridge::accept() {
  return engine_ ? engine_->accept().code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::reject() {
  return engine_ ? engine_->reject().code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::cancel() {
  return engine_ ? engine_->cancel().code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::hangup() {
  return engine_ ? engine_->hangup().code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::inviteMore(const QStringList& calleeIds) {
  return engine_ ? engine_->inviteMore(stdStrings(calleeIds)).code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::joinRoom(const QString& roomId, const QString& roomToken) {
  return engine_ ? engine_->joinRoom(roomId.toStdString(), roomToken.toStdString()).code()
                 : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::leaveRoom() {
  return engine_ ? engine_->leaveRoom().code() : IMRTC_V1_ERR_INVALID_STATE;
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
  return engine_ ? engine_->openMic().code() : IMRTC_V1_ERR_INVALID_STATE;
}
qint32 EngineBridge::closeMic() {
  return engine_ ? engine_->closeMic().code() : IMRTC_V1_ERR_INVALID_STATE;
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
