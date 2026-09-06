#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "TestHarness.h"
#include "imrtc/IxTransport.h"
#include "imrtc/Transport.h"

/**
 * IxTransport 的契约测试。
 *
 * **不需要外部网络**：连一个必然被拒的本地端口，验的是「连不上怎么上报」
 * 与「回调只在 poll() 里发生」这两条——后者是整套线程模型的基石，
 * IXWebSocket 在自己的后台线程上回调，而 Connection 不是线程安全的。
 */
namespace {

/** Recorder 记下 listener 收到的事件，并记住它们发生在第几次 poll。 */
class Recorder : public imrtc::TransportListener {
public:
  void onTransportOpen() override { events.push_back("open"); }
  void onTransportMessage(const std::string& raw) override { events.push_back("message:" + raw); }
  void onTransportClosed(int code, const std::string&) override {
    events.push_back("closed:" + std::to_string(code));
  }

  std::vector<std::string> events;
};

/** pumpUntilClosed 反复 poll，直到收到 closed 或超时。返回是否收到了。 */
bool pumpUntilClosed(imrtc::IxTransport& transport, Recorder& recorder, int timeoutMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    transport.poll();
    for (const std::string& event : recorder.events) {
      if (event.rfind("closed:", 0) == 0) return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

}  // namespace

IMRTC_TEST(ixTransportReportsUnreachable, "IxTransport —— 连不上时按「服务端不可达」上报，交给关闭码规则处置") {
  imrtc::IxTransport transport;
  Recorder recorder;
  transport.setListener(&recorder);

  // 127.0.0.1:1 必然被拒（不需要外部网络，也不会等 DNS）。
  transport.connect("ws://127.0.0.1:1");

  CHECK_TRUE(pumpUntilClosed(transport, recorder, 5000), "5 秒内应当报出连接失败");
  CHECK_EQ(recorder.events.back(),
           std::string("closed:") + std::to_string(imrtc::closecode::kGoingAway),
           "连不上按 1001 上报（Connection 对 1001 的处置正是重连）");
  CHECK_EQ(transport.isOpen(), false, "连不上时 isOpen 必须是 false");
}

IMRTC_TEST(ixTransportDefersCallbacks, "IxTransport —— 回调只在 poll() 里发生（IXWebSocket 在别的线程上收帧）") {
  imrtc::IxTransport transport;
  Recorder recorder;
  transport.setListener(&recorder);
  transport.connect("ws://127.0.0.1:1");

  // 给后台线程足够的时间失败掉。这段时间里 listener **一次都不该被碰**——
  // 它跑在宿主线程上，而事件产生在 IXWebSocket 自己的线程上。
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK_EQ(recorder.events.size(), std::size_t{0}, "poll 之前不许有任何回调");
  CHECK_TRUE(transport.queuedEvents() > 0, "事件应当已经排在队列里等着");

  transport.poll();
  CHECK_TRUE(!recorder.events.empty(), "poll 之后才放出来");
  CHECK_EQ(transport.queuedEvents(), std::size_t{0}, "poll 应当把队列排空");
}

IMRTC_TEST(ixTransportDoesNotSelfRetry, "IxTransport —— 关掉了 IXWebSocket 自带的重连（重连策略只能有一份）") {
  imrtc::IxTransport transport;
  Recorder recorder;
  transport.setListener(&recorder);
  transport.connect("ws://127.0.0.1:1");

  /*
    IXWebSocket 默认会自己重连。**必须关掉**：退避档、关闭码该不该重连、
    4401 的重试上限全在 Connection 里，两层重连同时跑会互相打架——
    退避档以两倍速度往上走，而关闭码规则完全失效（4403 被踢了它照样重连）。

    「关掉了」这件事没有 API 能直接问，只能从行为上看：连一个必然被拒的端口，
    1.5 秒里**只该报一次** closed。没关的话它自己会重试好几轮。
  */
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
  int closedCount = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    transport.poll();
    closedCount = 0;
    for (const std::string& event : recorder.events) {
      if (event.rfind("closed:", 0) == 0) ++closedCount;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK_EQ(closedCount, 1, "连不上只该报一次；报了多次说明底层在自己重连");
}

IMRTC_TEST(ixTransportSendBeforeOpenIsDropped, "IxTransport —— 没连上时 send 静默丢弃，不抛不崩") {
  imrtc::IxTransport transport;
  Recorder recorder;
  transport.setListener(&recorder);

  // 还没 connect 就发：应当什么都不做（Connection 那边有 isOpen 的前置判断，
  // 这里是第二道防线）。
  transport.send("{\"type\":\"sys.ping\"}");
  CHECK_EQ(recorder.events.size(), std::size_t{0}, "不该产生任何事件");
  CHECK_EQ(transport.isOpen(), false, "isOpen 是 false");
}

IMRTC_TEST(ixTransportCloseIsIdempotent, "IxTransport —— close 幂等，析构时不会有回调打到已销毁的对象上") {
  Recorder recorder;
  {
    imrtc::IxTransport transport;
    transport.setListener(&recorder);
    transport.connect("ws://127.0.0.1:1");
    transport.close(imrtc::closecode::kNormal, "bye");
    transport.close(imrtc::closecode::kNormal, "bye again");
    CHECK_EQ(transport.isOpen(), false, "close 之后 isOpen 是 false");
  }
  // 走到这里没崩，就说明析构顺序是对的：ws_ 先走、join 掉后台线程，
  // 之后才轮到 mutex_ / queue_。ASan 会在这条路径上抓 use-after-free。
  CHECK_TRUE(true, "析构不崩");
}
