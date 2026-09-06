#include <algorithm>
#include <string>
#include <vector>

#include "FakeTransport.h"
#include "TestHarness.h"
#include "imrtc/Backoff.h"
#include "imrtc/Connection.h"
#include "imrtc/Registry.h"
#include "imrtc/Transport.h"

using imrtc::Connection;
using imrtc::ConnectionEvents;
using imrtc::ConnectionOptions;
using imrtc::ConnectionState;
using imrtc::Json;

/**
 * 断线重连与关闭码（RTC_PROTOCOL.md §1.4、§1.5）。
 *
 * 退避抖动在这些用例里固定成 0（random 恒返回 0.5），档位才是确定的；
 * 抖动本身在 backoffJitter 用例里单独测。
 */
namespace {

constexpr std::int64_t kT0 = 1756876800000;

struct Harness {
  imtest::FakeNet net;
  std::vector<std::string> disconnected;
  int kickedOut = 0;
  std::unique_ptr<Connection> connection;

  Harness() {
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
    events.onKickedOut = [this]() { ++kickedOut; };
    connection = std::unique_ptr<Connection>(new Connection(options, events));
  }

  void handshake(const std::string& sessionId = "s-1", bool resumed = false) {
    connection->connect(kT0);
    net.open();
    const std::string reqId = imtest::field(imtest::lastSent(net.current()), "req_id");
    net.deliver(imtest::replyFrame(imrtc::okType(imrtc::frame::kHello), reqId,
                                   imtest::helloOkData(sessionId, resumed)));
  }
};

/** closeBehaviour 跑一次「连上 → 被这个码关掉」，返回是否安排了重连。 */
bool willReconnectFor(int code) {
  Harness harness;
  harness.handshake();
  harness.net.remoteClose(code);
  CHECK_EQ(harness.disconnected.size(), std::size_t{1}, "应当抛一次 onDisconnected");
  return harness.disconnected[0].find("/retry") != std::string::npos;
}

}  // namespace

IMRTC_TEST(reconnectCarriesSessionId, "重连 —— 第二次 sys.hello 带上旧 session_id 请求恢复") {
  Harness harness;
  harness.handshake("s-1");

  harness.net.remoteClose(imrtc::closecode::kGoingAway, "server restart");
  CHECK_EQ(harness.connection->state(), ConnectionState::Reconnecting, "断开后进 reconnecting");
  CHECK_EQ(harness.net.socketCount(), std::size_t{1}, "还没到点，不该新建连接");

  // 第一档 1 秒（抖动 0）。
  harness.connection->tick(kT0 + 999);
  CHECK_EQ(harness.net.socketCount(), std::size_t{1}, "不到 1 秒不该重连");
  harness.connection->tick(kT0 + 1000);
  CHECK_EQ(harness.net.socketCount(), std::size_t{2}, "到点该新建连接");

  harness.net.open();
  const Json hello = imtest::lastSent(harness.net.current());
  CHECK_EQ(imtest::field(hello, "type"), std::string("sys.hello"), "重连也先发 sys.hello");
  CHECK_EQ(imtest::field(hello, "session_id"), std::string("s-1"), "带上旧 session_id");

  const std::string reqId = imtest::field(hello, "req_id");
  harness.net.deliver(imtest::replyFrame("sys.hello.ok", reqId, imtest::helloOkData("s-1", true)));
  CHECK_EQ(harness.connection->state(), ConnectionState::Connected, "恢复后回到 connected");
  CHECK_EQ(harness.connection->reconnectAttempts(), 0, "连上之后退避档要重置");
}

IMRTC_TEST(reconnectCloseCodes, "重连 —— 1000 / 4400 / 4403 绝不重连，1001 / 4429 要重连") {
  CHECK_EQ(willReconnectFor(imrtc::closecode::kNormal), false, "1000 正常关闭不重连");
  CHECK_EQ(willReconnectFor(imrtc::closecode::kBadProtocol), false, "4400 是我们自己的 bug，不重连");
  CHECK_EQ(willReconnectFor(imrtc::closecode::kKickedOut), false, "4403 被踢，不重连");
  CHECK_EQ(willReconnectFor(imrtc::closecode::kGoingAway), true, "1001 服务端重启，重连");
  CHECK_EQ(willReconnectFor(imrtc::closecode::kRateLimited), true, "4429 频率超限，退避后重连");
}

IMRTC_TEST(reconnectKickedOut, "重连 —— 4403 抛 onKickedOut 且不再重连") {
  Harness harness;
  harness.handshake();
  harness.net.remoteClose(imrtc::closecode::kKickedOut, "logged in elsewhere");

  CHECK_EQ(harness.kickedOut, 1, "应当抛一次 onKickedOut");
  CHECK_EQ(harness.connection->state(), ConnectionState::Closed, "被踢之后是 closed");
  harness.connection->tick(kT0 + 60000);
  CHECK_EQ(harness.net.socketCount(), std::size_t{1}, "被踢之后绝不重连");
}

IMRTC_TEST(reconnectAuthFailures, "重连 —— 连续 3 次 4401 抛 onKickedOut 并停止重连（§1.5）") {
  Harness harness;
  harness.handshake();
  std::int64_t now = kT0;

  // 第 1、2 次 4401：还会重连，退避档往上走。
  for (int attempt = 1; attempt <= 2; ++attempt) {
    harness.net.remoteClose(imrtc::closecode::kUnauthorized, "token invalid");
    CHECK_EQ(harness.connection->authFailures(), attempt, "鉴权失败计数");
    CHECK_EQ(harness.kickedOut, 0, "还没到上限，不该抛 onKickedOut");
    now += 60000;
    harness.connection->tick(now);
    harness.net.open();  // 新连接打开，又发一次 hello
  }

  // 第 3 次：到上限，抛 onKickedOut 并彻底停。
  harness.net.remoteClose(imrtc::closecode::kUnauthorized, "token invalid");
  CHECK_EQ(harness.connection->authFailures(), 3, "第三次失败");
  CHECK_EQ(harness.kickedOut, 1, "到上限该抛 onKickedOut");

  const std::size_t sockets = harness.net.socketCount();
  harness.connection->tick(now + 120000);
  CHECK_EQ(harness.net.socketCount(), sockets, "用尽之后不再重连");

  // 换票是「这次不一样了」的唯一信号：计数清零，并且能重新连。
  harness.connection->updateToken("tk-2");
  CHECK_EQ(harness.connection->authFailures(), 0, "换票要把计数清零");
  harness.connection->connect(now + 130000);
  CHECK_EQ(harness.net.socketCount(), sockets + 1, "换票后显式 connect 应当能连");
  harness.net.open();
  CHECK_EQ(imtest::field(imtest::lastSent(harness.net.current()), "token"), std::string("tk-2"),
           "用的是新票");
}

IMRTC_TEST(reconnectBackoffSteps, "重连 —— 退避档位 1/2/4/8/15/30 秒，之后固定 30 秒") {
  Harness harness;
  harness.handshake();

  const std::vector<std::int64_t> want = {1000, 2000, 4000, 8000, 15000, 30000, 30000};
  std::int64_t now = kT0;
  for (std::size_t i = 0; i < want.size(); ++i) {
    harness.net.remoteClose(imrtc::closecode::kGoingAway);
    const std::size_t before = harness.net.socketCount();

    // 差 1 毫秒不该动；到点才动。抖动固定成 0，所以档位是精确的。
    harness.connection->tick(now + want[i] - 1);
    CHECK_EQ(harness.net.socketCount(), before,
             "第 " + std::to_string(i + 1) + " 档：早 1 毫秒不该重连");
    now += want[i];
    harness.connection->tick(now);
    CHECK_EQ(harness.net.socketCount(), before + 1,
             "第 " + std::to_string(i + 1) + " 档：到点该重连");
    harness.net.open();
  }
}

IMRTC_TEST(backoffJitter, "退避 —— 每档 ±20% 抖动，越界的 attempt 钳到最后一档") {
  const std::vector<std::int64_t>& steps = imrtc::backoffStepsMs();
  CHECK_EQ(steps, std::vector<std::int64_t>({1000, 2000, 4000, 8000, 15000, 30000}), "档位表");

  // random=0 → 下界 -20%；random=1 → 上界 +20%；0.5 → 正中。
  CHECK_EQ(imrtc::backoffDelayMs(0, []() { return 0.0; }), std::int64_t{800}, "第 1 档下界");
  CHECK_EQ(imrtc::backoffDelayMs(0, []() { return 1.0; }), std::int64_t{1200}, "第 1 档上界");
  CHECK_EQ(imrtc::backoffDelayMs(0, []() { return 0.5; }), std::int64_t{1000}, "第 1 档正中");

  CHECK_EQ(imrtc::backoffDelayMs(-1, []() { return 0.5; }), std::int64_t{1000}, "负数钳到第 1 档");
  CHECK_EQ(imrtc::backoffDelayMs(99, []() { return 0.5; }), std::int64_t{30000}, "越界钳到最后一档");

  // 默认随机源：跑一批，确认都落在 [800, 1200] 且确实在抖。
  bool varied = false;
  const std::int64_t first = imrtc::backoffDelayMs(0, imrtc::defaultRandom01);
  for (int i = 0; i < 200; ++i) {
    const std::int64_t delay = imrtc::backoffDelayMs(0, imrtc::defaultRandom01);
    CHECK_TRUE(delay >= 800 && delay <= 1200, "抖动越界：" + std::to_string(delay));
    if (delay != first) varied = true;
  }
  CHECK_TRUE(varied, "默认随机源应当真的在抖，否则服务端重启时所有客户端会同时回来");
}

IMRTC_TEST(reconnectCloseStops, "重连 —— 主动 close 之后不重连，在途请求全部失败") {
  Harness harness;
  harness.handshake();

  std::vector<imrtc::RequestResult> results;
  harness.connection->request(imrtc::frame::kRoomLeave, Json::parse("{\"room_id\":\"r-1\"}"), kT0,
                              [&results](const imrtc::RequestResult& r) { results.push_back(r); });

  harness.connection->close();
  CHECK_EQ(harness.connection->state(), ConnectionState::Closed, "close 之后是 closed");
  CHECK_EQ(results.size(), std::size_t{1}, "在途请求应当被结算");
  CHECK_EQ(results[0].errorName, std::string("invalid_state"), "主动关闭的错误名");
  CHECK_EQ(harness.net.current().closeCode, imrtc::closecode::kNormal, "主动关闭用 1000");

  harness.connection->tick(kT0 + 60000);
  CHECK_EQ(harness.net.socketCount(), std::size_t{1}, "主动关闭之后不重连");
}
