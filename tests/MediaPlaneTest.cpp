#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "FakeMediaAdapter.h"
#include "FakeTransport.h"
#include "TestHarness.h"

#include "imrtc/CallEngine.h"
#include "imrtc/Registry.h"

using imrtc::CallEngine;
using imrtc::CallEngineObserver;
using imrtc::CallEngineOptions;
using imrtc::CallState;
using imrtc::Json;
using imrtc::MediaKind;
using imrtc::PcRole;
using imrtc::PcState;

/**
 * 媒体面的接线测试：信令帧 ↔ MediaAdapter 的调用。
 *
 * 用假适配器，所以**不需要 libwebrtc、不需要摄像头、不需要网络**。这一层测的是
 * 「什么时候该问媒体层要什么」，而不是「SDP 生成得对不对」——后者要等真适配器。
 */
namespace {

constexpr std::int64_t kT0 = 1756876800000;

class Recorder : public CallEngineObserver {
public:
  void onError(std::int32_t code, const std::string& name, const std::string&) override {
    log.push_back("error:" + std::to_string(code) + "/" + name);
  }
  void onRoomJoined(const std::string& roomId) override { log.push_back("roomJoined:" + roomId); }
  void onCallEnd(const imrtc::CallEnd& end) override { log.push_back("callEnd:" + end.reason); }
  std::vector<std::string> log;
};

struct Harness {
  imtest::FakeNet net;
  std::shared_ptr<imtest::FakeMediaAdapter> media = std::make_shared<imtest::FakeMediaAdapter>();
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
    options.mediaAdapter = media;
    engine.reset(new CallEngine(options));
    engine->setObserver(recorder);
  }

  std::string lastReqId() { return imtest::field(imtest::lastSent(net.current()), "req_id"); }
  void reply(const std::string& type, Json data) {
    net.deliver(imtest::replyFrame(type, lastReqId(), std::move(data)));
  }
  void event(const std::string& type, Json data, const std::string& reqId = "") {
    net.deliver(imtest::replyFrame(type, reqId, std::move(data)));
  }

  void login() {
    engine->login("tk-1");
    net.open();
    reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-1", false));
  }

  /** enterRoom 走到「已进房」。mediaType 决定要不要开摄像头。 */
  void enterRoom(const std::string& mediaType) {
    login();
    engine->call({"bob"}, mediaType, false);
    reply(imrtc::okType(imrtc::frame::kCallInvite),
          Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\"}"));
    event(imrtc::frame::kCallConnected,
          Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\",\"room_token\":\"tk\","
                      "\"media_type\":\"" + mediaType + "\",\"connected_at_ms\":1756876800000,"
                      "\"accepted_by\":\"bob\"}"));
    reply(imrtc::okType(imrtc::frame::kRoomJoin),
          Json::parse("{\"room_id\":\"r-1\",\"room_kind\":\"call_1v1\","
                      "\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":[]}"));
  }

  /** sentTypes 列出线路上出现过的帧类型，便于整体断言。 */
  std::vector<std::string> sentTypes() {
    std::vector<std::string> types;
    for (const std::string& raw : net.current().sent) {
      types.push_back(imtest::field(Json::parse(raw), "type"));
    }
    return types;
  }

  /** countSent 数线路上某类型的帧有几条（断言「一条都没有」时用）。 */
  std::size_t countSent(const std::string& type) {
    const std::vector<std::string> types = sentTypes();
    return static_cast<std::size_t>(std::count(types.begin(), types.end(), type));
  }

  /** findSent 找最后一条某类型的帧。 */
  Json findSent(const std::string& type) {
    for (auto it = net.current().sent.rbegin(); it != net.current().sent.rend(); ++it) {
      const Json frame = Json::parse(*it);
      if (imtest::field(frame, "type") == type) return frame;
    }
    imtest::fail("findSent", "线路上没有 " + type);
  }
};

}  // namespace

IMRTC_TEST(mediaVideoCallPublishesBoth, "MediaPlane —— 视频通话进房后采麦克风 + 摄像头，各发一条 room.publish") {
  Harness harness;
  harness.enterRoom("video");

  CHECK_EQ(harness.media->callCount("acquireMicrophone"), 1, "该采麦克风");
  CHECK_EQ(harness.media->callCount("acquireCamera"), 1, "视频通话该采摄像头");

  const Json publish = harness.findSent(imrtc::frame::kRoomPublish);
  CHECK_EQ(imtest::field(publish, "cid"), std::string("local-cam-1"), "最后一条发的是视频轨");
  CHECK_EQ(imtest::field(publish, "kind"), std::string("video"), "kind");
  CHECK_EQ(imtest::field(publish, "source"), std::string("camera"), "source");
  // 视频才开 simulcast：音频没有分层这回事。
  CHECK_TRUE(publish.find("data")->find("simulcast")->asBool(), "视频轨要开 simulcast");
}

IMRTC_TEST(mediaAudioCallSkipsCamera, "MediaPlane —— 语音通话不碰摄像头（拍板 §11-10：连按钮都没有）") {
  Harness harness;
  harness.enterRoom("audio");

  CHECK_EQ(harness.media->callCount("acquireMicrophone"), 1, "该采麦克风");
  CHECK_EQ(harness.media->callCount("acquireCamera"), 0, "语音通话绝不该开摄像头");
}

IMRTC_TEST(mediaMicDeniedStopsPublish, "MediaPlane —— 麦克风被拒：报错且不发 publish") {
  Harness harness;
  harness.media->micAllowed = false;
  harness.enterRoom("audio");

  CHECK_EQ(harness.recorder->log.back(), std::string("error:2001/device_permission_denied"),
           "该报权限被拒");
  for (const std::string& type : harness.sentTypes()) {
    CHECK_TRUE(type != "room.publish", "麦克风都没有，不该发 publish");
  }
}

IMRTC_TEST(mediaCameraDeniedKeepsAudio, "MediaPlane —— 摄像头被拒只降级为语音，音频照发（交互稿 §02）") {
  Harness harness;
  harness.media->cameraAllowed = false;
  harness.enterRoom("video");

  const Json publish = harness.findSent(imrtc::frame::kRoomPublish);
  CHECK_EQ(imtest::field(publish, "kind"), std::string("audio"), "只该有音频那条");
  CHECK_EQ(harness.recorder->log.back(), std::string("error:2001/device_permission_denied"),
           "摄像头的错还是要报");
}

IMRTC_TEST(mediaPubOfferCarriesRealSdp, "MediaPlane —— 状态机产出的空 SDP 被接管，pub offer 带真 SDP 且是请求") {
  Harness harness;
  harness.enterRoom("audio");

  // publish.ok 之后房间机才发 pub offer（要先拿到 track_id，§3.2）。
  harness.reply(imrtc::okType(imrtc::frame::kRoomPublish),
                Json::parse("{\"track_id\":\"t-1\",\"cid\":\"local-mic-1\"}"));

  CHECK_EQ(harness.media->callCount("createPubOffer"), 1, "该问媒体层要一份 offer");
  const Json offer = harness.findSent(imrtc::frame::kRoomOffer);
  CHECK_EQ(imtest::field(offer, "pc"), std::string("pub"), "是上行那条");
  CHECK_EQ(imtest::field(offer, "sdp"), std::string("v=0\r\npub-offer"), "带的是真 SDP，不是空串");
  // pub 的 offerer 恒为本端，所以它是**我们发起的请求**，必须有 req_id（§3.3、§2.2）。
  CHECK_TRUE(!imtest::field(offer, "req_id").empty(), "pub offer 是请求，req_id 不能空");
}

IMRTC_TEST(mediaSubOfferAnswered, "MediaPlane —— 服务端的 sub offer：拿它生成 answer 并回显 req_id") {
  Harness harness;
  harness.enterRoom("audio");

  const std::string serverReqId = "srv-42";
  harness.event(imrtc::frame::kRoomOffer,
                Json::parse("{\"pc\":\"sub\",\"sdp\":\"v=0\\r\\nserver-offer\"}"), serverReqId);

  CHECK_EQ(harness.media->answeredSubOffers,
           std::vector<std::string>{"v=0\r\nserver-offer"}, "拿服务端那份 offer 去生成应答");
  const Json answer = harness.findSent(imrtc::frame::kRoomAnswer);
  CHECK_EQ(imtest::field(answer, "sdp"), std::string("v=0\r\nsub-answer"), "带真 SDP");
  CHECK_EQ(imtest::field(answer, "req_id"), serverReqId, "必须回显服务端那个 req_id");
}

IMRTC_TEST(mediaPubAnswerApplied, "MediaPlane —— pub offer 的应答（room.answer）要喂给媒体层") {
  Harness harness;
  harness.enterRoom("audio");
  harness.reply(imrtc::okType(imrtc::frame::kRoomPublish),
                Json::parse("{\"track_id\":\"t-1\",\"cid\":\"local-mic-1\"}"));

  // pub offer 是请求，它的应答就是 room.answer（没有 .ok，§3.3）。
  harness.reply(imrtc::frame::kRoomAnswer,
                Json::parse("{\"pc\":\"pub\",\"sdp\":\"v=0\\r\\nserver-answer\"}"));
  CHECK_EQ(harness.media->appliedPubAnswers, std::vector<std::string>{"v=0\r\nserver-answer"},
           "应答要落到 pub PC 上");
}

IMRTC_TEST(mediaCandidatesBothWays, "MediaPlane —— 候选双向：本端的发上去，远端的收下来，空候选忽略") {
  Harness harness;
  harness.enterRoom("audio");

  harness.media->emitLocalCandidate(PcRole::Pub, "candidate:1 1 udp 2130706431 10.0.0.2 7881 typ host");
  const Json sent = harness.findSent(imrtc::frame::kRoomIceCandidate);
  CHECK_EQ(imtest::field(sent, "pc"), std::string("pub"), "本端候选带上 pc");
  CHECK_TRUE(!imtest::field(sent, "candidate").empty(), "本端候选发上去了");

  /*
    **收下行候选这条路径最容易整条漏掉**：只发不收的话，下行能不能连全看运气——
    服务端 SDP 里碰巧带了主机候选就通，没带就永远停在 new，
    界面上是「格子在、画面黑」，而且不报任何错。（Web 端踩过。）
  */
  harness.event(imrtc::frame::kRoomIceCandidate,
                Json::parse("{\"pc\":\"sub\",\"candidate\":\"candidate:2 1 udp 1 1.2.3.4 9 typ srflx\","
                            "\"sdp_mid\":\"0\",\"sdp_mline_index\":0}"));
  CHECK_EQ(harness.media->remoteCandidates.size(), std::size_t{1}, "远端候选要收下来");

  // 空候选 = 收集结束，协议要求容忍（§3.3）——忽略，不往下传。
  harness.event(imrtc::frame::kRoomIceCandidate,
                Json::parse("{\"pc\":\"sub\",\"candidate\":\"\",\"sdp_mid\":\"\",\"sdp_mline_index\":0}"));
  CHECK_EQ(harness.media->remoteCandidates.size(), std::size_t{1}, "空候选不该往下传");
}

IMRTC_TEST(mediaReadyOnlyFromSub, "MediaPlane —— 「媒体就绪」只看 sub PC（能听见对方才算接通）") {
  Harness harness;
  harness.enterRoom("audio");
  CHECK_EQ(harness.engine->callState(), CallState::Connecting, "还没就绪");

  harness.media->emitPcState(PcRole::Pub, PcState::Connected);
  CHECK_EQ(harness.engine->callState(), CallState::Connecting,
           "pub 通了只说明我们发得出去，不算接通");

  harness.media->emitPcState(PcRole::Sub, PcState::Connected);
  CHECK_EQ(harness.engine->callState(), CallState::Connected, "sub 通了才算接通");
}

IMRTC_TEST(mediaResetOnCallEnd, "MediaPlane —— 一轮结束要重建 PC（否则下一轮 offer 多出几条死 m-line）") {
  Harness harness;
  harness.enterRoom("audio");
  CHECK_EQ(harness.media->resetCount, 0, "还没结束");

  harness.event(imrtc::frame::kCallEnded,
                Json::parse("{\"call_id\":\"call-1\",\"reason\":\"hangup\",\"duration_sec\":9}"));
  CHECK_EQ(harness.media->resetCount, 1, "通话终局要 reset 媒体面");
}

IMRTC_TEST(mediaMuteGoesBothWays, "MediaPlane —— 开关麦克风：本端停发 + 发 room.mute 让对端知道") {
  Harness harness;
  harness.enterRoom("audio");
  // 先把 track_id 拿到手：线路上的 room.mute 认的是它，不是本端的 cid。
  harness.reply(imrtc::okType(imrtc::frame::kRoomPublish),
                Json::parse("{\"track_id\":\"t-1\",\"cid\":\"local-mic-1\"}"));

  harness.engine->closeMic();
  CHECK_EQ(harness.media->muted, std::vector<std::string>{"local-mic-1:muted"}, "本端要停发");
  const Json mute = harness.findSent(imrtc::frame::kRoomMute);
  /*
    **回归**：早先这里发的是 cid（"local-mic-1"）。服务端按 track_id 查，查不到那条轨道，
    于是不广播 room.track_muted —— 本端停发了，房里其他人的麦克风图标却永远不变，
    而且两端都不报错。发上去的必须是服务端分配的 t-1（§3.2、room_fsm.json 第 10 步）。
  */
  CHECK_EQ(imtest::field(mute, "track_id"), std::string("t-1"), "发的是服务端的 track_id");
  CHECK_TRUE(mute.find("data")->find("muted")->asBool(), "muted=true");

  harness.engine->openMic();
  CHECK_EQ(harness.media->muted.size(), std::size_t{2}, "再开一次");
  CHECK_EQ(harness.media->muted.back(), std::string("local-mic-1:live"), "恢复发包");
}

IMRTC_TEST(mediaMuteBeforePublishOkIsReported,
           "MediaPlane —— publish.ok 还没回来就静音：本端照停，但不许发一帧废的上去") {
  Harness harness;
  harness.enterRoom("audio");
  // 故意**不**回 publish.ok：此刻 cid 有了，track_id 还没有。

  harness.engine->closeMic();
  CHECK_EQ(harness.media->muted, std::vector<std::string>{"local-mic-1:muted"}, "本端仍要停发");
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomMute), std::size_t{0},
           "线路上不该出现 room.mute —— 没有能指代这条轨道的 id");
  CHECK_EQ(harness.recorder->log.back(), std::string("error:2005/invalid_state"),
           "不能静悄悄吞掉：对端这次不会知道");
}

IMRTC_TEST(mediaAttachLocalViewIsSeparate,
           "MediaPlane —— 本端预览是独立的一条口子，不走远端轨道那条") {
  Harness harness;
  harness.enterRoom("video");

  int local = 0;
  harness.engine->attachLocalView(&local);
  CHECK_EQ(harness.media->attachedViews, std::vector<std::string>{"local:attach"},
           "本端预览直接落到适配器上，不需要任何 trackId");

  // 远端那条要有真轨道才挂得上；挂一个不存在的 uid 是空操作，不是错误
  // （宿主在 onUserEnter 就建好格子，那时对方的视频轨可能还没发布）。
  int remote = 0;
  harness.engine->attachView("nobody", &remote);
  CHECK_EQ(harness.media->attachedViews.size(), std::size_t{1}, "陌生 uid 不产生 attach");

  harness.engine->attachLocalView(nullptr);
  CHECK_EQ(harness.media->attachedViews.back(), std::string("local:detach"), "传空即卸载");
}

IMRTC_TEST(mediaDeferredCompletions, "MediaPlane —— 异步完成：poll() 之前一帧都不该发（真适配器就是异步的）") {
  Harness harness;
  harness.media->deferCompletions = true;
  harness.enterRoom("audio");

  // 采集还没完成，publish 无从谈起。
  for (const std::string& type : harness.sentTypes()) {
    CHECK_TRUE(type != "room.publish", "完成回调还没放出来，不该发 publish");
  }

  harness.engine->tick();  // tick 里会 poll 媒体面
  const Json publish = harness.findSent(imrtc::frame::kRoomPublish);
  CHECK_EQ(imtest::field(publish, "cid"), std::string("local-mic-1"), "poll 之后才发出去");
}

IMRTC_TEST(mediaNoAdapterIsPureSignaling, "CallEngine —— 不给适配器就是纯信令模式：能拨号，只是没声音画面") {
  imtest::FakeNet net;
  auto recorder = std::make_shared<Recorder>();
  std::int64_t now = kT0;

  CallEngineOptions options;
  options.url = "wss://rtc.example.com/v1/ws";
  options.deviceId = "mac-8f3a";
  options.transportFactory = net.factory();
  options.clock = [&now]() { return now; };
  // mediaAdapter 留空。

  CallEngine engine(options);
  engine.setObserver(recorder);
  engine.login("tk-1");
  net.open();
  const std::string reqId = imtest::field(imtest::lastSent(net.current()), "req_id");
  net.deliver(imtest::replyFrame("sys.hello.ok", reqId, imtest::helloOkData("s-1", false)));

  engine.call({"bob"}, "video", false);
  CHECK_EQ(imtest::field(imtest::lastSent(net.current()), "type"), std::string("call.invite"),
           "没有媒体也照样能拨号");

  // 媒体方法全部是空操作，不许崩。
  engine.openMic();
  engine.closeCamera();
  engine.attachView("bob", nullptr);
  engine.attachLocalView(nullptr);
  CHECK_EQ(engine.callState(), CallState::Inviting, "状态机照常");
}

/**
 * ICE 失败自愈与恢复后重协商（协议 §3.3 / §1.4）。
 *
 * 两个触发点**都要有**，理由见 MediaPlane.cpp 里那两段注释：
 * `failed` 那条覆盖「信令还活着、只有媒体路径断了」；`resumed` 那条覆盖
 * 「网整个断了」——后者才是它最该生效的场景，而恰恰是 failed 够不着的那个。
 */
IMRTC_TEST(iceRestartOnPubFailed, "ICE 自愈 —— pub 判 failed 就重启，并且那一帧真的带着重启位") {
  Harness harness;
  harness.enterRoom("audio");
  const std::size_t before = harness.countSent(imrtc::frame::kRoomOffer);

  harness.media->emitPcState(PcRole::Pub, PcState::Failed);
  harness.engine->tick();

  CHECK_EQ(harness.media->restartPubIceCalls, 1, "该置一次重启位");
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), before + 1, "该补发一条 room.offer");
  CHECK_EQ(imtest::field(harness.findSent(imrtc::frame::kRoomOffer), "pc"), std::string("pub"),
           "重启的是 pub 那条（sub 由服务端救）");
  /*
    **这一条才是重点。**「要重启」和「要补一次协商」必须分开记：位记在适配器上、
    帧走状态机。位若跟着帧走，忙的时候排队一次就丢了，补出来的是个普通 offer——
    那条连接**永远重连不上，而日志里一切正常**。四端都为这个坑钉了用例。
  */
  CHECK_EQ(harness.media->lastOfferHadIceRestart, true, "出去的 offer 必须带着重启位");
}

IMRTC_TEST(iceRestartNotOnDisconnected,
           "ICE 自愈 —— disconnected 不动它（那是几秒的抖动，见着就重启是自造风暴）") {
  Harness harness;
  harness.enterRoom("audio");
  const std::size_t before = harness.countSent(imrtc::frame::kRoomOffer);

  harness.media->emitPcState(PcRole::Pub, PcState::Disconnected);
  harness.engine->tick();

  CHECK_EQ(harness.media->restartPubIceCalls, 0, "不该重启");
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), before, "不该多发帧");
}

IMRTC_TEST(iceRestartSubIsServersJob,
           "ICE 自愈 —— sub 断了我们救不了（offerer 是服务端），只报给宿主") {
  Harness harness;
  harness.enterRoom("audio");
  const std::size_t before = harness.countSent(imrtc::frame::kRoomOffer);

  harness.media->emitPcState(PcRole::Sub, PcState::Failed);
  harness.engine->tick();

  CHECK_EQ(harness.media->restartPubIceCalls, 0, "不该去重启 pub");
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), before, "不该发 offer");
  const std::vector<std::string>& log = harness.recorder->log;
  CHECK_EQ(std::find(log.begin(), log.end(), std::string("error:2006/media_negotiation_failed")) !=
               log.end(),
           true, "该报一条给宿主");
}

IMRTC_TEST(iceRestartLostWhileReconnecting,
           "ICE 自愈 —— reconnecting 期间的重启请求会被丢掉（**这就是光靠 failed 不够的原因**）") {
  Harness harness;
  harness.enterRoom("audio");
  const std::size_t before = harness.countSent(imrtc::frame::kRoomOffer);

  // 网断了：信令先断，房间进 reconnecting。PC 要再过约 30 秒才判 failed。
  harness.net.remoteClose(imrtc::closecode::kGoingAway, "network lost");
  harness.media->emitPcState(PcRole::Pub, PcState::Failed);
  harness.engine->tick();

  /*
    位置上了，帧却发不出去：`restart_pub_ice` 刻意不进 isBufferable，
    于是在 reconnecting 上被本地拒掉。**这不是 bug，是这条用例要钉住的现状**——
    它正是「触发点必须挪到恢复之后」的全部依据（iOS 真机 2026-09-07 的实证）。
  */
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), before, "reconnecting 期间发不出去");

  // 恢复之后**只补一条**。
  // 这一句同时钉住「不进 isBufferable」：真把它做成可缓冲的，重放会再补一条，
  // 于是同一次恢复发出两条 offer——多一次没必要的协商，而且两条在飞会互相覆盖。
  harness.now += 1000;
  harness.engine->tick();
  harness.net.open();
  harness.reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-1", true));
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{1},
           "恢复之后只补一条（新连接上的第一条）");
}

IMRTC_TEST(iceRestartAfterResume, "恢复后重协商 —— resumed=true 补一条 room.offer{pub}，且带重启位") {
  Harness harness;
  harness.enterRoom("audio");
  const std::size_t before = harness.countSent(imrtc::frame::kRoomOffer);

  harness.net.remoteClose(imrtc::closecode::kGoingAway, "network lost");
  harness.now += 1000;
  harness.engine->tick();  // 退避第一档到点，重连
  harness.net.open();
  harness.reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-1", true));

  CHECK_EQ(harness.media->restartPubIceCalls, 1, "恢复之后该重启一次");
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), before + 1, "该补发一条 room.offer");
  CHECK_EQ(imtest::field(harness.findSent(imrtc::frame::kRoomOffer), "pc"), std::string("pub"),
           "补的是 pub 那条");
  CHECK_EQ(harness.media->lastOfferHadIceRestart, true, "补出来的 offer 必须带着重启位");
}

IMRTC_TEST(noRenegotiateWhenNotResumed,
           "恢复后重协商 —— resumed=false 不补（房间已归零，没有上行可谈）") {
  Harness harness;
  harness.enterRoom("audio");

  harness.net.remoteClose(imrtc::closecode::kGoingAway, "network lost");
  harness.now += 1000;
  harness.engine->tick();
  harness.net.open();
  const std::size_t before = harness.countSent(imrtc::frame::kRoomOffer);
  harness.reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-2", false));

  CHECK_EQ(harness.media->restartPubIceCalls, 0, "不该重启");
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), before, "不该补帧");
}
