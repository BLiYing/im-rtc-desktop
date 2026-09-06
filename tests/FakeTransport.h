#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "TestHarness.h"
#include "imrtc/Envelope.h"
#include "imrtc/Json.h"
#include "imrtc/Transport.h"

namespace imtest {

/**
 * 一条假的连接。
 *
 * Connection 每次 connect 都会向工厂要一条新的，所以测试要能拿到**当前那一条**——
 * 用 shared_ptr 让 FakeNet 与 Transport 各持一份，Transport 被 Connection 析构掉
 * 之后记录还在，正好用来断言「上一条是被什么码关掉的」。
 */
struct FakeSocket {
  std::string url;
  bool open = false;
  bool closed = false;
  int closeCode = 0;
  std::string closeReason;
  std::vector<std::string> sent;
  imrtc::TransportListener* listener = nullptr;
};

class FakeTransport : public imrtc::Transport {
public:
  explicit FakeTransport(std::shared_ptr<FakeSocket> socket) : socket_(std::move(socket)) {}

  void setListener(imrtc::TransportListener* listener) override { socket_->listener = listener; }
  void connect(const std::string& url) override { socket_->url = url; }
  void send(const std::string& raw) override {
    if (socket_->open) socket_->sent.push_back(raw);
  }
  void close(int code, const std::string& reason) override {
    if (socket_->closed) return;
    socket_->closed = true;
    socket_->open = false;
    socket_->closeCode = code;
    socket_->closeReason = reason;
    if (socket_->listener != nullptr) socket_->listener->onTransportClosed(code, reason);
  }
  bool isOpen() const override { return socket_->open; }

private:
  std::shared_ptr<FakeSocket> socket_;
};

/** FakeNet 是测试这一侧的把手：造连接、放行握手、投帧、模拟断开。 */
class FakeNet {
public:
  imrtc::TransportFactory factory() {
    return [this]() -> std::unique_ptr<imrtc::Transport> {
      auto socket = std::make_shared<FakeSocket>();
      sockets_.push_back(socket);
      return std::unique_ptr<imrtc::Transport>(new FakeTransport(socket));
    };
  }

  std::size_t socketCount() const { return sockets_.size(); }

  FakeSocket& current() {
    CHECK_TRUE(!sockets_.empty(), "还没有任何连接被创建");
    return *sockets_.back();
  }

  FakeSocket& at(std::size_t index) {
    CHECK_TRUE(index < sockets_.size(), "连接下标越界");
    return *sockets_[index];
  }

  /** open 放行底层连接。 */
  void open() {
    FakeSocket& socket = current();
    socket.open = true;
    if (socket.listener != nullptr) socket.listener->onTransportOpen();
  }

  /** deliver 从服务端投一帧下来。 */
  void deliver(const std::string& raw) {
    FakeSocket& socket = current();
    if (socket.listener != nullptr) socket.listener->onTransportMessage(raw);
  }

  /** remoteClose 模拟服务端/网络把连接关掉。 */
  void remoteClose(int code, const std::string& reason = "") {
    FakeSocket& socket = current();
    if (socket.closed) return;
    socket.closed = true;
    socket.open = false;
    socket.closeCode = code;
    socket.closeReason = reason;
    if (socket.listener != nullptr) socket.listener->onTransportClosed(code, reason);
  }

private:
  std::vector<std::shared_ptr<FakeSocket>> sockets_;
};

/** parseSent 把已发出的第 index 帧解回来。 */
inline imrtc::Json parseSent(const FakeSocket& socket, std::size_t index) {
  CHECK_TRUE(index < socket.sent.size(),
             "想看第 " + std::to_string(index) + " 帧，但只发出了 " +
                 std::to_string(socket.sent.size()) + " 帧");
  return imrtc::Json::parse(socket.sent[index]);
}

/** lastSent 把最后一帧解回来。 */
inline imrtc::Json lastSent(const FakeSocket& socket) {
  CHECK_TRUE(!socket.sent.empty(), "一帧都没发出去");
  return imrtc::Json::parse(socket.sent.back());
}

/** field 从解出来的帧里取一个字符串字段（顶层或 data 里）。 */
inline std::string field(const imrtc::Json& frame, const std::string& key) {
  const imrtc::Json* value = frame.find(key);
  if (value == nullptr) {
    const imrtc::Json* data = frame.find("data");
    if (data != nullptr) value = data->find(key);
  }
  return value != nullptr && value->isString() ? value->asString() : std::string();
}

/** replyFrame 拼一帧服务端应答。 */
inline std::string replyFrame(const std::string& type, const std::string& reqId,
                              imrtc::Json data) {
  return imrtc::encodeEnvelope(type, reqId, std::move(data), 1756876800000);
}

/** helloOkData 是一份典型的 sys.hello.ok（§1.2 的例子）。 */
inline imrtc::Json helloOkData(const std::string& sessionId, bool resumed,
                               std::int64_t pingIntervalSec = 15) {
  imrtc::Json limits = imrtc::Json::makeObject();
  limits.set("max_frame_bytes", imrtc::Json::make(std::int64_t{65536}));
  limits.set("max_callees", imrtc::Json::make(std::int64_t{8}));
  limits.set("max_room_participants", imrtc::Json::make(std::int64_t{9}));
  limits.set("max_user_data_bytes", imrtc::Json::make(std::int64_t{4096}));
  limits.set("ring_timeout_sec_default", imrtc::Json::make(std::int64_t{30}));

  imrtc::Json data = imrtc::Json::makeObject();
  data.set("uid", imrtc::Json::make("alice"));
  data.set("device_id", imrtc::Json::make("mac-8f3a"));
  data.set("session_id", imrtc::Json::make(sessionId));
  data.set("server_time_ms", imrtc::Json::make(std::int64_t{1756876800200}));
  data.set("resumed", imrtc::Json::make(resumed));
  data.set("ping_interval_sec", imrtc::Json::make(pingIntervalSec));
  data.set("limits", std::move(limits));
  return data;
}

}  // namespace imtest
