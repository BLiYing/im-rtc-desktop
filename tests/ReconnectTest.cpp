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
  imrtc::KickedReason kickedReason = imrtc::KickedReason::TakenOver;
  int unrecoverable = 0;
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
    events.onKickedOut = [this](imrtc::KickedReason reason) {
      ++kickedOut;
      kickedReason = reason;
    };
    events.onSessionUnrecoverable = [this]() { ++unrecoverable; };
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

/**
 * rejectHandshake 让服务端拒掉在飞的那次握手，然后**照真实服务端那样关掉连接**。
 *
 * 关连接这一步不能省。少了它，「不再重连」那条断言是**空的**——没有任何东西会去
 * 排下一次重连，于是把修复整个删掉用例也照样绿。Web 与 iOS 补这条时都踩过一次。
 */
void rejectHandshake(Harness& harness, std::int32_t code, const std::string& name, bool retryable,
                     int closeCode) {
  const std::string reqId = imtest::field(imtest::lastSent(harness.net.current()), "req_id");
  imrtc::Json data = imrtc::Json::makeObject();
  data.set("code", imrtc::Json::make(static_cast<std::int64_t>(code)));
  data.set("name", imrtc::Json::make(name));
  data.set("msg", imrtc::Json::make(name));
  data.set("for_type", imrtc::Json::make(std::string("sys.hello")));
  data.set("retryable", imrtc::Json::make(retryable));
  harness.net.deliver(imtest::replyFrame("sys.error", reqId, std::move(data)));
  harness.net.remoteClose(closeCode, name);
}

}  // namespace

IMRTC_TEST(handshakeRejectedGivesUpAtOnce,
           "握手被拒 —— 不可重试的一次就放弃，并按「谁救得了」给出原因") {
  Harness harness;
  harness.connection->connect(kT0);
  harness.net.open();

  /*
    用 1106 app_disabled + 4401：这是**最能说明问题**的一组。

    服务端对「票验不过」一律关 4401（gateway/handshake.go），而 4401 的既定处置是
    「换新票后重连、三次才放弃」。可 app_disabled 是这个应用被停了，**重连一万次
    它还是停着的**——照 4401 走就是白白三轮，然后还报成「票的问题」。

    （`device_id` 不合规那一条服务端关的是 4400，本来就不重连；但宿主同样只收得到
    一个光秃秃的 onError，不知道该去改配置。原因这一半是这里补的。）
  */
  rejectHandshake(harness, 1106, "app_disabled", false, imrtc::closecode::kUnauthorized);

  CHECK_EQ(harness.kickedOut, 1, "应当抛一次 onKickedOut");
  CHECK_EQ(harness.kickedReason, imrtc::KickedReason::ConfigRejected,
           "1106 换票救不了，该让宿主去改配置");
  CHECK_EQ(harness.disconnected.size(), std::size_t{1}, "抛一次 onDisconnected");
  CHECK_EQ(harness.disconnected[0].find("/stop") != std::string::npos, true, "且明说不会再回来");

  // 载重的那一半：把时间推过所有退避档，一条新连接都不许出现。
  harness.connection->tick(kT0 + 60000);
  CHECK_EQ(harness.net.socketCount(), std::size_t{1}, "不许再重连");
  CHECK_EQ(harness.connection->state(), ConnectionState::Closed, "停在 closed");
}

IMRTC_TEST(handshakeRejectedRetryableStillReconnects,
           "握手被拒 —— 可重试的照常退避重连（别误伤 token_expired）") {
  Harness harness;
  harness.connection->connect(kT0);
  harness.net.open();

  // 1102 token_expired 是**可重试**的：重连的那一刻宿主可能已经 updateToken 了。
  rejectHandshake(harness, 1102, "token_expired", true, imrtc::closecode::kUnauthorized);

  CHECK_EQ(harness.kickedOut, 0, "不该抛 onKickedOut");
  harness.connection->tick(kT0 + 1000);
  CHECK_EQ(harness.net.socketCount(), std::size_t{2}, "第一档到点就重连");
}

IMRTC_TEST(logoutDuringHandshakeIsNotRejection,
           "握手被拒 —— logout 结掉在飞的握手不算被拒（否则静默续期会把人踹回登录页）") {
  Harness harness;
  harness.connection->connect(kT0);
  harness.net.open();

  /*
    close() 会拿 2005 invalid_state 把在飞的 sys.hello 结算掉，而 2005 的
    `retryable` 就是 false。照「不可重试就放弃」判的话，**一次正常的 logout 会被
    报成「服务端拒了你的参数」**——而静默续期正是「先 logout 再换票」，
    等于每次续期都把用户踹回登录页。local 组的码必须直接放行。
  */
  harness.connection->close();

  CHECK_EQ(harness.kickedOut, 0, "宿主自己按的退出，不是被踢");
}

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

/*
  「断得太久 → 服务端那一侧的会话已经没了」这条倒计时（§1.4）。

  守的是真机 2026-09-08 的一幕：断网后停在「正在重连」，**不接网就永远停在
  通话界面，连挂断都点不动**——本地放弃的唯一入口是「重连上了但 resumed=false」，
  而网络不回来那一刻永远不会到。四端同形，本仓是最后一端。

  上界取的是 `3×ping + 30s + 5s`（默认心跳 15 秒 → 80 秒），**不是恢复窗口那 30 秒**：
  服务端的 30 秒是从**它自己察觉**算起，而它要连续 3 个心跳周期收不到东西才察觉（§1.3）。
*/
IMRTC_TEST(sessionUnrecoverableAfterResumeWindow,
           "恢复窗口 —— 断开超过上界就报会话不可恢复") {
  Harness harness;
  harness.handshake();
  harness.net.remoteClose(1001);

  // 60 秒时服务端一定还没放弃（最快 2×ping 才察觉，再加 30 秒窗口）。
  harness.connection->tick(kT0 + 60000);
  CHECK_EQ(harness.unrecoverable, 0, "提前放弃会杀掉一通还能恢复的通话");

  harness.connection->tick(kT0 + 80000);
  CHECK_EQ(harness.unrecoverable, 1, "断了 80 秒还不放弃，界面就永远停在「正在重连」");
}

/*
  **每次重连失败都重排的话，截止时刻就一直往后挪、永远不会到**——
  而那正是这条倒计时要治的病。起点必须是第一次断开的那一刻。
  生产里退避封顶 30 秒 < 80 秒，重排等于这条闸从来不会合上。
*/
IMRTC_TEST(sessionUnrecoverableDeadlineIsNotPushedBack,
           "恢复窗口 —— 重连一直失败不许把截止时刻往后推") {
  Harness harness;
  harness.handshake();
  harness.net.remoteClose(1001);

  // 一路 tick 到 80 秒，中间不断制造「重连失败」。
  for (std::int64_t t = 1000; t <= 80000; t += 1000) {
    harness.connection->tick(kT0 + t);
    harness.net.remoteClose(1001);  // 幂等：这一条已经关掉的话什么都不做
  }
  CHECK_EQ(harness.unrecoverable, 1, "截止时刻被重连失败一路推后 = 这条闸从来不会合上");
}

/** 连上了就撤掉——不撤的话会把一通已经恢复的通话杀掉。 */
IMRTC_TEST(sessionUnrecoverableCancelledOnResume, "恢复窗口 —— 重连成功就把倒计时撤掉") {
  Harness harness;
  harness.handshake();
  harness.net.remoteClose(1001);

  harness.connection->tick(kT0 + 2000);  // 退避到点，重连
  harness.net.open();
  const std::string reqId = imtest::field(imtest::lastSent(harness.net.current()), "req_id");
  harness.net.deliver(imtest::replyFrame(imrtc::okType(imrtc::frame::kHello), reqId,
                                         imtest::helloOkData("s-1", true)));

  harness.connection->tick(kT0 + 200000);
  CHECK_EQ(harness.unrecoverable, 0, "已经连回来了还报不可恢复，会把正在进行的通话杀掉");
}
