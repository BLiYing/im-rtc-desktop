#include <string>
#include <vector>

#include "FakeTransport.h"
#include "TestHarness.h"
#include "imrtc/Connection.h"
#include "imrtc/Registry.h"

using imrtc::Connection;
using imrtc::ConnectionEvents;
using imrtc::ConnectionOptions;
using imrtc::ConnectionState;
using imrtc::Json;
using imrtc::RequestResult;

/**
 * 信令连接的时序测试。
 *
 * **一秒都不用真的等**：Connection 不持有定时器，心跳周期、请求超时、退避延时
 * 全靠 `tick(nowMs)` 推进，时间由测试喂。「45 秒静默判死」「10 秒请求超时」
 * 这些规则因此可以被确定性地复现。
 */
namespace {

/** kT0 是所有用例的起点时刻。 */
constexpr std::int64_t kT0 = 1756876800000;

/** Harness 把 Connection、假网络与收到的事件打包在一起。 */
struct Harness {
  imtest::FakeNet net;
  std::vector<std::string> connected;
  std::vector<std::string> disconnected;
  int kickedOut = 0;
  std::vector<std::string> events;
  std::vector<std::int32_t> errors;
  std::unique_ptr<Connection> connection;

  explicit Harness(std::int64_t requestTimeoutMs = 10000) {
    ConnectionOptions options;
    options.url = "wss://rtc.example.com/v1/ws";
    options.token = "tk-1";
    options.deviceId = "mac-8f3a";
    options.sdk = "desktop/0.0.1";
    options.requestTimeoutMs = requestTimeoutMs;
    options.transportFactory = net.factory();
    // 抖动固定成 0：退避档才是确定的，测试才好写。抖动本身单独测。
    options.random = []() { return 0.5; };

    ConnectionEvents events_;
    events_.onConnected = [this](const imrtc::HelloOk& hello) {
      connected.push_back(hello.sessionId + (hello.resumed ? "/resumed" : "/fresh"));
    };
    events_.onDisconnected = [this](int code, const std::string&, bool willReconnect) {
      disconnected.push_back(std::to_string(code) + (willReconnect ? "/retry" : "/stop"));
    };
    events_.onKickedOut = [this]() { ++kickedOut; };
    events_.onEvent = [this](const std::string& type, const Json&, const imrtc::Envelope&) {
      events.push_back(type);
    };
    events_.onError = [this](std::int32_t code, const std::string&, const std::string&) {
      errors.push_back(code);
    };
    connection = std::unique_ptr<Connection>(new Connection(options, events_));
  }

  /** handshake 走完一次完整握手，停在 connected。 */
  void handshake(const std::string& sessionId = "s-1", bool resumed = false,
                 std::int64_t pingIntervalSec = 15) {
    connection->connect(kT0);
    net.open();
    const std::string reqId = imtest::field(imtest::lastSent(net.current()), "req_id");
    net.deliver(imtest::replyFrame(imrtc::okType(imrtc::frame::kHello), reqId,
                                   imtest::helloOkData(sessionId, resumed, pingIntervalSec)));
  }
};

}  // namespace

IMRTC_TEST(connHandshake, "Connection —— 握手发 sys.hello，收 hello.ok 后进 connected") {
  Harness harness;
  harness.connection->connect(kT0);

  CHECK_EQ(harness.net.socketCount(), std::size_t{1}, "应当造了一条连接");
  CHECK_EQ(harness.net.current().url, std::string("wss://rtc.example.com/v1/ws"), "连接的 URL");
  // 底层还没打开，一帧都不该发出去。
  CHECK_EQ(harness.net.current().sent.size(), std::size_t{0}, "打开前不该发帧");

  harness.net.open();
  const Json hello = imtest::lastSent(harness.net.current());
  CHECK_EQ(imtest::field(hello, "type"), std::string("sys.hello"), "首帧必须是 sys.hello");
  CHECK_EQ(imtest::field(hello, "token"), std::string("tk-1"), "带上 token");
  CHECK_EQ(imtest::field(hello, "device_id"), std::string("mac-8f3a"), "带上 device_id");
  CHECK_EQ(imtest::field(hello, "session_id"), std::string(""), "首次连接 session_id 为空");
  // 从「已填好默认值的实例」起手：protocol_version 默认 1，从零值起手会发出 0。
  CHECK_EQ(hello.find("data")->find("protocol_version")->asInt(), std::int64_t{1},
           "protocol_version 必须是 1");
  CHECK_TRUE(!imtest::field(hello, "req_id").empty(), "请求必须带非空 req_id");

  const std::string reqId = imtest::field(hello, "req_id");
  harness.net.deliver(imtest::replyFrame("sys.hello.ok", reqId, imtest::helloOkData("s-1", false)));

  CHECK_EQ(harness.connection->state(), ConnectionState::Connected, "握手后的状态");
  CHECK_EQ(harness.connection->sessionId(), std::string("s-1"), "记住 session_id");
  CHECK_EQ(harness.connected, std::vector<std::string>{"s-1/fresh"}, "onConnected");
  CHECK_EQ(harness.connection->pendingCount(), std::size_t{0}, "握手结算后不该有在途请求");
}

IMRTC_TEST(connHeartbeat, "Connection —— 每个周期发 ping，收到任何帧都算活着，连续 3 个周期静默判死") {
  Harness harness;
  harness.handshake("s-1", false, 15);
  const std::size_t afterHello = harness.net.current().sent.size();

  harness.connection->tick(kT0 + 14000);
  CHECK_EQ(harness.net.current().sent.size(), afterHello, "不到一个周期不该发 ping");

  harness.connection->tick(kT0 + 15000);
  CHECK_EQ(imtest::field(imtest::lastSent(harness.net.current()), "type"), std::string("sys.ping"),
           "到点该发 ping");

  // **判活条件是「收到对端任何一帧」**，不是 pong 回来了（§1.3）。
  harness.net.deliver(imtest::replyFrame("room.closed", "",
                                         imrtc::Json::parse("{\"room_id\":\"r-1\"}")));
  // 最后一帧落在 15000 附近，于是「连续 3 个周期 = 45 秒」的死线正是 60000（§1.3）。
  harness.connection->tick(kT0 + 30000);
  harness.connection->tick(kT0 + 45000);
  CHECK_TRUE(!harness.net.current().closed, "才静默两个周期，不该判死");

  harness.connection->tick(kT0 + 60000);
  CHECK_TRUE(harness.net.current().closed, "连续 3 个周期（45s）静默之后判死");
  CHECK_EQ(harness.net.current().closeCode, imrtc::closecode::kGoingAway, "判死用的关闭码");

  // 回归：早先这里写的是 `missed_ > kMissLimit`，要攒到第 4 个周期（60s 静默、
  // 也就是 tick(75000)）才判死，比服务端那边晚整整一个周期。
}

IMRTC_TEST(connLateOkIsNotAnEvent,
           "Connection —— 超时之后才回来的 .ok 是迟到的应答，不许当成服务端主动事件") {
  Harness harness;
  harness.handshake();

  std::vector<std::string> outcomes;
  harness.connection->request(imrtc::frame::kRoomJoin, Json::makeObject(), kT0,
                              [&outcomes](const RequestResult& result) {
                                outcomes.push_back(result.ok ? "ok" : "fail");
                              });
  const std::string reqId = imtest::field(imtest::lastSent(harness.net.current()), "req_id");

  // 十秒无应答：expire 报 2004，在途表里已经没有这条了。
  harness.connection->tick(kT0 + 10000);
  CHECK_EQ(outcomes, std::vector<std::string>{"fail"}, "先超时");

  // 服务端慢半拍才把 .ok 送回来。
  harness.net.deliver(imtest::replyFrame(imrtc::okType(imrtc::frame::kRoomJoin), reqId,
                                         Json::parse("{\"room_id\":\"r-1\"}")));
  /*
    **回归**：早先没人在等的帧一律往 dispatchEvent 掉，于是这条 room.join.ok 会被
    当成服务端主动推送交给上层——房间机据此从 idle 跳回 joined，而宿主刚刚才收到
    进房失败。事件的 req_id 恒为 ""（§2.2），带 req_id 的 .ok 只能是应答。
  */
  CHECK_EQ(harness.events, std::vector<std::string>{}, "迟到的 .ok 不许当事件抛上去");
  CHECK_EQ(outcomes, std::vector<std::string>{"fail"}, "也不许把回调再调一次");
}

IMRTC_TEST(connReplyTypeMustMatch,
           "Connection —— 对不上号的应答类型不算这条请求的应答（只认 req_id 会串号）") {
  Harness harness;
  harness.handshake();

  std::vector<std::string> outcomes;
  harness.connection->request(imrtc::frame::kCallInvite, Json::makeObject(), kT0,
                              [&outcomes](const RequestResult& result) {
                                outcomes.push_back(result.ok ? "ok" : "fail");
                              });
  const std::string reqId = imtest::field(imtest::lastSent(harness.net.current()), "req_id");

  /*
    服务端把一条房间事件挂在了这个 req_id 上（串号、或重连后 req_id 撞车）。
    **回归**：早先 settle 只按 req_id 认，于是这条帧把 call.invite 结算成「成功」，
    而 data 是按 participant_joined 的字段表解的——call_id 是空串，
    之后每一次挂断都发向一个空 call_id，服务端换回 1401，那通电话再也退不出去。
  */
  harness.net.deliver(imtest::replyFrame(
      imrtc::frame::kRoomParticipantJoined, reqId,
      Json::parse("{\"room_id\":\"r-1\",\"uid\":\"bob\"}")));
  CHECK_EQ(outcomes, std::vector<std::string>{}, "类型对不上，不许结算成成功");

  // 真正的应答回来了才算数。
  harness.net.deliver(imtest::replyFrame(imrtc::okType(imrtc::frame::kCallInvite), reqId,
                                         Json::parse("{\"call_id\":\"call-1\"}")));
  CHECK_EQ(outcomes, std::vector<std::string>{"ok"}, "对得上的 .ok 才结算");
}

IMRTC_TEST(connPongIsSwallowed, "Connection —— pong 不往上抛（它的全部信息是「还活着」）") {
  Harness harness;
  harness.handshake();
  harness.connection->tick(kT0 + 15000);
  const std::string pingReqId = imtest::field(imtest::lastSent(harness.net.current()), "req_id");

  harness.net.deliver(imtest::replyFrame("sys.pong", pingReqId, Json::makeObject()));
  CHECK_EQ(harness.events, std::vector<std::string>{}, "pong 不该变成宿主事件");
}

IMRTC_TEST(connRequestTimeout, "Connection —— 请求 10 秒无应答抛 2004 signaling_timeout") {
  Harness harness;
  harness.handshake();

  std::vector<RequestResult> results;
  const bool sent = harness.connection->request(
      imrtc::frame::kRoomLeave, Json::parse("{\"room_id\":\"r-1\"}"), kT0,
      [&results](const RequestResult& result) { results.push_back(result); });
  CHECK_TRUE(sent, "请求应当发得出去");
  CHECK_EQ(harness.connection->pendingCount(), std::size_t{1}, "在途请求数");

  harness.connection->tick(kT0 + 9999);
  CHECK_EQ(results.size(), std::size_t{0}, "没到 10 秒不该超时");

  harness.connection->tick(kT0 + 10000);
  CHECK_EQ(results.size(), std::size_t{1}, "到 10 秒该超时");
  CHECK_EQ(results[0].ok, false, "超时是失败");
  CHECK_EQ(results[0].errorName, std::string("signaling_timeout"), "超时的错误名");
  CHECK_EQ(results[0].forType, std::string("room.leave"), "超时带上出错的请求 type");
  CHECK_EQ(harness.connection->pendingCount(), std::size_t{0}, "超时后不该还挂着");
}

IMRTC_TEST(connPairsByReqId, "Connection —— 按 req_id 配对而不是按帧类型（room.offer 由 room.answer 应答）") {
  Harness harness;
  harness.handshake();

  std::vector<RequestResult> results;
  harness.connection->request(imrtc::frame::kRoomOffer,
                              Json::parse("{\"pc\":\"pub\",\"sdp\":\"v=0\"}"), kT0,
                              [&results](const RequestResult& r) { results.push_back(r); });
  const std::string reqId = imtest::field(imtest::lastSent(harness.net.current()), "req_id");

  // 类型对不上（offer 的应答是 answer），但 req_id 对得上——就该结算。
  harness.net.deliver(imtest::replyFrame(imrtc::frame::kRoomAnswer, reqId,
                                         Json::parse("{\"pc\":\"pub\",\"sdp\":\"v=0-ans\"}")));

  CHECK_EQ(results.size(), std::size_t{1}, "应当被结算");
  CHECK_TRUE(results[0].ok, "应当是成功");
  CHECK_EQ(results[0].envelope.type, std::string("room.answer"), "应答的帧类型");
  CHECK_EQ(imtest::field(results[0].data, "sdp"), std::string("v=0-ans"), "已解码的 data");
  CHECK_EQ(harness.events, std::vector<std::string>{}, "被请求认领的帧不该再当事件抛");
}

IMRTC_TEST(connServerErrorFailsRequest, "Connection —— sys.error 应答让请求失败并带上服务端的码") {
  Harness harness;
  harness.handshake();

  std::vector<RequestResult> results;
  harness.connection->request(imrtc::frame::kRoomJoin,
                              Json::parse("{\"room_id\":\"r-1\",\"room_token\":\"tk\"}"), kT0,
                              [&results](const RequestResult& r) { results.push_back(r); });
  const std::string reqId = imtest::field(imtest::lastSent(harness.net.current()), "req_id");

  harness.net.deliver(imtest::replyFrame(
      "sys.error", reqId,
      Json::parse("{\"code\":1201,\"name\":\"room_not_found\",\"msg\":\"room not found\","
                  "\"for_type\":\"room.join\",\"retryable\":false}")));

  CHECK_EQ(results.size(), std::size_t{1}, "应当被结算");
  CHECK_EQ(results[0].ok, false, "sys.error 是失败");
  CHECK_EQ(results[0].errorCode, std::int32_t{1201}, "服务端的错误码");
  CHECK_EQ(results[0].errorName, std::string("room_not_found"), "错误名从本地表查出来");
  CHECK_EQ(results[0].forType, std::string("room.join"), "出错的请求 type");
}

IMRTC_TEST(connUnknownTypeIgnored, "Connection —— 未知 type 的事件静默忽略（前向兼容 §2.3）") {
  Harness harness;
  harness.handshake();

  harness.net.deliver(imtest::replyFrame("room.something_new", "", Json::makeObject()));
  CHECK_EQ(harness.events, std::vector<std::string>{}, "未知帧不该往上抛");
  CHECK_EQ(harness.errors, std::vector<std::int32_t>{}, "未知帧也不该报错");

  harness.net.deliver(imtest::replyFrame(
      imrtc::frame::kRoomParticipantJoined, "",
      Json::parse("{\"room_id\":\"r-1\",\"participant_id\":\"p-2\",\"uid\":\"bob\"}")));
  CHECK_EQ(harness.events, std::vector<std::string>{"room.participant_joined"}, "已知事件正常抛");
}

IMRTC_TEST(connUndecodableFrameCloses, "Connection —— 解不开的帧报错并以 4400 断开") {
  Harness harness;
  harness.handshake();

  harness.net.deliver("{\"type\":\"sys.ping\",");
  CHECK_EQ(harness.errors.size(), std::size_t{1}, "应当报一个错");
  CHECK_EQ(harness.errors[0], std::int32_t{1001}, "解不开的帧是 bad_envelope");
  CHECK_EQ(harness.net.current().closeCode, imrtc::closecode::kBadProtocol, "以 4400 断开");
}

IMRTC_TEST(connDisconnectFailsPending, "Connection —— 断线时在途请求全部失败（应答永远不会来了）") {
  Harness harness;
  harness.handshake();

  std::vector<RequestResult> results;
  harness.connection->request(imrtc::frame::kRoomLeave, Json::parse("{\"room_id\":\"r-1\"}"), kT0,
                              [&results](const RequestResult& r) { results.push_back(r); });
  CHECK_EQ(harness.connection->pendingCount(), std::size_t{1}, "在途请求数");

  harness.net.remoteClose(imrtc::closecode::kGoingAway, "server restart");
  CHECK_EQ(results.size(), std::size_t{1}, "在途请求应当被结算");
  CHECK_EQ(results[0].errorName, std::string("network_unreachable"), "断线的错误名");
  CHECK_EQ(harness.connection->pendingCount(), std::size_t{0}, "断线后不该还挂着");
}

IMRTC_TEST(connRequestNeedsConnected, "Connection —— 没连上时 request 直接返回 false，不静默吞回调") {
  Harness harness;
  bool called = false;
  const bool sent = harness.connection->request(imrtc::frame::kRoomLeave, Json::makeObject(), kT0,
                                                [&called](const RequestResult&) { called = true; });
  CHECK_EQ(sent, false, "没连上不该发得出去");
  CHECK_EQ(called, false, "返回 false 时回调不该被调用");
}
