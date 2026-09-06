#pragma once

#include <functional>
#include <memory>
#include <string>

namespace imrtc {

/**
 * WebSocket 关闭码（RTC_PROTOCOL.md §1.5）。
 */
namespace closecode {
/** 正常关闭（客户端主动 logout）。不重连。 */
constexpr int kNormal = 1000;
/** 服务端下线/重启。立即重连（退避从第 1 档起）。 */
constexpr int kGoingAway = 1001;
/** 信封非法 / 帧超长 / 协议版本不支持。**不重连**，属实现 bug。 */
constexpr int kBadProtocol = 4400;
/** 未鉴权 / 鉴权超时 / token 无效或过期。换新 token 后重连。 */
constexpr int kUnauthorized = 4401;
/** 被踢（同 uid 同 device_id 在别处登录）。**不重连**。 */
constexpr int kKickedOut = 4403;
/** 频率超限。退避加倍后重连。 */
constexpr int kRateLimited = 4429;
}  // namespace closecode

/**
 * shouldReconnect 按关闭码判断要不要重连。
 *
 * 4400 与 4403 **绝不重连**：前者是我们自己的实现 bug，重连只会再撞一次；
 * 后者是被踢，重连等于跟另一台设备打架。
 */
bool shouldReconnect(int code);

/**
 * TransportListener 是 Transport 的三个出口，由 Connection 实现。
 *
 * **生命周期**：Connection **拥有** Transport，并在自己析构时先把 Transport 放掉，
 * 所以这里用裸指针不违反 CONVENTIONS §5——那条禁的是「观察者活得比被观察者久」，
 * 这里是反过来的所有权方向，且拆除顺序由 Connection 显式保证。
 */
class TransportListener {
public:
  virtual ~TransportListener() = default;

  /** 底层连接已打开，可以发首帧了。 */
  virtual void onTransportOpen() = 0;
  /** 收到一帧文本。 */
  virtual void onTransportMessage(const std::string& raw) = 0;
  /** 连接关闭。`code` 见 closecode；底层自身失败（DNS/TLS）用 kGoingAway。 */
  virtual void onTransportClosed(int code, const std::string& reason) = 0;
};

/**
 * Transport 是 WS 的最小抽象。
 *
 * engine **零 Qt 依赖**，也不该把某个具体 WS 库焊死在信令逻辑里——握手、心跳、
 * 超时、退避这些规则跟「用哪个 socket 库」无关，把它们隔开之后：
 * ① 时序逻辑能在没有网络的机器上被确定性地测；
 * ② 将来换 WS 库（IXWebSocket / Boost.Beast）只动这一层的实现文件。
 */
class Transport {
public:
  virtual ~Transport() = default;

  /** setListener 由 Connection 调用；传 nullptr 表示注销（拆除时必须做）。 */
  virtual void setListener(TransportListener* listener) = 0;
  /** connect 开始连接。结果通过 onTransportOpen / onTransportClosed 回来。 */
  virtual void connect(const std::string& url) = 0;
  /** send 发一帧文本。连接不可用时应当静默丢弃而不是抛。 */
  virtual void send(const std::string& raw) = 0;
  /** close 主动关闭。幂等。 */
  virtual void close(int code, const std::string& reason) = 0;
  /** isOpen 报告底层是不是可写。 */
  virtual bool isOpen() const = 0;

  /**
   * poll 把攒下的底层事件投递给 listener，由 `Connection::tick()` 调用。
   *
   * **真实的 WS 库在自己的线程上回调**（IXWebSocket 就是），而 Connection
   * 不是线程安全的。所以真实实现把事件排进队列，在这里于宿主线程上放出来。
   * 同步的实现（测试用的假 Transport）什么都不用做。
   */
  virtual void poll() {}
};

/** TransportFactory 造一条新连接。测试注入假的，运行期注入真的 WS 实现。 */
using TransportFactory = std::function<std::unique_ptr<Transport>()>;

}  // namespace imrtc
