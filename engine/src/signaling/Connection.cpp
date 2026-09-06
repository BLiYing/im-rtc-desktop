#include "imrtc/Connection.h"

#include <utility>

#include "imrtc/Errors.h"
#include "imrtc/Frames.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

/** readInt / readBool / readString 从线路形状的 data 里取字段。 */
std::int64_t readInt(const Json& data, const std::string& key) {
  const Json* value = data.find(key);
  return value != nullptr && value->isInt() ? value->asInt() : 0;
}

bool readBool(const Json& data, const std::string& key) {
  const Json* value = data.find(key);
  return value != nullptr && value->isBool() && value->asBool();
}

std::string readString(const Json& data, const std::string& key) {
  const Json* value = data.find(key);
  return value != nullptr && value->isString() ? value->asString() : std::string();
}

HelloOk toHelloOk(const Json& data) {
  HelloOk hello;
  hello.uid = readString(data, "uid");
  hello.deviceId = readString(data, "device_id");
  hello.sessionId = readString(data, "session_id");
  hello.resumed = readBool(data, "resumed");
  hello.pingIntervalSec = readInt(data, "ping_interval_sec");

  const Json* limits = data.find("limits");
  if (limits != nullptr) {
    hello.limits.maxFrameBytes = readInt(*limits, "max_frame_bytes");
    hello.limits.maxCallees = readInt(*limits, "max_callees");
    hello.limits.maxRoomParticipants = readInt(*limits, "max_room_participants");
    hello.limits.maxUserDataBytes = readInt(*limits, "max_user_data_bytes");
    hello.limits.ringTimeoutSecDefault = readInt(*limits, "ring_timeout_sec_default");
  }
  return hello;
}

}  // namespace

const char* connectionStateName(ConnectionState state) {
  switch (state) {
    case ConnectionState::Idle: return "idle";
    case ConnectionState::Connecting: return "connecting";
    case ConnectionState::Connected: return "connected";
    case ConnectionState::Reconnecting: return "reconnecting";
    case ConnectionState::Closed: return "closed";
  }
  return "idle";
}

Connection::Connection(ConnectionOptions options, ConnectionEvents events)
    : options_(std::move(options)),
      events_(std::move(events)),
      pending_(options_.requestTimeoutMs) {}

Connection::~Connection() {
  // **先注销监听再放掉 Transport**：反过来的话，正在飞的回调会打到一个已析构的
  // 对象上——这正是 CONVENTIONS §5 说的「对象先死、回调后到」。
  if (transport_) transport_->setListener(nullptr);
  transport_.reset();
}

void Connection::connect(std::int64_t nowMs) {
  if (state_ == ConnectionState::Connecting || state_ == ConnectionState::Connected) return;
  if (!options_.transportFactory) {
    emitError(codeValue(ErrorCode::Internal), "");
    return;
  }

  nowMs_ = nowMs;
  reconnectAtMs_ = 0;
  // 显式 connect 解掉「彻底放弃」的闩：宿主换了新票再连，是「这次不一样了」的信号。
  reconnectStopped_ = false;
  state_ = sessionId_.empty() ? ConnectionState::Connecting : ConnectionState::Reconnecting;

  // 上一条连接在这里才真正放掉：绝不在它自己的关闭回调里析构它。
  if (transport_) transport_->setListener(nullptr);
  transport_ = options_.transportFactory();
  if (!transport_) {
    state_ = ConnectionState::Closed;
    emitError(codeValue(ErrorCode::NetworkUnreachable), "");
    return;
  }
  transport_->setListener(this);
  transport_->connect(options_.url);
}

void Connection::close() {
  state_ = ConnectionState::Closed;
  reconnectStopped_ = true;
  reconnectAtMs_ = 0;
  heartbeat_.stop();
  pending_.failAll(codeValue(ErrorCode::InvalidState));
  if (transport_) transport_->close(closecode::kNormal, "client logout");
}

void Connection::updateToken(std::string token) {
  options_.token = std::move(token);
  authFailures_ = 0;
}

void Connection::onTransportOpen() { sendHello(nowMs_); }

void Connection::sendHello(std::int64_t nowMs) {
  // 从**已填好默认值的实例**起手（§2.4 规则 2 的发送侧陷阱）：
  // protocol_version 的默认值是 1，从零值起手会发出 0 去换一个 1006。
  Json hello = newFrameData(helloFields());
  hello.set("token", Json::make(options_.token));
  hello.set("device_id", Json::make(options_.deviceId));
  // 首次连接为空串；重连时带上旧的请求恢复（§1.4）。
  hello.set("session_id", Json::make(sessionId_));
  hello.set("sdk", Json::make(options_.sdk));

  dispatch(frame::kHello, hello, nowMs, [this](const RequestResult& result) {
    handleHelloOk(result);
  });
}

void Connection::handleHelloOk(const RequestResult& result) {
  if (!result.ok) {
    // 握手失败不在这里重连：随后必有一条 close（服务端 100ms 内断开，§1.2），
    // 由 onTransportClosed 统一按关闭码决定重连与否。两处都排会让退避档一次涨两级。
    emitError(result.errorCode, frame::kHello);
    return;
  }

  const HelloOk hello = toHelloOk(result.data);
  sessionId_ = hello.sessionId;
  state_ = ConnectionState::Connected;
  authFailures_ = 0;
  reconnectAttempt_ = 0;
  reconnectStopped_ = false;
  heartbeat_.start(hello.pingIntervalSec, nowMs_);
  if (events_.onConnected) events_.onConnected(hello);
}

bool Connection::request(const std::string& type, const Json& data, std::int64_t nowMs,
                         ResponseHandler handler) {
  if (state_ != ConnectionState::Connected) return false;
  return dispatch(type, data, nowMs, std::move(handler));
}

bool Connection::dispatch(const std::string& type, const Json& data, std::int64_t nowMs,
                          ResponseHandler handler) {
  if (!transport_ || !transport_->isOpen()) return false;
  nowMs_ = nowMs;

  const FrameFields* fields = lookupFrame(type);
  const std::string reqId = nextReqId();
  std::string raw;
  try {
    raw = encodeEnvelope(type, reqId, fields == nullptr ? data : decodeFields(*fields, data), nowMs);
  } catch (const RtcError& error) {
    // 编不出来是我们自己的 bug（字段类型错、帧超长）。不发、不登记、直接报错。
    emitError(error.code(), type);
    return false;
  }

  pending_.track(reqId, type, nowMs, std::move(handler));
  transport_->send(raw);
  return true;
}

void Connection::sendFrame(const std::string& type, const std::string& reqId, const Json& data,
                           std::int64_t nowMs) {
  if (!transport_ || !transport_->isOpen()) return;
  nowMs_ = nowMs;
  const FrameFields* fields = lookupFrame(type);
  try {
    transport_->send(
        encodeEnvelope(type, reqId, fields == nullptr ? data : decodeFields(*fields, data), nowMs));
  } catch (const RtcError& error) {
    emitError(error.code(), type);
  }
}

void Connection::onTransportMessage(const std::string& raw) {
  // 收到**任何**帧都算对端活着，不只是 pong（§1.3）。
  heartbeat_.noteFrameReceived();

  Envelope envelope;
  try {
    envelope = decodeEnvelope(raw);
  } catch (const RtcError& error) {
    // 解不开的帧是对端的实现 bug。报给宿主并断开——继续读只会读到更多垃圾。
    emitError(error.code(), "");
    if (transport_) transport_->close(closecode::kBadProtocol, "undecodable frame");
    return;
  }

  if (!envelope.reqId.empty() &&
      pending_.settle(envelope, [this](const Envelope& env) { return decodeData(env); })) {
    return;
  }
  dispatchEvent(envelope);
}

void Connection::dispatchEvent(const Envelope& envelope) {
  if (envelope.type == frame::kError) {
    const Json* code = envelope.data.find("code");
    const std::int32_t value = code != nullptr && code->isInt()
                                   ? static_cast<std::int32_t>(code->asInt())
                                   : codeValue(ErrorCode::Internal);
    if (value == codeValue(ErrorCode::KickedOut) && events_.onKickedOut) events_.onKickedOut();
    emitError(value, readString(envelope.data, "for_type"));
    return;
  }
  if (envelope.type == frame::kPong) {
    // pong 的全部信息就是「对端还活着」，而那件事在进这个函数之前已经记过了
    // （noteFrameReceived）。再往上抛只会给宿主一条每 15 秒一次的噪声事件。
    return;
  }
  if (lookupFrame(envelope.type) == nullptr) {
    // §2.3：客户端收到未知 type **必须静默忽略**——服务端可能比我们新。
    return;
  }
  if (events_.onEvent) events_.onEvent(envelope.type, decodeData(envelope), envelope);
}

Json Connection::decodeData(const Envelope& envelope) const {
  const FrameFields* fields = lookupFrame(envelope.type);
  if (fields == nullptr) return envelope.data;
  // 解一次的作用是三件事：填默认值、枚举兜底、数值钳制。产出仍是**线路形状**——
  // 状态机吃的就是线路形状（它跑的一致性向量正是线路形状）。
  return decodeFields(*fields, envelope.data);
}

void Connection::onTransportClosed(int code, const std::string& reason) {
  heartbeat_.stop();
  pending_.failAll(codeValue(ErrorCode::NetworkUnreachable));

  if (code == closecode::kKickedOut && events_.onKickedOut) events_.onKickedOut();

  /*
    4401 要计数。重连**带的是同一枚 token**，所以协议 §1.5 那句「换新 token 后重连」
    只有配上一个上限才成立——否则一枚废票能自己重试到天荒地老。
    连续 3 次之后抛 onKickedOut，让宿主回登录页重新取票。
    （Web 端实测：服务端重启换了签名密钥，一个没关的标签页重试到第 19 次还在敲，
    日志里全是 token_invalid，把真正的问题淹掉了。）
  */
  bool exhausted = false;
  if (code == closecode::kUnauthorized) {
    ++authFailures_;
    exhausted = authFailures_ >= kMaxAuthFailures;
    if (exhausted) {
      reconnectStopped_ = true;
      if (events_.onKickedOut) events_.onKickedOut();
    }
  }

  const bool willReconnect = !exhausted && !reconnectStopped_ &&
                             state_ != ConnectionState::Closed && shouldReconnect(code);
  if (events_.onDisconnected) events_.onDisconnected(code, reason, willReconnect);

  if (!willReconnect) {
    state_ = ConnectionState::Closed;
    reconnectAtMs_ = 0;
    return;
  }
  state_ = ConnectionState::Reconnecting;
  scheduleReconnect(nowMs_);
}

void Connection::scheduleReconnect(std::int64_t nowMs) {
  // **已经排着一次就什么都不做**（不重排、不进档）。一次失败可能从两条路走到这里，
  // 每次都重排的话退避档一次涨两级，几分钟后就退到几十秒一次，看着像「不重连了」。
  if (reconnectStopped_ || reconnectAtMs_ != 0) return;
  reconnectAtMs_ = nowMs + backoffDelayMs(reconnectAttempt_, options_.random);
  ++reconnectAttempt_;
}

void Connection::tick(std::int64_t nowMs) {
  nowMs_ = nowMs;
  pending_.expire(nowMs);

  switch (heartbeat_.tick(nowMs)) {
    case Heartbeat::Action::SendPing:
      sendFrame(frame::kPing, nextReqId(), Json::makeObject(), nowMs);
      break;
    case Heartbeat::Action::Dead:
      // 判死走的是「主动关掉」，让关闭码的那套规则统一决定重不重连。
      if (transport_) transport_->close(closecode::kGoingAway, "heartbeat timeout");
      break;
    case Heartbeat::Action::None:
      break;
  }

  if (reconnectAtMs_ != 0 && nowMs >= reconnectAtMs_ && !reconnectStopped_) {
    reconnectAtMs_ = 0;
    connect(nowMs);
  }
}

std::string Connection::nextReqId() {
  ++seq_;
  return "d-" + std::to_string(seq_);
}

void Connection::emitError(std::int32_t code, const std::string& forType) {
  if (events_.onError) events_.onError(code, errorName(code), forType);
}

}  // namespace imrtc
