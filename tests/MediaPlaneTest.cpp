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

/**
 * 上行协商闸门（协议 §3.3）：**同一时刻只许一个 pub offer 在飞**。
 *
 * 不加的后果不是「多一次协商」：offer#2 的 setLocalDescription 覆盖掉 offer#1，
 * answer#1 回来时本端已经不是当初那个 offer 了。iOS 真机上是
 * `Called in wrong state: stable (INVALID_STATE)`（那次自愈了），
 * **Android 上同一个缺陷的后果是上行再也协商不出去**。
 */
namespace {

/** answerPubOffer 回一条 pub answer，走 req_id 配对（§3.3：answer 就是 offer 的应答）。 */
void answerPubOffer(Harness& harness, const std::string& reqId) {
  harness.net.deliver(imtest::replyFrame(
      imrtc::frame::kRoomAnswer, reqId,
      Json::parse("{\"pc\":\"pub\",\"sdp\":\"v=0\\r\\npub-answer\"}")));
}

/** lastOfferReqId 取线路上最后一条 pub offer 的 req_id。 */
std::string lastOfferReqId(Harness& harness) {
  return imtest::field(harness.findSent(imrtc::frame::kRoomOffer), "req_id");
}

/**
 * reqIdsOf 按顺序列出线路上某类型每一帧的 req_id。
 *
 * **不能用 Harness::reply**：它回的是「最后发出去的那一帧」的 req_id，
 * 而两条轨道时最后一帧是第二条 publish，第一条就再也答不上了——
 * 应答类型对不上会被当成事件静默忽略，于是用例在一个假的前提上绿。
 */
std::vector<std::string> reqIdsOf(Harness& harness, const std::string& type) {
  std::vector<std::string> ids;
  for (const std::string& raw : harness.net.current().sent) {
    const Json frame = Json::parse(raw);
    if (imtest::field(frame, "type") == type) ids.push_back(imtest::field(frame, "req_id"));
  }
  return ids;
}

/** publishOk 用指定的 req_id 回一条 room.publish.ok。 */
void publishOk(Harness& harness, const std::string& reqId, const std::string& trackId,
               const std::string& cid) {
  harness.net.deliver(imtest::replyFrame(imrtc::okType(imrtc::frame::kRoomPublish), reqId,
                                         Json::parse("{\"track_id\":\"" + trackId +
                                                     "\",\"cid\":\"" + cid + "\"}")));
}

}  // namespace

IMRTC_TEST(pubOfferGateHoldsSecond,
           "上行协商闸门 —— 两条轨道两次 publish.ok，第二条 offer 要等第一条的 answer") {
  Harness harness;
  harness.enterRoom("video");  // 视频通话发 audio + video 两条轨道

  const std::vector<std::string> publishIds = reqIdsOf(harness, imrtc::frame::kRoomPublish);
  CHECK_EQ(publishIds.size(), std::size_t{2}, "视频通话发了两条 room.publish");

  // 第一条轨道的 publish.ok → 第一条 offer 出去。
  publishOk(harness, publishIds[0], "t-1", "local-mic-1");
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{1}, "第一条 offer 出去了");
  const std::string firstReqId = lastOfferReqId(harness);

  /*
    第二条轨道的 publish.ok 紧跟着来——**真机上就是这个时序**（audio 与 video
    几乎同时发布）。第二条 offer 必须被拦住，否则它的 setLocalDescription 会盖掉
    第一条，第一条的 answer 回来就落在一个对不上的本地描述上。
  */
  publishOk(harness, publishIds[1], "t-2", "local-cam-1");
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{1}, "第二条要被闸住");
  CHECK_EQ(harness.media->callCount("createPubOffer"), 1, "连 offer 都不该生成");

  // 第一条的 answer 落地 → 放闸 → 补发攒下的那一条。
  answerPubOffer(harness, firstReqId);
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{2}, "放闸后补发第二条");
  CHECK_EQ(harness.media->callCount("createPubOffer"), 2, "这时才生成第二份 offer");
  CHECK_TRUE(lastOfferReqId(harness) != firstReqId, "补发的是一条新请求");
}

IMRTC_TEST(pubOfferGateCoalesces,
           "上行协商闸门 —— 闸住期间来三条也只补一条（offer 描述的是当前全部轨道）") {
  Harness harness;
  harness.enterRoom("audio");
  harness.reply(imrtc::okType(imrtc::frame::kRoomPublish),
                Json::parse("{\"track_id\":\"t-1\",\"cid\":\"local-mic-1\"}"));
  const std::string firstReqId = lastOfferReqId(harness);

  // 闸住期间连来三次重启请求。
  for (int i = 0; i < 3; ++i) {
    harness.media->emitPcState(PcRole::Pub, PcState::Failed);
    harness.engine->tick();
  }
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{1}, "全被闸住");

  answerPubOffer(harness, firstReqId);
  // 攒一条就够：offer 描述的是**当前**全部轨道的状态，三条待办合成一条不丢任何东西。
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{2}, "只补一条，不是三条");
}

IMRTC_TEST(pubOfferGateReleasedOnAnswerFailure,
           "上行协商闸门 —— answer 应用失败也要放闸（不会再有第二条 answer 回来）") {
  Harness harness;
  harness.enterRoom("audio");
  harness.reply(imrtc::okType(imrtc::frame::kRoomPublish),
                Json::parse("{\"track_id\":\"t-1\",\"cid\":\"local-mic-1\"}"));
  const std::string firstReqId = lastOfferReqId(harness);

  harness.media->applyPubAnswerAllowed = false;  // 让 answer 落地失败
  answerPubOffer(harness, firstReqId);

  // 闸放了才谈得上下一轮：再来一次重启应当真的发得出去。
  harness.media->emitPcState(PcRole::Pub, PcState::Failed);
  harness.engine->tick();
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{2}, "失败之后闸门必须是开的");
}

IMRTC_TEST(pubOfferGateSurvivesSendFailure,
           "上行协商闸门 —— offer 生成到一半连接断了：不崩、不卡死，恢复后照样协商") {
  Harness harness;
  harness.enterRoom("audio");

  /*
    构造「createPubOffer 还没回来，连接就断了」这个窗口：把假适配器改成异步完成，
    发起协商之后再断线，然后 poll —— 完成回调这时才跑，`deps_.send` 返回 false。

    **这一条是安全网，不是唯一防线**：真到了这一步，房间已经在 reconnecting，
    随后的恢复会走 resetPubNegotiation 把闸门清零。所以它单独拿掉也不会让
    下面的断言变红——留着是因为「四个终局一个都不能少」，漏一个就是上行永久沉默，
    而这种病没有任何症状可查。真正把它钉住的是代码里那句注释与这里的路径覆盖。
  */
  harness.media->deferCompletions = true;
  harness.reply(imrtc::okType(imrtc::frame::kRoomPublish),
                Json::parse("{\"track_id\":\"t-1\",\"cid\":\"local-mic-1\"}"));
  CHECK_EQ(harness.media->callCount("createPubOffer"), 1, "已经去要 offer 了");
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{0}, "还没发出去");

  harness.net.remoteClose(imrtc::closecode::kGoingAway, "network lost");
  harness.engine->tick();  // 完成回调在这里跑，send 会失败
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{0}, "断了就发不出去");

  // 恢复之后上行照样协商得起来。
  harness.media->deferCompletions = false;
  harness.now += 1000;
  harness.engine->tick();
  harness.net.open();
  harness.reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-1", true));
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{1},
           "新连接上补得出来（闸门没被卡死）");
}

IMRTC_TEST(pubOfferGateResetOnResume,
           "上行协商闸门 —— 会话恢复要清零：旧 answer 永远不会回来了") {
  Harness harness;
  harness.enterRoom("audio");
  harness.reply(imrtc::okType(imrtc::frame::kRoomPublish),
                Json::parse("{\"track_id\":\"t-1\",\"cid\":\"local-mic-1\"}"));
  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{1}, "第一条在飞");

  /*
    断线时那条 offer 的 answer **永远不会回来**（在途请求被 2003 结掉，
    而门面对 2003 是刻意放过的）。闸门不清就是 Android 上那个「上行永久沉默」：
    它停在「有一个在飞」，而那个东西活在一条已经不存在的连接上。

    **这条断言钉的是「两条放闸路径至少得留一条」**，不是其中某一条。实测过：
    单独拿掉「恢复时清零」或单独拿掉「请求失败放闸」，这里都不会红——两者互为兜底；
    **两条一起拿掉才红**。是刻意留的冗余，不是重复代码：一条走门面（它才看得见
    请求失败），一条走媒体面（换连接这件事只有恢复那一刻知道），
    将来动其中任何一条，另一条还在。
  */
  harness.net.remoteClose(imrtc::closecode::kGoingAway, "network lost");
  harness.now += 1000;
  harness.engine->tick();
  harness.net.open();
  harness.reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-1", true));

  CHECK_EQ(harness.countSent(imrtc::frame::kRoomOffer), std::size_t{1},
           "新连接上补的那一条发得出去");
}
