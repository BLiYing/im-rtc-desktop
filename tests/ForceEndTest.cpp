#include <cstddef>
#include <string>
#include <vector>

#include "EngineHarness.h"
#include "FakeTransport.h"
#include "TestHarness.h"
#include "imrtc/CallEngine.h"
#include "imrtc/Registry.h"

using enginetest::Harness;
using imrtc::CallState;
using imrtc::Json;
using imrtc::RoomState;

/**
 * forceEnd：红键按下去等不到结束事件时的出口（五端同形，Web `state/forceEnd.ts`）。
 * 结束帧照发、本地立刻收场，服务端之后的 call.ended 不再抛第二次；迟到的应答补发结束帧。
 */
namespace {

/** connectedCall 走到「接通 + 进房成功 + 媒体就绪」。 */
void connectedCall(Harness& harness) {
  harness.login();
  harness.engine->call({"bob"}, "video", false);
  harness.reply(imrtc::okType(imrtc::frame::kCallInvite),
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\"}"));
  harness.event(imrtc::frame::kCallConnected,
                Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\",\"room_token\":\"tk\","
                            "\"media_type\":\"video\",\"is_group\":false,"
                            "\"connected_at_ms\":1756876800000,\"accepted_by\":\"bob\"}"));
  harness.reply(imrtc::okType(imrtc::frame::kRoomJoin),
                Json::parse("{\"room_id\":\"r-1\",\"room_kind\":\"call_1v1\","
                            "\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":[]}"));
  harness.engine->notifyMediaReady();
}

}  // namespace

IMRTC_TEST(forceEndConnectedCall, "forceEnd —— 通话中：发 call.hangup，本地收场只抛一次 onCallEnd") {
  Harness harness;
  harness.now += 100000;  // 本端比整通电话的 connected_at_ms 晚 100 秒才进来（像中途被拉进来的人）
  connectedCall(harness);
  harness.now += 12000;
  harness.engine->forceEnd();
  CHECK_EQ(harness.lastType(), std::string("call.hangup"), "结束帧照发");
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "通话机立刻归零，不等服务端");
  CHECK_EQ(harness.engine->roomState(), RoomState::Idle, "房间也一起归零");
  CHECK_EQ(harness.recorder->log.back(), std::string("callEnd:hangup/12"), "时长从本端 onCallBegin 那一刻算，不是整通的 connected_at_ms");

  const std::size_t before = harness.recorder->log.size();
  harness.event(imrtc::frame::kCallEnded,
                Json::parse("{\"call_id\":\"call-1\",\"reason\":\"hangup\",\"duration_sec\":12,"
                            "\"ended_by\":\"alice\"}"));
  CHECK_EQ(harness.recorder->log.size(), before, "服务端随后的 call.ended 不许再抛一次");
  harness.engine->forceEnd();
  CHECK_EQ(harness.recorder->log.size(), before, "已经收场了再调是空操作");
}

IMRTC_TEST(forceEndRingingRejects, "forceEnd —— 来电响铃中：发 call.reject，reason=reject") {
  Harness harness;
  harness.login();
  harness.event(imrtc::frame::kCallIncoming,
                Json::parse("{\"call_id\":\"call-9\",\"caller\":\"bob\",\"callee_ids\":[\"alice\"],"
                            "\"media_type\":\"audio\",\"is_group\":false}"));
  CHECK_EQ(harness.engine->callState(), CallState::Ringing, "先在响铃");
  harness.engine->forceEnd();
  CHECK_EQ(harness.lastType(), std::string("call.reject"), "响铃中收场 = 拒接");
  CHECK_EQ(harness.recorder->log.back(), std::string("callEnd:reject/0"), "reason=reject");
}

IMRTC_TEST(forceEndBeforeInviteOk, "forceEnd —— invite.ok 还没回来：本地收场，invite.ok 迟到时补发 call.cancel") {
  Harness harness;
  harness.login();
  harness.engine->call({"bob"}, "audio", false);
  const std::string inviteReqId = harness.lastReqId();
  harness.engine->forceEnd();
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "本地收场");
  CHECK_EQ(harness.recorder->log.back(), std::string("callEnd:cancel/0"), "reason=cancel");
  CHECK_EQ(harness.lastType(), std::string("call.invite"), "此刻手里没有 call_id，什么都发不了");

  harness.net.deliver(imtest::replyFrame(imrtc::okType(imrtc::frame::kCallInvite), inviteReqId,
                                         Json::parse("{\"call_id\":\"call-2\",\"room_id\":\"r-2\"}")));
  CHECK_EQ(harness.lastType(), std::string("call.cancel"), "邀请落地了：补发 cancel，别让被叫一直响");
  CHECK_EQ(imtest::field(imtest::lastSent(harness.net.current()), "call_id"), std::string("call-2"),
           "cancel 带的是迟到那条的 call_id");
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "本地状态不动");
}

IMRTC_TEST(lateConnectedAndJoinOkInIdle, "迟到帧 —— idle 下的 call.connected 补发 hangup，room.join.ok 补发 room.leave") {
  Harness harness;
  harness.login();
  harness.event(imrtc::frame::kCallConnected,
                Json::parse("{\"call_id\":\"call-3\",\"room_id\":\"r-3\",\"room_token\":\"tk\","
                            "\"media_type\":\"audio\",\"is_group\":false,"
                            "\"connected_at_ms\":1756876800000,\"accepted_by\":\"bob\"}"));
  CHECK_EQ(harness.lastType(), std::string("call.hangup"), "有人接起来了：补发 hangup");
  harness.event(imrtc::okType(imrtc::frame::kRoomJoin),
                Json::parse("{\"room_id\":\"r-3\",\"room_kind\":\"call_1v1\","
                            "\"participant_id\":\"p-3\",\"participants\":[],\"tracks\":[]}"));
  CHECK_EQ(harness.lastType(), std::string("room.leave"), "服务端放我们进了房：补发 leave");
  CHECK_EQ(harness.engine->roomState(), RoomState::Idle, "原先这里会被搭成 joined");
  CHECK_EQ(harness.recorder->log, std::vector<std::string>{"connected:s-1/fresh"}, "不抛任何回调");
}

IMRTC_TEST(forceEndMeetingLeaves, "forceEnd —— 会议房里：发 room.leave，抛 onRoomLeft，leave.ok 回来不再抛") {
  Harness harness;
  harness.login();
  harness.engine->joinRoom("m-1", "tk");
  harness.reply(imrtc::okType(imrtc::frame::kRoomJoin),
                Json::parse("{\"room_id\":\"m-1\",\"room_kind\":\"meeting\","
                            "\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":[]}"));
  CHECK_EQ(harness.engine->roomState(), RoomState::Joined, "先进房");
  harness.engine->forceEnd();
  CHECK_EQ(harness.lastType(), std::string("room.leave"), "会议里收场 = 离房");
  CHECK_EQ(harness.engine->roomState(), RoomState::Idle, "立刻归零");
  CHECK_EQ(harness.recorder->log.back(), std::string("roomLeft:m-1"), "抛 onRoomLeft");
  const std::size_t before = harness.recorder->log.size();
  harness.reply(imrtc::okType(imrtc::frame::kRoomLeave), Json::parse("{\"room_id\":\"m-1\"}"));
  CHECK_EQ(harness.recorder->log.size(), before, "leave.ok 回来不许再抛 onRoomLeft");
}

IMRTC_TEST(forceEndWithoutConnection, "forceEnd —— 连接断着：只做本地收场，不报错") {
  Harness harness;
  connectedCall(harness);
  harness.net.current().open = false;
  harness.engine->logout();
  harness.engine->forceEnd();
  CHECK_EQ(harness.engine->callState(), CallState::Idle, "logout 之后本来就收掉了，这里是空操作");
}
