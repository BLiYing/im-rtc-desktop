#include "imrtc/IxTransport.h"

#include <mutex>
#include <utility>

#include "ixwebsocket/IXNetSystem.h"
#include "ixwebsocket/IXWebSocket.h"

namespace imrtc {
namespace {

/**
 * ensureNetSystem 在进程内初始化一次网络子系统。
 *
 * Windows 上这是 WSAStartup，不调就一个字节都发不出去；其它平台是空操作。
 * 用函数内静态量而不是全局对象：静态初始化顺序在跨编译单元时没有保证。
 */
void ensureNetSystem() {
  static const bool kInitialized = []() {
    ix::initNetSystem();
    return true;
  }();
  (void)kInitialized;
}

}  // namespace

IxTransport::IxTransport() : ws_(new ix::WebSocket()) {
  ensureNetSystem();

  // 见头文件「三件必须做对的事」之一：重连策略在 Connection 里，这里必须关掉。
  ws_->disableAutomaticReconnection();

  ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
    switch (message->type) {
      case ix::WebSocketMessageType::Open:
        enqueue(Event{Event::Kind::Open, "", 0});
        break;
      case ix::WebSocketMessageType::Message:
        // 协议帧一律是 UTF-8 文本（§2.4）。二进制帧是对端的实现 bug，丢掉。
        if (!message->binary) enqueue(Event{Event::Kind::Message, message->str, 0});
        break;
      case ix::WebSocketMessageType::Close:
        enqueue(Event{Event::Kind::Closed, message->closeInfo.reason,
                      static_cast<int>(message->closeInfo.code)});
        break;
      case ix::WebSocketMessageType::Error:
        // 连不上（DNS / TLS / 握手失败）没有关闭码。按「服务端不可达」上报，
        // 让 Connection 那套关闭码规则统一决定重连——它对 1001 的处置正是重连。
        enqueue(Event{Event::Kind::Closed, message->errorInfo.reason, closecode::kGoingAway});
        break;
      case ix::WebSocketMessageType::Ping:
      case ix::WebSocketMessageType::Pong:
      case ix::WebSocketMessageType::Fragment:
        // WS 层的 ping/pong 与我们无关：协议心跳是业务帧 sys.ping（§1.3）。
        break;
    }
  });
}

IxTransport::~IxTransport() {
  // 先停后台线程再让成员析构。stop() 会 join，返回之后不会再有回调打进来。
  if (ws_) ws_->stop();
  listener_ = nullptr;
}

void IxTransport::setListener(TransportListener* listener) { listener_ = listener; }

void IxTransport::connect(const std::string& url) {
  ws_->setUrl(url);
  ws_->start();
}

void IxTransport::send(const std::string& raw) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!open_) return;
  }
  /*
    **在锁外发**（CONVENTIONS §6「不在锁内做 IO」）。

    sendText 要做 socket IO，而 IX 的后台线程收到帧时要拿同一把锁去 enqueue。
    在锁内发的话，一次慢发送会把整条收帧路径一起卡住——收和发的延迟被焊死在一起，
    而这条锁本来只是护着那个投递队列。

    sendText 会校验 UTF-8；帧是我们自己序列化的 JSON，校验失败等于本端有 bug。
  */
  ws_->sendText(raw);
}

void IxTransport::close(int code, const std::string& reason) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (closing_) return;
    closing_ = true;
    open_ = false;
  }
  // stop() 会 join 后台线程，**绝不能在回调线程里调**——poll() 跑在宿主线程上，
  // 而 close() 只从宿主线程来（Connection 的方法都在同一个线程），所以安全。
  ws_->stop(static_cast<std::uint16_t>(code), reason);
}

bool IxTransport::isOpen() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return open_;
}

void IxTransport::enqueue(Event event) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (event.kind == Event::Kind::Open) open_ = true;
  if (event.kind == Event::Kind::Closed) open_ = false;
  /*
    满了就**丢**，不阻塞（CONVENTIONS §6）——阻塞在这里等于把 IX 的后台线程钉死。

    Open / Closed 永远收下：它们是状态转移，丢了比丢一帧数据严重得多。
    丢掉的数据帧记一笔，由 poll() 去按断线处置（见 overflowed_）。
  */
  if (event.kind == Event::Kind::Message && queue_.size() >= kMaxQueuedEvents) {
    overflowed_ = true;
    return;
  }
  queue_.push_back(std::move(event));
}

void IxTransport::poll() {
  bool overflowed = false;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    overflowed = overflowed_;
    if (overflowed) {
      overflowed_ = false;
      queue_.clear();
      open_ = false;
      closing_ = true;
    }
  }
  if (overflowed) {
    /*
      **丢过帧就不能假装没事**：本地状态已经和服务端对不上了，接着把剩下的帧放出去
      只会在错位的状态上继续叠。按断线处置——Connection 那套关闭码规则会失败掉全部
      在途请求、停心跳、退避重连，再用一次 `sys.hello` 把状态整个重来一遍。
    */
    ws_->stop(static_cast<std::uint16_t>(closecode::kGoingAway), "event queue overflow");
    if (listener_ != nullptr) {
      listener_->onTransportClosed(closecode::kGoingAway, "event queue overflow");
    }
    return;
  }

  while (true) {
    Event event;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (queue_.empty()) return;
      event = std::move(queue_.front());
      queue_.pop_front();
    }
    // **在锁外回调**：listener 会在回调里 send / close，那些又要拿同一把锁。
    if (listener_ == nullptr) continue;
    switch (event.kind) {
      case Event::Kind::Open: listener_->onTransportOpen(); break;
      case Event::Kind::Message: listener_->onTransportMessage(event.text); break;
      case Event::Kind::Closed: listener_->onTransportClosed(event.code, event.text); break;
    }
  }
}

std::size_t IxTransport::queuedEvents() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return queue_.size();
}

}  // namespace imrtc
