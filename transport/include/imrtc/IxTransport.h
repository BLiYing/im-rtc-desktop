#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "imrtc/Transport.h"

namespace ix {
class WebSocket;
}

namespace imrtc {

/**
 * IxTransport 是 `Transport` 的真实实现，底层用 IXWebSocket（BSD-3-Clause）。
 *
 * # 为什么它不在 engine/ 里
 *
 * engine **零第三方依赖**：一份 CMake + 一个编译器就能编译并跑完全部测试
 * （测试用的是假 Transport）。把唯一需要下载依赖的部分单独成一个目标，
 * 离线机器关掉 `IMRTC_WITH_IX_TRANSPORT` 照样能开发。
 *
 * # 三件必须做对的事
 *
 * 1. **关掉 IXWebSocket 自带的自动重连**。重连策略（退避档、关闭码该不该重连、
 *    4401 的重试上限）在 `Connection` 里，两层重连同时跑会互相打架——
 *    退避档会以两倍速度往上走，而关闭码规则完全失效。
 * 2. **不用它的 WS 层 ping**。协议 §1.3 的心跳是**业务帧** `sys.ping`，
 *    判活条件是「收到对端任何一帧」；WS 层的 ping/pong 满足不了那条，
 *    两套心跳并存只会让排障时分不清是谁在保活。
 * 3. **回调要跨线程投递**。IXWebSocket 在自己的后台线程上回调，而 `Connection`
 *    不是线程安全的。所以这里把事件排进队列，由 `poll()`（`Connection::tick()`
 *    调用）在宿主线程上放出来。
 */
class IxTransport : public Transport {
public:
  IxTransport();
  ~IxTransport() override;

  IxTransport(const IxTransport&) = delete;
  IxTransport& operator=(const IxTransport&) = delete;

  void setListener(TransportListener* listener) override;
  void connect(const std::string& url) override;
  void send(const std::string& raw) override;
  void close(int code, const std::string& reason) override;
  bool isOpen() const override;
  void poll() override;

  /** queuedEvents 是还没投递的事件数，供诊断观察。 */
  std::size_t queuedEvents() const;

private:
  struct Event {
    enum class Kind { Open, Message, Closed };
    Kind kind = Kind::Open;
    std::string text;
    int code = 0;
  };

  void enqueue(Event event);

  /** listener_ 只在宿主线程上读写（setListener / poll 都在那边）。 */
  TransportListener* listener_ = nullptr;
  mutable std::mutex mutex_;
  std::deque<Event> queue_;
  bool open_ = false;
  /** close() 已经发出过，别再重复 stop。 */
  bool closing_ = false;

  /**
   * ws_ **必须是最后一个成员**：成员按声明的逆序析构，于是它先走，
   * 它的析构会 join 掉后台线程，之后才轮到 mutex_ / queue_ 被销毁。
   * 反过来的话，正在飞的回调会打到已销毁的队列上。
   */
  std::unique_ptr<ix::WebSocket> ws_;
};

}  // namespace imrtc
