#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "EngineHarness.h"
#include "FakeMediaAdapter.h"
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
using enginetest::Harness;
using enginetest::kT0;

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
  CHECK_EQ(static_cast<int>(harness.recorder->lastCallEndReasonCode),
           static_cast<int>(imrtc::EndReason::Hangup),
           "onCallEnd 附带的类型化 reasonCode 要和 reason 字符串对应上");
}

IMRTC_TEST(engineEmitOrderOnSendFailure,
           "CallEngine —— 帧发不出去时，事件顺序不许倒过来（onCallBegin 要在 onRoomLeft 前）") {
  Harness harness;
  harness.login();
  harness.engine->call({"bob"}, "video", false);
  harness.reply(imrtc::okType(imrtc::frame::kCallInvite),
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\"}"));
  harness.event(imrtc::frame::kCallAccepted,
                Json::parse("{\"call_id\":\"call-1\",\"uid\":\"bob\"}"));

  /*
    线路**悄悄发不出去了**（帧还进得来，但 send 走不掉）。真实里就是这个形状：
    TCP 已经断了而关闭事件还没到，或者对端半关。

    call.connected 这一步同时产出 **onCallBegin（事件）与一帧 room.join（要发）**，
    正好是重入顺序问题的复现点：room.join 发不出去 → failLocally → 内层 apply
    跑完整个 dispatchOutput，把 onError / onRoomLeft 都抛了，
    而外层的 onCallBegin 还一条没抛。
  */
  harness.net.current().open = false;
  harness.event(imrtc::frame::kCallConnected,
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\",\"room_token\":\"tk\","
                            "\"media_type\":\"video\",\"is_group\":false,"
                            "\"connected_at_ms\":1756876812000,\"accepted_by\":\"bob\"}"));

  /*
    要守的就是这个顺序：**先有这通电话，才谈得上它为什么结束**。
    修之前这里是 error → roomLeft → callBegin，宿主拿着一条「结束」去关一个
    它还不知道存在的通话，界面收不了场。
  */
  const std::vector<std::string> want{"connected:s-1/fresh", "userAccept:bob",
                                      "callBegin:call-1/caller", "error:2003/network_unreachable",
                                      "roomLeft:r-1"};
  CHECK_EQ(harness.recorder->log, want, "生命周期顺序：先 callBegin，再失败，最后 roomLeft");
}

IMRTC_TEST(engineInviteFailure, "CallEngine —— invite 被拒要回 idle 并抛 onCallEnd，否则界面永远收不了场") {
  Harness harness;
  harness.login();
  // 不传回调：失败退回 onError（ACTION_RESULT_DESIGN R7），顺序同 2.0.0 之前。
  harness.engine->call({"bob", "carol"}, "audio", true);
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
  CHECK_EQ(static_cast<int>(harness.recorder->lastCallEndReasonCode),
           static_cast<int>(imrtc::EndReason::Error), "reason=error 对应 EndReason::Error");
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
  CHECK_EQ(harness.recorder->log, std::vector<std::string>{"disconnected:1001/retry"},
           "只报断开，且带上关闭码与 willReconnect");
  CHECK_EQ(harness.engine->callState(), CallState::Connecting, "通话不许因为断线就没了");

  harness.now = kT0 + 1000;
  harness.engine->tick();
  harness.net.open();
  // 超窗了：服务端已按 reason=network 结束通话，我们本地补一条 onCallEnd。
  harness.now = kT0 + 5000;
  harness.reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-2", false));

  CHECK_EQ(harness.recorder->log,
           std::vector<std::string>(
               {"disconnected:1001/retry", "connected:s-2/fresh", "callEnd:network/5"}),
           "恢复失败要合成 onCallEnd(network)，时长用本地计时");
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "合成之后回 idle");
}

IMRTC_TEST(engineKickedOut, "CallEngine —— 4403 被踢：抛 onKickedOut + onDisconnected 并清空一切") {
  Harness harness;
  harness.login();
  harness.engine->call({"bob"}, "audio", false);
  harness.recorder->log.clear();

  harness.net.remoteClose(imrtc::closecode::kKickedOut, "logged in elsewhere");
  // 原因是 taken_over 而不是别的两个：4403 的含义就是「别处登录 / 被吊销」，
  // 宿主该回登录页。**它要穿过状态机到达宿主**——状态机那条 emit 的 args 是空的。
  CHECK_EQ(harness.recorder->log,
           std::vector<std::string>({"kickedOut:taken_over", "disconnected:4403/stop"}),
           "被踢的回调顺序，且带上原因；4403 不会自动重连");
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "通话清空");
  CHECK_EQ(harness.engine->roomState(), RoomState::Idle, "房间清空");
}

IMRTC_TEST(engineRejectsBadDeviceId,
           "CallEngine —— device_id 不合规时连 socket 都不开，抛的是和服务端同一个 1004") {
  /*
    真机上就是这个：Android 的 `Build.MODEL` 是 `Pixel 2 XL`，带空格违反协议 §2.5。

    不拦的话服务端回 1004，而它那句说得很清楚的 charset 说明到不了宿主手里——
    宿主看到的只有一个光秃秃的 bad_params，界面上只有「登录失败」。
  */
  Harness harness("Pixel 2 XL");
  harness.engine->login("tk-1");

  CHECK_EQ(harness.net.socketCount(), std::size_t{0}, "**一个 socket 都不该开**");
  CHECK_EQ(harness.recorder->log, std::vector<std::string>{"error:1004/bad_params"},
           "抛的码与服务端拒绝时同一个，宿主不用写两遍分支");
}

IMRTC_TEST(engineAcceptsValidDeviceId,
           "CallEngine —— 合法 device_id 照常连（别把校验写成谁都拦）") {
  Harness harness("Pixel_2_XL");
  harness.login();
  CHECK_EQ(harness.recorder->log, std::vector<std::string>{"connected:s-1/fresh"}, "照常握手");
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

IMRTC_TEST(engineLogoutIsQuietWithAnAsyncTransport,
           "CallEngine —— logout 之后再 tick，不许冒出一条 onDisconnected（真实 Transport 是异步的）") {
  Harness harness;
  harness.net.deferClose = true;  // 像 IxTransport 那样：关闭事件排到 poll() 里放
  harness.login();
  harness.engine->call({"bob"}, "audio", false);
  harness.reply(imrtc::okType(imrtc::frame::kCallInvite),
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\"}"));
  harness.recorder->log.clear();

  harness.engine->logout();
  // 宿主的 tick 还在跑（Demo 里 disconnectFromServer 就明确又 tick 了一次）。
  harness.engine->tick();

  /*
    **回归**：早先 logout() 只是把 tearingDown_ 竖起来、调完 close() 就立刻放平。
    可真实 Transport 的关闭事件要到下一次 poll() 才出来，那时标记早清了，于是
    「用户自己点的退出」还是会收到一条 onDisconnected——有的界面据此闪一下
    「正在重连…」。假件当时是**同步**回调的，所以测试一路全绿。

    现在 logout() 连同连接一起放掉，队列里攒着的事件随之丢弃。
  */
  for (const std::string& entry : harness.recorder->log) {
    // 前缀比对而不是整串相等："disconnected" 现在带着关闭码/willReconnect
    // 后缀（如 "disconnected:1001/retry"），整串比对会让这条断言形同虚设。
    CHECK_TRUE(entry.rfind("disconnected", 0) != 0, "主动 logout 不许冒出 onDisconnected");
  }
  CHECK_EQ(harness.engine->connectionState(), imrtc::ConnectionState::Idle, "连接已经放掉了");
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

/**
 * MediaHarness 是这两条 publish_deferred 用例专用的假宿主：媒体面接上
 * `FakeMediaAdapter`，让「进房/接通就自动发布麦克风轨」这条真实链路跑起来——
 * publish_deferred 只有在这条链路上才测得出来，纯信令模式下没有人会发 room.publish。
 * 复用 `enginetest::Recorder` 拿回调日志。桌面端还没有真实 libwebrtc 适配器
 * （P5 第四刀），媒体面照 MediaPlaneTest.cpp 的先例用假的。
 */
struct MediaHarness {
  imtest::FakeNet net;
  std::shared_ptr<imtest::FakeMediaAdapter> media = std::make_shared<imtest::FakeMediaAdapter>();
  std::shared_ptr<enginetest::Recorder> recorder = std::make_shared<enginetest::Recorder>();
  std::int64_t now = enginetest::kT0;
  std::unique_ptr<CallEngine> engine;

  MediaHarness() {
    CallEngineOptions options;
    options.url = "wss://rtc.example.com/v1/ws";
    options.deviceId = "mac-8f3a";
    options.transportFactory = net.factory();
    options.random = []() { return 0.5; };
    options.clock = [this]() { return now; };
    options.mediaAdapter = media;
    engine.reset(new CallEngine(options));
    engine->setObserver(recorder);
  }

  std::string lastReqId() { return imtest::field(imtest::lastSent(net.current()), "req_id"); }
  void reply(const std::string& type, Json data) {
    net.deliver(imtest::replyFrame(type, lastReqId(), std::move(data)));
  }
  void event(const std::string& type, Json data) {
    net.deliver(imtest::replyFrame(type, "", std::move(data)));
  }
  /**
   * findSent 找**当前 socket** 上最后一条某类型的帧。
   *
   * resume 成功之后 `renegotiateAfterResume()` 会紧跟着补一条 `room.offer`
   * （恢复后重协商，与 publish 重放无关），所以不能拿 `lastSent` 断言重发的
   * `room.publish`——它已经不是这个 socket 上最后一帧了。
   */
  Json findSent(const std::string& type) {
    for (auto it = net.current().sent.rbegin(); it != net.current().sent.rend(); ++it) {
      const Json frame = Json::parse(*it);
      if (imtest::field(frame, "type") == type) return frame;
    }
    imtest::fail("MediaHarness::findSent", "线路上没有 " + type);
  }
  void login() {
    engine->login("tk-1");
    net.open();
    reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-1", false));
  }
  /** reconnectResumed 把退避推到点、开新 socket、握手 resumed=true——两条用例共用的尾段。 */
  void reconnectResumed() {
    now = enginetest::kT0 + 1000;
    engine->tick();
    net.open();
    reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-1", true));
  }
};

IMRTC_TEST(enginePublishDeferredReplayedAfterResumeInMeeting,
           "CallEngine —— 会议房 room.publish 没等到应答：断线挂起，resume 后新连接上重发同一 "
           "cid，房间不散（静默失败审计 §A / 2026-09-18）") {
  /*
    **回归**：修之前会议房里 room.publish 的任何失败（含没等到应答的 2003/2004/2007）
    都直接摘掉 publishing、不重试、不通知宿主——信令抖一下用户就静音，界面上
    什么都看不出。这条钉住「没等到应答」要挂起等重连，而不是当场判死那条 cid。
  */
  MediaHarness h;
  h.login();
  h.engine->joinRoom("r-1", "tk");
  h.reply(imrtc::okType(imrtc::frame::kRoomJoin),
          Json::parse("{\"room_id\":\"r-1\",\"room_kind\":\"meeting\","
                      "\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":[]}"));

  CHECK_EQ(h.media->callCount("acquireMicrophone"), 1, "进会议房该自动采麦克风");
  const Json firstPublish = imtest::lastSent(h.net.current());
  CHECK_EQ(imtest::field(firstPublish, "type"), std::string("room.publish"), "自动发布麦克风轨");
  CHECK_EQ(imtest::field(firstPublish, "cid"), std::string("local-mic-1"), "cid");
  CHECK_EQ(h.engine->roomState(), RoomState::Joined, "此刻已经在房里");

  // 这条 room.publish 还没等到应答（10 秒超时 / 服务端拒绝都没到）就断线了。
  h.net.remoteClose(imrtc::closecode::kGoingAway, "network");
  CHECK_EQ(h.engine->roomState(), RoomState::Reconnecting, "断线进 reconnecting，房间没被判死");
  for (const std::string& entry : h.recorder->log) {
    CHECK_TRUE(entry.rfind("roomLeft", 0) != 0, "没等到应答不该收场，onRoomLeft 不许抛");
  }

  h.reconnectResumed();

  CHECK_EQ(h.engine->roomState(), RoomState::Joined, "恢复窗口内 resume，房间回到 joined");
  const Json replayed = h.findSent(imrtc::frame::kRoomPublish);
  CHECK_EQ(imtest::field(replayed, "cid"), std::string("local-mic-1"), "新连接上要重发同一路 cid");
  CHECK_EQ(h.media->callCount("acquireMicrophone"), 1, "重放走的是状态机 reduceRoomAct，不会再问媒体层要一次轨道");
}

IMRTC_TEST(enginePublishDeferredReplayedAfterResumeInCall,
           "CallEngine —— 通话中 room.publish 没等到应答：断线挂起，resume 后重发同一 cid，"
           "通话没被判死（2026-09-18 真机：9 秒后就 resume 成功了）") {
  /*
    **回归**：修之前通话里 room.publish 的任何失败都直接 forceEnd 整通电话，
    reason=error，不分「服务端拒了」与「没等到应答」。真机 18:18:39 room.publish
    超时、整通被判成 error 收场，而 18:18:48 连接就在恢复窗口内 resume 成功——
    本来能接着打的一通被自己判了死刑。
  */
  MediaHarness h;
  h.login();
  h.engine->call({"bob"}, "audio", false);
  h.reply(imrtc::okType(imrtc::frame::kCallInvite),
          Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\"}"));
  h.event(imrtc::frame::kCallConnected,
          Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\",\"room_token\":\"tk\","
                      "\"media_type\":\"audio\",\"connected_at_ms\":1756876800000,"
                      "\"accepted_by\":\"bob\"}"));
  h.reply(imrtc::okType(imrtc::frame::kRoomJoin),
          Json::parse("{\"room_id\":\"r-1\",\"room_kind\":\"call_1v1\","
                      "\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":[]}"));

  CHECK_EQ(h.media->callCount("acquireMicrophone"), 1, "接通后该自动采麦克风");
  const Json firstPublish = imtest::lastSent(h.net.current());
  CHECK_EQ(imtest::field(firstPublish, "type"), std::string("room.publish"), "自动发布麦克风轨");
  CHECK_EQ(imtest::field(firstPublish, "cid"), std::string("local-mic-1"), "cid");
  CHECK_EQ(h.engine->callState(), CallState::Connecting, "媒体还没就绪，没到 connected");

  h.net.remoteClose(imrtc::closecode::kGoingAway, "network");
  CHECK_EQ(h.engine->callState(), CallState::Connecting, "断线不该让通话被判死");
  CHECK_EQ(h.engine->roomState(), RoomState::Reconnecting, "房间随断线进 reconnecting");
  for (const std::string& entry : h.recorder->log) {
    CHECK_TRUE(entry.rfind("callEnd", 0) != 0, "没等到应答不该 forceEnd，通话不许被收场");
  }

  h.reconnectResumed();

  CHECK_EQ(h.engine->callState(), CallState::Connecting, "resume 成功，通话还在，等媒体就绪");
  CHECK_EQ(h.engine->roomState(), RoomState::Joined, "房间也回到 joined");
  const Json replayed = h.findSent(imrtc::frame::kRoomPublish);
  CHECK_EQ(imtest::field(replayed, "cid"), std::string("local-mic-1"), "新连接上要重发同一路 cid");
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
