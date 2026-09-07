#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "FakeTransport.h"
#include "TestHarness.h"
#include "Vectors.h"
#include "imrtc/CallEngine.h"
#include "imrtc/Registry.h"

using imrtc::CallEngine;
using imrtc::CallEngineObserver;
using imrtc::CallEngineOptions;
using imrtc::CallState;
using imrtc::Json;
using imrtc::RoomState;

/**
 * 门面的端到端测试：宿主调用 → 帧上线路 → 服务端回帧 → 回调抛给宿主。
 *
 * 用假 Transport + 假时钟，所以整条链路（含 10 秒超时、退避重连）都是确定性的。
 */
namespace {

constexpr std::int64_t kT0 = 1756876800000;

/** Recorder 把收到的回调摊成一串可比对的字符串。 */
class Recorder : public CallEngineObserver {
public:
  void onConnected(const std::string& sessionId, bool resumed) override {
    log.push_back("connected:" + sessionId + (resumed ? "/resumed" : "/fresh"));
  }
  void onDisconnected() override { log.push_back("disconnected"); }
  void onKickedOut() override { log.push_back("kickedOut"); }
  void onError(std::int32_t code, const std::string& name, const std::string&) override {
    log.push_back("error:" + std::to_string(code) + "/" + name);
  }
  void onCallReceived(const imrtc::CallInvite& invite) override {
    log.push_back("callReceived:" + invite.callId + "/" + invite.caller + "/" + invite.mediaType);
  }
  void onCallBegin(const imrtc::CallBegin& begin) override {
    log.push_back("callBegin:" + begin.callId + "/" + begin.role);
  }
  void onCallEnd(const imrtc::CallEnd& end) override {
    log.push_back("callEnd:" + end.reason + "/" + std::to_string(end.durationSec));
  }
  void onCallMissed(const imrtc::CallMissed& missed) override {
    log.push_back("callMissed:" + missed.caller);
  }
  void onUserAccept(const std::string& uid) override { log.push_back("userAccept:" + uid); }
  void onUserEnter(const std::string& uid) override { log.push_back("userEnter:" + uid); }
  void onRoomJoined(const std::string& roomId) override { log.push_back("roomJoined:" + roomId); }
  void onRoomLeft(const std::string& roomId) override { log.push_back("roomLeft:" + roomId); }

  std::vector<std::string> log;
};

struct Harness {
  imtest::FakeNet net;
  std::shared_ptr<Recorder> recorder = std::make_shared<Recorder>();
  std::int64_t now = kT0;
  std::unique_ptr<CallEngine> engine;

  Harness() {
    CallEngineOptions options;
    options.url = "wss://rtc.example.com/v1/ws";
    options.deviceId = "mac-8f3a";
    options.transportFactory = net.factory();
    options.random = []() { return 0.5; };
    options.clock = [this]() { return now; };
    engine.reset(new CallEngine(options));
    engine->setObserver(recorder);
  }

  /** login 走完握手，停在 connected。 */
  void login(bool resumed = false, const std::string& sessionId = "s-1") {
    engine->login("tk-1");
    net.open();
    reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData(sessionId, resumed));
  }

  /** lastType 是最后一帧的 type。 */
  std::string lastType() { return imtest::field(imtest::lastSent(net.current()), "type"); }
  /** lastReqId 是最后一帧的 req_id。 */
  std::string lastReqId() { return imtest::field(imtest::lastSent(net.current()), "req_id"); }

  /** reply 用最后一帧的 req_id 回一条应答。 */
  void reply(const std::string& type, Json data) {
    net.deliver(imtest::replyFrame(type, lastReqId(), std::move(data)));
  }
  /** event 投一条服务端主动事件（req_id 为空）。 */
  void event(const std::string& type, Json data) {
    net.deliver(imtest::replyFrame(type, "", std::move(data)));
  }
};

}  // namespace

IMRTC_TEST(engineHappyPath, "CallEngine —— 主叫全程：login → call → 接通 → 进房 → 挂断") {
  Harness harness;
  harness.login();
  CHECK_EQ(harness.recorder->log, std::vector<std::string>{"connected:s-1/fresh"}, "握手回调");

  harness.engine->call({"bob"}, "video", false);
  CHECK_EQ(harness.lastType(), std::string("call.invite"), "call() 该发 call.invite");
  CHECK_EQ(harness.engine->callState(), CallState::Inviting, "进 inviting");

  harness.reply(imrtc::okType(imrtc::frame::kCallInvite),
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\"}"));
  harness.event(imrtc::frame::kCallRinging,
                Json::parse("{\"call_id\":\"call-1\",\"uid\":\"bob\",\"device_count\":1}"));
  harness.event(imrtc::frame::kCallAccepted,
                Json::parse("{\"call_id\":\"call-1\",\"uid\":\"bob\"}"));

  harness.event(imrtc::frame::kCallConnected,
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\",\"room_token\":\"tk\","
                            "\"media_type\":\"video\",\"is_group\":false,"
                            "\"connected_at_ms\":1756876812000,\"accepted_by\":\"bob\"}"));
  // 通话机产出的 room.join 要被转交给房间机，否则随后的 join.ok 没人接。
  CHECK_EQ(harness.lastType(), std::string("room.join"), "接通后该发 room.join");
  CHECK_EQ(harness.engine->callState(), CallState::Connecting, "进 connecting");
  CHECK_EQ(harness.engine->roomState(), RoomState::Joining, "房间机也该知道自己在进房");

  harness.reply(imrtc::okType(imrtc::frame::kRoomJoin),
                Json::parse("{\"room_id\":\"r-1\",\"room_kind\":\"call_1v1\","
                            "\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":[]}"));
  CHECK_EQ(harness.engine->roomState(), RoomState::Joined, "进房成功");

  harness.engine->notifyMediaReady();
  CHECK_EQ(harness.engine->callState(), CallState::Connected, "媒体就绪后才是 connected");

  harness.engine->hangup();
  CHECK_EQ(harness.lastType(), std::string("call.hangup"), "hangup 该发 call.hangup");

  harness.event(imrtc::frame::kCallEnded,
                Json::parse("{\"call_id\":\"call-1\",\"reason\":\"hangup\",\"duration_sec\":201,"
                            "\"ended_by\":\"alice\"}"));
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "结束后回 idle");
  // 通话结束 = 房间没了。不清的话之后每一帧都发向一个已销毁的房间（1201）。
  CHECK_EQ(harness.engine->roomState(), RoomState::Idle, "房间也要跟着归零");

  CHECK_EQ(harness.recorder->log,
           std::vector<std::string>({"connected:s-1/fresh", "userAccept:bob",
                                     "callBegin:call-1/caller", "roomJoined:r-1",
                                     "callEnd:hangup/201"}),
           "完整回调序列");
}

IMRTC_TEST(engineInviteFailure, "CallEngine —— invite 被拒要回 idle 并抛 onCallEnd，否则界面永远收不了场") {
  Harness harness;
  harness.login();
  harness.engine->call({"bob", "alice"}, "audio", true);
  CHECK_EQ(harness.engine->callState(), CallState::Inviting, "先进 inviting");

  // 服务端回 1004（比如群呼把主叫自己也放进了 callee_ids）。
  harness.reply(imrtc::frame::kError,
                Json::parse("{\"code\":1004,\"name\":\"bad_params\",\"msg\":\"invalid frame "
                            "parameters\",\"for_type\":\"call.invite\",\"retryable\":false}"));

  CHECK_EQ(harness.engine->callState(), CallState::Idle, "必须退回 idle");
  CHECK_EQ(harness.recorder->log,
           std::vector<std::string>({"connected:s-1/fresh", "error:1004/bad_params",
                                     "callEnd:error/0"}),
           "报错之后要给界面一个收场信号");
}

IMRTC_TEST(engineJoinFailure, "CallEngine —— room.join 被拒要回 idle 并抛 onRoomLeft") {
  Harness harness;
  harness.login();
  harness.engine->joinRoom("r-9", "tk");
  CHECK_EQ(harness.engine->roomState(), RoomState::Joining, "先进 joining");

  harness.reply(imrtc::frame::kError,
                Json::parse("{\"code\":1201,\"name\":\"room_not_found\",\"msg\":\"room not "
                            "found\",\"for_type\":\"room.join\",\"retryable\":false}"));

  CHECK_EQ(harness.engine->roomState(), RoomState::Idle, "必须退回 idle");
  CHECK_EQ(harness.recorder->log.back(), std::string("roomLeft:r-9"), "房间的收场信号");
}

IMRTC_TEST(engineResumeFailure, "CallEngine —— 恢复失败要本地合成 onCallEnd(network)（不变量 I8）") {
  Harness harness;
  harness.login();
  harness.engine->call({"bob"}, "audio", false);
  harness.reply(imrtc::okType(imrtc::frame::kCallInvite),
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\"}"));
  harness.event(imrtc::frame::kCallConnected,
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\",\"room_token\":\"tk\","
                            "\"media_type\":\"audio\",\"connected_at_ms\":1756876800000,"
                            "\"accepted_by\":\"bob\"}"));
  harness.recorder->log.clear();

  // 断线：通话要**保持在 connected**，界面展示「正在重连…」（§1.4）。
  harness.net.remoteClose(imrtc::closecode::kGoingAway, "network");
  CHECK_EQ(harness.recorder->log, std::vector<std::string>{"disconnected"}, "只报断开");
  CHECK_EQ(harness.engine->callState(), CallState::Connecting, "通话不许因为断线就没了");

  harness.now = kT0 + 1000;
  harness.engine->tick();
  harness.net.open();
  // 超窗了：服务端已按 reason=network 结束通话，我们本地补一条 onCallEnd。
  harness.now = kT0 + 5000;
  harness.reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-2", false));

  CHECK_EQ(harness.recorder->log,
           std::vector<std::string>({"disconnected", "connected:s-2/fresh", "callEnd:network/5"}),
           "恢复失败要合成 onCallEnd(network)，时长用本地计时");
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "合成之后回 idle");
}

IMRTC_TEST(engineKickedOut, "CallEngine —— 4403 被踢：抛 onKickedOut + onDisconnected 并清空一切") {
  Harness harness;
  harness.login();
  harness.engine->call({"bob"}, "audio", false);
  harness.recorder->log.clear();

  harness.net.remoteClose(imrtc::closecode::kKickedOut, "logged in elsewhere");
  CHECK_EQ(harness.recorder->log, std::vector<std::string>({"kickedOut", "disconnected"}),
           "被踢的回调顺序");
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "通话清空");
  CHECK_EQ(harness.engine->roomState(), RoomState::Idle, "房间清空");
}

IMRTC_TEST(engineLogoutIsNotKickedOut, "CallEngine —— 主动 logout 不是被踢：不许抛 onKickedOut") {
  Harness harness;
  harness.login();
  harness.engine->call({"bob"}, "audio", false);
  harness.reply(imrtc::okType(imrtc::frame::kCallInvite),
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\"}"));
  harness.event(imrtc::frame::kCallConnected,
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\",\"room_token\":\"tk\","
                            "\"media_type\":\"audio\",\"connected_at_ms\":1756876800000,"
                            "\"accepted_by\":\"bob\"}"));
  harness.now = kT0 + 7000;
  harness.recorder->log.clear();

  harness.engine->logout();

  /*
    用户自己点的退出，收到一条「您的账号在别处登录」是荒唐的——真机 smoke
    第一次跑就撞上了这个（logout 当时走的是 ws_closed_4403）。

    但**通话记录只由 onCallEnd 拼出来**（不变量 I1），所以中途登出那通电话
    仍然要有一条终局，否则它会从记录里凭空消失。
  */
  for (const std::string& entry : harness.recorder->log) {
    CHECK_TRUE(entry != "kickedOut", "主动 logout 不许抛 onKickedOut");
  }
  CHECK_EQ(harness.recorder->log, std::vector<std::string>({"callEnd:network/7"}),
           "登出要给通话一个终局，时长用本地计时");
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "登出后回 idle");
  CHECK_EQ(harness.engine->roomState(), RoomState::Idle, "房间也清空");
}

IMRTC_TEST(engineObserverIsWeak, "CallEngine —— 观察者放手即注销，回调不许打到已销毁的对象上") {
  Harness harness;
  harness.login();

  // 宿主放手：weak_ptr 失效，之后的事件应当被静默跳过而不是崩。
  harness.recorder.reset();
  harness.engine->call({"bob"}, "audio", false);
  harness.reply(imrtc::okType(imrtc::frame::kCallInvite),
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\"}"));
  harness.event(imrtc::frame::kCallEnded,
                Json::parse("{\"call_id\":\"call-1\",\"reason\":\"hangup\",\"duration_sec\":3}"));

  // 状态机照常推进，只是没人听。ASan 会在这条路径上抓 use-after-free。
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "状态机照常工作");
}

IMRTC_TEST(engineSubAnswerEchoesReqId, "CallEngine —— sub 侧的 answer 要回显服务端那个 offer 的 req_id（§3.3）") {
  Harness harness;
  harness.login();
  harness.engine->joinRoom("r-1", "tk");
  harness.reply(imrtc::okType(imrtc::frame::kRoomJoin),
                Json::parse("{\"room_id\":\"r-1\",\"room_kind\":\"meeting\","
                            "\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":[]}"));

  /*
    sub PC 的 offerer **恒为服务端**（§3.3）：它发一条带 req_id 的 room.offer 当请求，
    我们的 room.answer 就是那条请求的应答，**必须回显同一个 req_id**。
    不回显的话服务端配不上号，那条下行流永远挂不上来——而且不会报错，只是没画面。
  */
  const std::string serverReqId = "srv-77";
  harness.net.deliver(imtest::replyFrame(imrtc::frame::kRoomOffer, serverReqId,
                                         Json::parse("{\"pc\":\"sub\",\"sdp\":\"v=0\"}")));

  const Json answer = imtest::lastSent(harness.net.current());
  CHECK_EQ(imtest::field(answer, "type"), std::string("room.answer"), "该回一条 room.answer");
  CHECK_EQ(imtest::field(answer, "req_id"), serverReqId, "必须回显服务端那个 req_id");
  CHECK_EQ(imtest::field(answer, "pc"), std::string("sub"), "回的是 sub 那条 PC");
}

/**
 * joinedWithTracks 把 harness 送进一个已经有远端轨道的房间。
 *
 * `tracks` 直接写进 `room.join.ok`——这与 `room.track_published` 走的是同一条
 * 记账路径（RoomRecv.cpp），但少三帧噪声。
 */
void joinedWithTracks(Harness& harness, const std::string& tracksJson) {
  harness.login();
  harness.engine->joinRoom("r-1", "tk");
  harness.reply(imrtc::okType(imrtc::frame::kRoomJoin),
                Json::parse("{\"room_id\":\"r-1\",\"room_kind\":\"meeting\","
                            "\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":" +
                            tracksJson + "}"));
}

IMRTC_TEST(engineSetRemoteLayerSendsUpdateLayer,
           "CallEngine —— setRemoteLayer 发 room.update_layer，带那个人的 track_id（§3.5）") {
  Harness harness;
  joinedWithTracks(harness, "[{\"track_id\":\"t-bob-v\",\"uid\":\"bob\",\"kind\":\"video\"}]");

  harness.engine->setRemoteLayer("bob", "h");

  const Json frame = imtest::lastSent(harness.net.current());
  CHECK_EQ(imtest::field(frame, "type"), std::string("room.update_layer"), "该发 room.update_layer");
  CHECK_EQ(imtest::field(frame, "track_id"), std::string("t-bob-v"), "带的是 bob 那条视频轨");
  CHECK_EQ(imtest::field(frame, "max_layer"), std::string("h"), "层上界是 h");
  CHECK_TRUE(!imtest::field(frame, "req_id").empty(), "它是一次请求，必须有 req_id");
}

IMRTC_TEST(engineSetRemoteLayerCoversEveryVideoTrack,
           "CallEngine —— 一个人有多条视频轨时每条都要报，音频轨不参与分层") {
  /*
    屏幕共享落地后一个人就是两条视频轨。只改第一条的话画面全对，
    只是另一条白白多占带宽——**没有任何症状**，所以只能靠用例钉住。
  */
  Harness harness;
  joinedWithTracks(harness,
                   "[{\"track_id\":\"t-cam\",\"uid\":\"bob\",\"kind\":\"video\"},"
                   "{\"track_id\":\"t-mic\",\"uid\":\"bob\",\"kind\":\"audio\"},"
                   "{\"track_id\":\"t-screen\",\"uid\":\"bob\",\"kind\":\"video\"},"
                   "{\"track_id\":\"t-carol\",\"uid\":\"carol\",\"kind\":\"video\"}]");

  const std::size_t before = harness.net.current().sent.size();
  harness.engine->setRemoteLayer("bob", "l");

  std::vector<std::string> touched;
  for (std::size_t i = before; i < harness.net.current().sent.size(); ++i) {
    const Json frame = imtest::parseSent(harness.net.current(), i);
    CHECK_EQ(imtest::field(frame, "type"), std::string("room.update_layer"), "只该发换层帧");
    CHECK_EQ(imtest::field(frame, "max_layer"), std::string("l"), "每条都是 l");
    touched.push_back(imtest::field(frame, "track_id"));
  }
  std::sort(touched.begin(), touched.end());
  CHECK_EQ(touched, (std::vector<std::string>{"t-cam", "t-screen"}),
           "bob 的两条视频轨都要报，音频轨与 carol 一概不碰");
}

IMRTC_TEST(engineSetRemoteLayerDropsUnknownUid,
           "CallEngine —— 轨道还没发布时静默丢掉，不许发一帧空 track_id 上去") {
  /*
    宿主在 onUserEnter 就把格子建好、顺手报个 l 是最自然的写法，而那个人的
    视频轨可能几百毫秒后才到。这时**什么都不做**是对的；发一帧 track_id 为空的
    update_layer 上去，换回来的是 1301 track_not_found，界面上会冒出一个
    莫名其妙的错误提示。宿主该在 onUserVideoAvailable 里补报一次——
    这条写在 imrtc_c.h 里。
  */
  Harness harness;
  joinedWithTracks(harness, "[]");

  const std::size_t before = harness.net.current().sent.size();
  harness.engine->setRemoteLayer("bob", "h");
  CHECK_EQ(harness.net.current().sent.size(), before, "一帧都不该发");
}

IMRTC_TEST(engineCallbackCoverage, "CallEngine —— 两台状态机能抛的每一个回调名，映射表里都有人接") {
  // 「加回调时忘了改映射表」是静默的：宿主永远收不到那个事件，也没人报错。
  // 这条用例把向量里出现过的回调名全部过一遍映射表。
  CallEngineObserver sink;
  std::vector<std::string> names;

  for (const char* file : {"call_fsm.json", "room_fsm.json"}) {
    const Json vector = imtest::loadVector(file);
    for (const Json& testCase : vector.find("cases")->items()) {
      for (const Json& step : testCase.find("steps")->items()) {
        const Json* emit = step.find("emit");
        if (emit == nullptr) continue;
        for (const Json& event : emit->items()) {
          const Json* cb = event.find("cb");
          if (cb != nullptr && cb->isString()) names.push_back(cb->asString());
        }
      }
    }
  }
  CHECK_TRUE(names.size() > 20, "向量里应当出现足够多的回调名");

  for (const std::string& name : names) {
    imrtc::EmittedEvent event;
    event.cb = name;
    event.args = Json::makeObject();
    CHECK_TRUE(imrtc::dispatchObserverEvent(sink, event),
               "映射表里没有 " + name + "——加回调时 CallEngineObserver.h 与 "
               "CallEngineEvents.cpp 两处要一起改");
  }

  // 反向哨兵：一个不存在的回调名必须返回 false，否则上面那条断言等于没测。
  imrtc::EmittedEvent bogus;
  bogus.cb = "onSomethingNobodyDeclared";
  bogus.args = Json::makeObject();
  CHECK_EQ(imrtc::dispatchObserverEvent(sink, bogus), false, "未知回调名必须报 false");
}
