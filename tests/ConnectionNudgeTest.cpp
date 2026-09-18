#include <memory>
#include <string>
#include <vector>

#include "FakeTransport.h"
#include "TestHarness.h"
#include "imrtc/Connection.h"
#include "imrtc/Registry.h"

using imrtc::Connection;
using imrtc::ConnectionEvents;
using imrtc::ConnectionOptions;
using imrtc::Json;

/**
 * 回前台 / 网络变化：不再按退避白等（2026-09-18，与 iOS `NetworkNudgeTests`、Android
 * `NetworkChangeReconnectTest`、Web `networkNudge.test.ts` 对应）。规则见 `ConnectionNudge.cpp`。
 *
 * 退避抖动固定成 0（random 恒 0.5）：第一档正好 1000ms，「立刻」的判据是不等这 1 秒。
 */
namespace {

constexpr std::int64_t kT0 = 1756876800000;

struct Harness {
  imtest::FakeNet net;
  std::vector<std::string> disconnected;
  std::unique_ptr<Connection> connection;

  explicit Harness(bool deferClose = false) {
    net.deferClose = deferClose;
    ConnectionOptions options;
    options.url = "wss://rtc.example.com/v1/ws";
    options.token = "tk-1";
    options.deviceId = "mac-8f3a";
    options.transportFactory = net.factory();
    options.random = []() { return 0.5; };
    ConnectionEvents events;
    events.onDisconnected = [this](int code, const std::string&, bool willReconnect) {
      disconnected.push_back(std::to_string(code) + (willReconnect ? "/retry" : "/stop"));
    };
    connection = std::unique_ptr<Connection>(new Connection(options, events));
  }

  void handshake() {
    connection->connect(kT0);
    net.open();
    const std::string reqId = imtest::field(imtest::lastSent(net.current()), "req_id");
    net.deliver(imtest::replyFrame(imrtc::okType(imrtc::frame::kHello), reqId,
                                   imtest::helloOkData("s-1", false)));
  }
};

}  // namespace

IMRTC_TEST(nudgeWaitingReconnectsOnNextTick, "网络变化：正等着退避的，下一个 tick 就连、退避归零") {
  Harness h;
  h.handshake();
  h.net.remoteClose(1001);  // 排上 kT0+1000
  h.connection->networkChanged(kT0 + 10);
  h.connection->tick(kT0 + 10);
  CHECK_EQ(h.net.socketCount(), std::size_t{2}, "不该再等退避那 1 秒");
  CHECK_EQ(h.connection->reconnectAttempts(), 0, "退避该归零");
}

IMRTC_TEST(nudgeForegroundOnlyWhenComingBack, "回前台同样立刻重连；进后台什么都不做") {
  Harness h;
  h.handshake();
  h.net.remoteClose(1001);
  h.connection->appForeground(false, kT0 + 10);
  h.connection->tick(kT0 + 500);
  CHECK_EQ(h.net.socketCount(), std::size_t{1}, "进后台不该提前连");
  h.connection->appForeground(true, kT0 + 600);
  h.connection->tick(kT0 + 600);
  CHECK_EQ(h.net.socketCount(), std::size_t{2}, "回前台该立刻连");
}

IMRTC_TEST(nudgeProbeAnsweredKeepsConnection, "连着：探一下，有回音就什么都不动") {
  Harness h;
  h.handshake();
  h.connection->networkChanged(kT0 + 100);
  const Json ping = imtest::lastSent(h.net.current());
  CHECK_EQ(imtest::field(ping, "type"), std::string(imrtc::frame::kPing), "该立刻发探测 ping");
  h.net.deliver(imtest::replyFrame(imrtc::frame::kPong, imtest::field(ping, "req_id"), Json::makeObject()));
  h.connection->tick(kT0 + 3100);
  CHECK_TRUE(!h.net.current().closed, "活连接不许被误断");
  CHECK_EQ(h.disconnected.size(), std::size_t{0}, "不该有断开");
}

IMRTC_TEST(nudgeProbeUnansweredReconnectsWithoutBackoff, "连着：3 秒没回音就判死，当场重连不走退避") {
  Harness h;
  h.handshake();
  h.connection->networkChanged(kT0 + 100);
  h.connection->tick(kT0 + 3099);
  CHECK_TRUE(!h.net.at(0).closed, "差 1ms 不该判死");
  h.connection->tick(kT0 + 3100);
  CHECK_EQ(h.net.at(0).closeCode, 1001, "判死走主动关（goingAway）");
  CHECK_EQ(h.disconnected.size(), std::size_t{1}, "抛一次断开");
  CHECK_EQ(h.net.socketCount(), std::size_t{2}, "同一个 tick 里就重连，不等退避 1 秒");
}

IMRTC_TEST(nudgeProbeWithRealisticDeferredClose, "真 Transport 的关闭事件晚一个 poll 才到：下一个 tick 重连") {
  Harness h(/*deferClose=*/true);
  h.handshake();
  h.connection->networkChanged(kT0 + 100);
  h.connection->tick(kT0 + 3100);
  CHECK_EQ(h.net.socketCount(), std::size_t{1}, "关闭事件还没放出来");
  h.connection->tick(kT0 + 3200);
  CHECK_EQ(h.net.socketCount(), std::size_t{2}, "poll 放出关闭后同一个 tick 就重连");
}

IMRTC_TEST(nudgeInFlightFailureRetriesAtOnce, "正在连：这次失败后立刻再连，不走退避") {
  Harness h;
  h.handshake();
  h.net.remoteClose(1001);
  h.connection->tick(kT0 + 1000);  // 退避 1 秒后正在连
  CHECK_EQ(h.net.socketCount(), std::size_t{2}, "前提：正在连第二条");
  h.connection->networkChanged(kT0 + 1100);
  h.connection->tick(kT0 + 1100);
  CHECK_EQ(h.net.socketCount(), std::size_t{2}, "正在连的那次让它跑完，不另开");
  h.net.remoteClose(1006);  // 没连上；下一档本该是 2 秒
  h.connection->tick(kT0 + 1100);
  CHECK_EQ(h.net.socketCount(), std::size_t{3}, "失败后该立刻再连");
}

IMRTC_TEST(nudgeTwoInARowAreTwoSecondsApart, "网络来回跳：两次立刻重连之间至少隔 2 秒") {
  Harness h;
  h.handshake();
  h.net.remoteClose(1001);
  h.connection->networkChanged(kT0 + 100);
  h.connection->tick(kT0 + 100);
  CHECK_EQ(h.net.socketCount(), std::size_t{2}, "第一次立刻连");
  h.net.remoteClose(1006);                   // 退避排到 kT0+1100
  h.connection->networkChanged(kT0 + 200);   // 补足间隔：kT0+2100
  h.connection->tick(kT0 + 1100);
  CHECK_EQ(h.net.socketCount(), std::size_t{2}, "也不许走退避那 1 秒");
  h.connection->tick(kT0 + 2099);
  CHECK_EQ(h.net.socketCount(), std::size_t{2}, "距上次不足 2 秒");
  h.connection->tick(kT0 + 2100);
  CHECK_EQ(h.net.socketCount(), std::size_t{3}, "满 2 秒该连");
}

IMRTC_TEST(nudgeIgnoredWhenIdleOrClosed, "没登录或已登出：什么都不做") {
  Harness h;
  h.connection->networkChanged(kT0);
  h.connection->appForeground(true, kT0);
  h.connection->tick(kT0 + 5000);
  CHECK_EQ(h.net.socketCount(), std::size_t{0}, "没登录不该连");

  h.handshake();
  h.connection->close();
  const std::size_t sent = h.net.current().sent.size();
  h.connection->networkChanged(kT0 + 100);
  h.connection->tick(kT0 + 10000);
  CHECK_EQ(h.net.socketCount(), std::size_t{1}, "登出后不该连");
  CHECK_EQ(h.net.current().sent.size(), sent, "登出后不该发探测");
}
